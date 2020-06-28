#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <lua.h>
#include <stdio.h>

#include "malloc_hook.h"
#include "skynet.h"
#include "atomic.h"

// turn on MEMORY_CHECK can do more memory check, such as double free
// #define MEMORY_CHECK

#define MEMORY_ALLOCTAG 0x20140605
#define MEMORY_FREETAG 0x0badf00d

////////////////////////////////////////// 内存统计：C环境
static size_t _used_memory = 0;  		// 内存总大小
static size_t _memory_block = 0; 		// 内存块数量

struct mem_data
{
	uint32_t handle;   					// 服务实例句柄
	ssize_t allocated; 					// 分配的内存数量（有符号size_t，可能<0）
};

struct mem_cookie
{
	uint32_t handle;
#ifdef MEMORY_CHECK
	// dogtag： 狗牌，狗带，身份牌。就是你用来公司打卡，挂在脖子上的那个。
	uint32_t dogtag; 					// 内存尾标记，两个状态：使用中（MEMORY_ALLOCTAG），空闲（MEMORY_FREETAG）
#endif
};

#define SLOT_SIZE 0x10000					  // 最多可以统计65536个服务分配的内存
#define PREFIX_SIZE sizeof(struct mem_cookie) // 内存标记的区域，说是PREFIX_（前缀），其实是记在内存的尾端

static struct mem_data mem_stats[SLOT_SIZE];
//////////////////////////////////////////

#ifndef NOUSE_JEMALLOC

#include "jemalloc.h"

// for skynet_lalloc use
#define raw_realloc je_realloc
#define raw_free je_free

/**
 * 获取 mem_stats 中对应 handle 的统计项
 * 如果数据未初始化，则初始化
*/
static ssize_t *
get_allocated_field(uint32_t handle)
{
	int h = (int)(handle & (SLOT_SIZE - 1)); // 直接做mask运算取hash值
	struct mem_data *data = &mem_stats[h];
	uint32_t old_handle = data->handle;
	ssize_t old_alloc = data->allocated;
	if (old_handle == 0 || old_alloc <= 0)
	{
		// 处理未初始化的数据
		// data->allocated may less than zero, because it may not count at start.
		if (!ATOM_CAS(&data->handle, old_handle, handle))
		{
			return 0;
		}
		if (old_alloc < 0)
		{
			ATOM_CAS(&data->allocated, old_alloc, 0);
		}
	}
	if (data->handle != handle)
	{
		return 0;
	}
	return &data->allocated;
}

/**
 * 更新总的累计值和对应服务的统计值
*/
inline static void
update_xmalloc_stat_alloc(uint32_t handle, size_t __n)
{
	ATOM_ADD(&_used_memory, __n);
	ATOM_INC(&_memory_block);
	ssize_t *allocated = get_allocated_field(handle);
	if (allocated)
	{
		ATOM_ADD(allocated, __n);
	}
}

/**
 * 更新总的累计值和对应服务的统计值
*/
inline static void
update_xmalloc_stat_free(uint32_t handle, size_t __n)
{
	ATOM_SUB(&_used_memory, __n);
	ATOM_DEC(&_memory_block);
	ssize_t *allocated = get_allocated_field(handle);
	if (allocated)
	{
		ATOM_SUB(allocated, __n);
	}
}

// malloc -> fill_prefix
inline static void *
fill_prefix(char *ptr)
{
	uint32_t handle = skynet_current_handle();
	size_t size = je_malloc_usable_size(ptr); // 内存块的实际大小

	// 在内存尾部添加 mem_cookie信息
	struct mem_cookie *p = (struct mem_cookie *)(ptr + size - sizeof(struct mem_cookie));
	memcpy(&p->handle, &handle, sizeof(handle));
#ifdef MEMORY_CHECK
	uint32_t dogtag = MEMORY_ALLOCTAG;
	memcpy(&p->dogtag, &dogtag, sizeof(dogtag));
#endif
	update_xmalloc_stat_alloc(handle, size);
	return ptr;
}

