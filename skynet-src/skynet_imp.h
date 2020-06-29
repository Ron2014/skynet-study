#ifndef SKYNET_IMP_H
#define SKYNET_IMP_H

struct skynet_config {
	int thread;
	int harbor;
	int profile;
	const char * daemon;
	const char * module_path;			// cpath: 动态库存放目录 ./cservice/?.so
	const char * bootstrap;				// bootstrap: 自举命令 snlua bootstrap
	const char * logger;				// 日志服务的名称, 通常为 logger, 对应 cserver/logger.so
	const char * logservice;			// 日志文件名, 日志服务实例初始化时传入的参数, 默认打到stdout
};

// 线程分类，私有数据根据该值与key关联
#define THREAD_WORKER 0			// 可知，多个woker线程的私有数据是共享的
#define THREAD_MAIN 1
#define THREAD_SOCKET 2
#define THREAD_TIMER 3
#define THREAD_MONITOR 4

void skynet_start(struct skynet_config * config);

#endif
