#include "skynet.h"

#include "skynet_module.h"
#include "spinlock.h"

#include <assert.h>
#include <string.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_MODULE_TYPE 32

////////////////////////////////////////////////// 全局模块列表
struct modules {
	int count;									// 模块数量
	struct spinlock lock;						// 自旋锁
	const char * path;							// 由skynet配置表中的cpath指定，一般为./cservice/?.so路径
	struct skynet_module m[MAX_MODULE_TYPE]; 	// 存放模块的数组
};

static struct modules * M = NULL;
//////////////////////////////////////////////////

/**
 * 将path路径./cservice/?.so中的?替换成name
 * 然后通过动态装载器加载到内存
*/
static void *
_try_open(struct modules *m, const char * name) {
	const char *l;
	const char * path = m->path;
	size_t path_size = strlen(path);
	size_t name_size = strlen(name);

	int sz = path_size + name_size;
	//search path
	void * dl = NULL;
	char tmp[sz];
	do
	{
		memset(tmp,0,sz);
		while (*path == ';') path++;
		if (*path == '\0') break;
		l = strchr(path, ';');
		if (l == NULL) l = path + strlen(path);
		int len = l - path;
		int i;
		for (i=0;path[i]!='?' && i < len ;i++) {
			tmp[i] = path[i];
		}
		memcpy(tmp+i,name,name_size);
		if (path[i] == '?') {
			strncpy(tmp+i+name_size,path+i+1,len - i - 1);
		} else {
			fprintf(stderr,"Invalid C service path\n");
			exit(1);
		}
		dl = dlopen(tmp, RTLD_NOW | RTLD_GLOBAL);
		path = l;
	}while(dl == NULL);

	if (dl == NULL) {
		fprintf(stderr, "try open %s failed : %s\n",name,dlerror());
	}

	return dl;
}

// 通过名称，查找动态库
static struct skynet_module * 
_query(const char * name) {
	int i;
	for (i=0;i<M->count;i++) {
		if (strcmp(M->m[i].name,name)==0) {
			return &M->m[i];
		}
	}
	return NULL;
}

// 通过导出符号，从动态库找到接口
static void *
get_api(struct skynet_module *mod, const char *api_name) {
	size_t name_size = strlen(mod->name);
	size_t api_size = strlen(api_name);
	char tmp[name_size + api_size + 1];

	//接口名一般为 mod->name + api_name的拼接
	memcpy(tmp, mod->name, name_size);
	memcpy(tmp+name_size, api_name, api_size+1);

	char *ptr = strrchr(tmp, '.');
	if (ptr == NULL) {
		ptr = tmp;
	} else {
		ptr = ptr + 1;
	}
	return dlsym(mod->module, ptr);
}

// 将四个重要的导出函数地址保存在模块信息中
static int
open_sym(struct skynet_module *mod) {
	mod->create = get_api(mod, "_create");
	mod->init = get_api(mod, "_init");
	mod->release = get_api(mod, "_release");
	mod->signal = get_api(mod, "_signal");

	return mod->init == NULL;
}

/**
 * 通过name查找对应的模块（so库）
 * 如果找不到，则dlopen动态装载，并记录到M列表中。
 */ 
struct skynet_module * 
skynet_module_query(const char * name) {
	struct skynet_module * result = _query(name);
	if (result)
		return result;

	SPIN_LOCK(M)

	result = _query(name); // double check

	if (result == NULL && M->count < MAX_MODULE_TYPE) {
		int index = M->count;
		void * dl = _try_open(M,name);
		if (dl) {
			M->m[index].name = name;
			M->m[index].module = dl;

			if (open_sym(&M->m[index]) == 0) {
				M->m[index].name = skynet_strdup(name);
				M->count ++;
				result = &M->m[index];
			}
		}
	}

	SPIN_UNLOCK(M)

	return result;
}

/**
 * C语言库，编译成so。
 * 在调用该库的某个API句柄时，会被加载到modules列表中
*/
void 
skynet_module_insert(struct skynet_module *mod) {
	SPIN_LOCK(M)

	struct skynet_module * m = _query(mod->name);
	assert(m == NULL && M->count < MAX_MODULE_TYPE);
	int index = M->count;
	M->m[index] = *mod;
	++M->count;

	SPIN_UNLOCK(M)
}

// 参考skynet_context_new接口，确定各函数触发时机

// _create函数：服务实例创建前触发
// 返回值：若是有效地址才创建服务实例
void * 
skynet_module_instance_create(struct skynet_module *m) {
	if (m->create) {
		return m->create();
	} else {
		return (void *)(intptr_t)(~0);
	}
}

// _init函数：服务实例创建成功后：注册到全剧服务实例存储、初始化消息队列且消息队列还未加入到全局消息队列
// 返回值：有效值0
// 通常在init函数中指定callback函数（skynet_callback）
int
skynet_module_instance_init(struct skynet_module *m, void * inst, struct skynet_context *ctx, const char * parm) {
	return m->init(inst, ctx, parm); // 模块实例、服务实例、参数
}

// _release函数：服务实例下线时触发（delete_context）
void 
skynet_module_instance_release(struct skynet_module *m, void *inst) {
	if (m->release) {
		m->release(inst);
	}
}

// _signal函数：cmd_signal触发
void
skynet_module_instance_signal(struct skynet_module *m, void *inst, int signal) {
	if (m->signal) {
		m->signal(inst, signal);
	}
}

void 
skynet_module_init(const char *path) {
	struct modules *m = skynet_malloc(sizeof(*m));
	m->count = 0;
	m->path = skynet_strdup(path);

	SPIN_INIT(m)

	M = m;
}