// free -> clean_prefix
inline static void *
clean_prefix(char *ptr)
{
	size_t size = je_malloc_usable_size(ptr);
	struct mem_cookie *p = (struct mem_cookie *)(ptr + size - sizeof(struct mem_cookie));
	// 取出 handle，更新统计信息
	uint32_t handle;
	memcpy(&handle, &p->handle, sizeof(handle));
#ifdef MEMORY_CHECK
	uint32_t dogtag;
	memcpy(&dogtag, &p->dogtag, sizeof(dogtag));
	if (dogtag == MEMORY_FREETAG)
	{
		fprintf(stderr, "xmalloc: double free in :%08x\n", handle);
	}
	assert(dogtag == MEMORY_ALLOCTAG); // memory out of bounds
	dogtag = MEMORY_FREETAG;
	memcpy(&p->dogtag, &dogtag, sizeof(dogtag));
#endif
	update_xmalloc_stat_free(handle, size);
	return ptr;
}

// OOM: Out Of Memory
static void malloc_oom(size_t size)
{
	fprintf(stderr, "xmalloc: Out of memory trying to allocate %zu bytes\n",
			size);
	fflush(stderr);
	abort();
}

// memory_info_dump 以及后面的 mallctl_xxx 均是调用 jemalloc 的接口

/**
 * jemalloc的完整dump信息
*/
void memory_info_dump(void)
{
	je_malloc_stats_print(0, 0, 0);
}

/**
 * 
 * jemalloc 提供了读/写内存的接口
 * 
int mallctl(const char *name,
 	void *oldp,
 	size_t *oldlenp,
 	void *newp,
 	size_t newlen);

name 指定了内存分配器中树状命名空间的一个地址。
mallctl_cmd 用来探测地址，不读不写

后面给出3个接口，按不同字节大小对内存进行读/写
mallctl_bool	：1字节
mallctl_opt		：4字节
mallctl_int64	：8字节
*/

bool mallctl_bool(const char *name, bool *newval)
{
	bool v = 0;
	size_t len = sizeof(v);
	if (newval)
	{
		je_mallctl(name, &v, &len, newval, sizeof(bool));
	}
	else
	{
		je_mallctl(name, &v, &len, NULL, 0);
	}
	return v;
}

int mallctl_cmd(const char *name)
{
	return je_mallctl(name, NULL, NULL, NULL, 0);
}

size_t
mallctl_int64(const char *name, size_t *newval)
{
	size_t v = 0;
	size_t len = sizeof(v);
	if (newval)
	{
		je_mallctl(name, &v, &len, newval, sizeof(size_t));
	}
	else
	{
		je_mallctl(name, &v, &len, NULL, 0);
	}
	// skynet_error(NULL, "name: %s, value: %zd\n", name, v);
	return v;
}

int mallctl_opt(const char *name, int *newval)
{
	int v = 0;
	size_t len = sizeof(v);
	if (newval)
	{
		int ret = je_mallctl(name, &v, &len, newval, sizeof(int));
		if (ret == 0)
		{
			skynet_error(NULL, "set new value(%d) for (%s) succeed\n", *newval, name);
		}
		else
		{
			skynet_error(NULL, "set new value(%d) for (%s) failed: error -> %d\n", *newval, name, ret);
		}
	}
	else
	{
		je_mallctl(name, &v, &len, NULL, 0);
	}

	return v;
}

// hook : malloc, realloc, free, calloc

void *
skynet_malloc(size_t size)
{
	// assert(size>0, "xmalloc: malloc at least ONE byte!")
	void *ptr = je_malloc(size + PREFIX_SIZE);
	if (!ptr)
		malloc_oom(size);
	return fill_prefix(ptr);
}

void *
skynet_realloc(void *ptr, size_t size)
{
	if (ptr == NULL)
		return skynet_malloc(size);
	// realloc 没有考虑原先这块内存是属于哪个服务的，而是直接将 handle 更新为当前服务
	void *rawptr = clean_prefix(ptr);
	void *newptr = je_realloc(rawptr, size + PREFIX_SIZE);
	if (!newptr)
		malloc_oom(size);
	return fill_prefix(newptr);
}

void skynet_free(void *ptr)
{
	if (ptr == NULL)
		return;
	void *rawptr = clean_prefix(ptr);
	je_free(rawptr);
}

void *
skynet_calloc(size_t nmemb, size_t size)
{
	void *ptr = je_calloc(nmemb + ((PREFIX_SIZE + size - 1) / size), size);
	if (!ptr)
		malloc_oom(size);
	return fill_prefix(ptr);
}

void *
skynet_memalign(size_t alignment, size_t size)
{
	void *ptr = je_memalign(alignment, size + PREFIX_SIZE);
	if (!ptr)
		malloc_oom(size);
	return fill_prefix(ptr);
}

