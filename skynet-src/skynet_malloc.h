#ifndef skynet_malloc_h
#define skynet_malloc_h

#include <stddef.h>

#define skynet_malloc malloc
#define skynet_calloc calloc
#define skynet_realloc realloc
#define skynet_free free
#define skynet_memalign memalign
#define skynet_aligned_alloc aligned_alloc
#define skynet_posix_memalign posix_memalign

// 经过一番宏定义，下面的函数在编译时都会改名
// 如果定义了 NOUSE_JEMALLOC，这些符号就只有声明，没有定义（弱符号），最终被 glibc 中的符号替换（全局符号介入 Global Symbol Interpose）
// 如果没有定义 NOUSE_JEMALLOC，这些符号就是强符号（有定义）

void * skynet_malloc(size_t sz);
void * skynet_calloc(size_t nmemb,size_t size);
void * skynet_realloc(void *ptr, size_t size);
void skynet_free(void *ptr);
char * skynet_strdup(const char *str);
void * skynet_lalloc(void *ptr, size_t osize, size_t nsize);	// use for lua
void * skynet_memalign(size_t alignment, size_t size);
void * skynet_aligned_alloc(size_t alignment, size_t size);
int skynet_posix_memalign(void **memptr, size_t alignment, size_t size);

#endif
