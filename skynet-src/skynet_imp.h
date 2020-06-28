#ifndef SKYNET_IMP_H
#define SKYNET_IMP_H

struct skynet_config {
	int thread;
	int harbor;
	int profile;
	const char * daemon;
	const char * module_path;
	const char * bootstrap;
	const char * logger;
	const char * logservice;
};

// 线程分类，私有数据根据该值与key关联
#define THREAD_WORKER 0			// 可知，多个woker线程的私有数据是共享的
#define THREAD_MAIN 1
#define THREAD_SOCKET 2
#define THREAD_TIMER 3
#define THREAD_MONITOR 4

void skynet_start(struct skynet_config * config);

#endif