void *
skynet_aligned_alloc(size_t alignment, size_t size)
{
	void *ptr = je_aligned_alloc(alignment, size + (size_t)((PREFIX_SIZE + alignment - 1) & ~(alignment - 1)));
	if (!ptr)
		malloc_oom(size);
	return fill_prefix(ptr);
}

int skynet_posix_memalign(void **memptr, size_t alignment, size_t size)
{
	int err = je_posix_memalign(memptr, alignment, size + PREFIX_SIZE);
	if (err)
		malloc_oom(size);
	fill_prefix(*memptr);
	return err;
}

#else

// for skynet_lalloc use
#define raw_realloc realloc
#define raw_free free

void memory_info_dump(void)
{
	skynet_error(NULL, "No jemalloc");
}

size_t
mallctl_int64(const char *name, size_t *newval)
{
	skynet_error(NULL, "No jemalloc : mallctl_int64 %s.", name);
	return 0;
}

int mallctl_opt(const char *name, int *newval)
{
	skynet_error(NULL, "No jemalloc : mallctl_opt %s.", name);
	return 0;
}

bool mallctl_bool(const char *name, bool *newval)
{
	skynet_error(NULL, "No jemalloc : mallctl_bool %s.", name);
	return 0;
}

int mallctl_cmd(const char *name)
{
	skynet_error(NULL, "No jemalloc : mallctl_cmd %s.", name);
	return 0;
}

#endif

size_t
malloc_used_memory(void)
{
	return _used_memory;
}

size_t
malloc_memory_block(void)
{
	return _memory_block;
}

void dump_c_mem()
{
	int i;
	size_t total = 0;
	skynet_error(NULL, "dump all service mem:");
	for (i = 0; i < SLOT_SIZE; i++)
	{
		struct mem_data *data = &mem_stats[i];
		if (data->handle != 0 && data->allocated != 0)
		{
			total += data->allocated;
			skynet_error(NULL, ":%08x -> %zdkb %db", data->handle, data->allocated >> 10, (int)(data->allocated % 1024)); // %z for ssize_t
		}
	}
	skynet_error(NULL, "+total: %zdkb", total >> 10); // 这个 total 应该等于 _used_memory 才对
}

/**
 * 字符串常量放到堆里
*/
char *
skynet_strdup(const char *str)
{
	size_t sz = strlen(str);
	char *ret = skynet_malloc(sz + 1);
	memcpy(ret, str, sz + 1);
	return ret;
}

/**
 * 原生的 realloc 分配，不走内存统计（skynet_realloc）
 * 这个接口提供给 lua虚拟机 的内存分配器 lua_newstate(lalloc, l); 见 service_snlua.c
 * lua内存分配执行流程为：
 * lalloc （内存限额、报警）-> skynet_lalloc
 * 
 * 所以，lua内存的统计由上层函数 lalloc 完成
*/
void *
skynet_lalloc(void *ptr, size_t osize, size_t nsize)
{
	if (nsize == 0)
	{
		raw_free(ptr);
		return NULL;
	}
	else
	{
		return raw_realloc(ptr, nsize);
	}
}

/**
 * 做个 table
 * {
 * 	[data->handle] = data->allocated
 * }
*/
int dump_mem_lua(lua_State *L)
{
	int i;
	lua_newtable(L);
	for (i = 0; i < SLOT_SIZE; i++)
	{
		struct mem_data *data = &mem_stats[i];
		if (data->handle != 0 && data->allocated != 0)
		{
			lua_pushinteger(L, data->allocated);
			lua_rawseti(L, -2, (lua_Integer)data->handle);
		}
	}
	return 1;
}

/**
 * 函数执行都是在消息循环里
 * 在处理消息前通过TLS记录下了对应服务的handle
 * 这里通过 skynet_current_handle 取出
*/
size_t
malloc_current_memory(void)
{
	uint32_t handle = skynet_current_handle();
	int i;
	for (i = 0; i < SLOT_SIZE; i++)
	{
		struct mem_data *data = &mem_stats[i];
		if (data->handle == (uint32_t)handle && data->allocated != 0)
		{
			return (size_t)data->allocated;
		}
	}
	return 0;
}

void skynet_debug_memory(const char *info)
{
	// for debug use
	uint32_t handle = skynet_current_handle();
	size_t mem = malloc_current_memory();
	fprintf(stderr, "[:%08x] %s %p\n", handle, info, (void *)mem);
}
