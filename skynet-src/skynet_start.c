#include "skynet.h"
#include "skynet_server.h"
#include "skynet_imp.h"
#include "skynet_mq.h"
#include "skynet_handle.h"
#include "skynet_module.h"
#include "skynet_timer.h"
#include "skynet_monitor.h"
#include "skynet_socket.h"
#include "skynet_daemon.h"
#include "skynet_harbor.h"

#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

//////////////////////////////////// 工作线程监视器管理器
struct monitor {
	int count;					  // 工作线程的数量
	struct skynet_monitor ** m;	  // 监视器列表
	pthread_cond_t cond;
	pthread_mutex_t mutex;
	int sleep;					  // 休眠中的工作线程数量
	int quit;					  // 退出标记
};
////////////////////////////////////

struct worker_parm {
	struct monitor *m;			// 工作线程监视器管理器
	int id;						// 工作线程序号
	int weight;					// 权重
};

static volatile int SIG = 0;

/**
 * 响应信号SIGHUP，将日志文件重新打开
 * 0. skynet_start：注册信号SIGHUP响应函数
 * 1. handle_hup：设置标记SIG
 * 2. thread_timer -> signal_hup：计时器线程主循环，发送 PTYPE_SYSTEM 消息给logger服务
 * 3. logger_cb -> freopen：见 service_logger.c
 * 
 * 想实现这个目的不一定要动信号响应机制
 * 可以写cmd命令完成此功能
*/
static void
handle_hup(int signal) {
	if (signal == SIGHUP) {
		SIG = 1;
	}
}

#define CHECK_ABORT if (skynet_context_total()==0) break;

static void
create_thread(pthread_t *thread, void *(*start_routine) (void *), void *arg) {
	if (pthread_create(thread,NULL, start_routine, arg)) {
		fprintf(stderr, "Create thread failed");
		exit(1);
	}
}

/**
 * busy是个工作线程活动数量的容忍值
 * 即目前正在运转的工作线程必须大于这个值
 * 否则，通过条件变量把正在休眠的工作线程唤醒
 * 
*/
static void
wakeup(struct monitor *m, int busy) {
	if (m->sleep >= m->count - busy) {
		// signal sleep worker, "spurious wakeup" is harmless
		// 唤醒一个休眠中的工作线程
		pthread_cond_signal(&m->cond);
	}
}

static void *
thread_socket(void *p) {
	struct monitor * m = p;
	skynet_initthread(THREAD_SOCKET);
	for (;;) {
		int r = skynet_socket_poll();
		if (r==0)
			break;
		if (r<0) {
			CHECK_ABORT
			continue;
		}
		wakeup(m,0);
	}
	return NULL;
}

static void
free_monitor(struct monitor *m) {
	int i;
	int n = m->count;
	for (i=0;i<n;i++) {
		skynet_monitor_delete(m->m[i]);
	}
	pthread_mutex_destroy(&m->mutex);
	pthread_cond_destroy(&m->cond);
	skynet_free(m->m);
	skynet_free(m);
}

static void *
thread_monitor(void *p) {
	struct monitor * m = p;
	int i;
	int n = m->count;
	skynet_initthread(THREAD_MONITOR);
	for (;;) {
		CHECK_ABORT
		for (i=0;i<n;i++) {
			skynet_monitor_check(m->m[i]);
		}
		// 监视器每休眠5秒，轮询一遍woker线程
		for (i=0;i<5;i++) {
			CHECK_ABORT
			sleep(1);
		}
	}

	return NULL;
}

static void
signal_hup() {
	// make log file reopen

	struct skynet_message smsg;
	smsg.source = 0;
	smsg.session = 0;
	smsg.data = NULL;
	smsg.sz = (size_t)PTYPE_SYSTEM << MESSAGE_TYPE_SHIFT;
	uint32_t logger = skynet_handle_findname("logger");
	if (logger) {
		skynet_context_push(logger, &smsg);
	}
}

/**
 * 计时器线程启动例程函数
*/
static void *
thread_timer(void *p) {
	struct monitor * m = p;
	skynet_initthread(THREAD_TIMER);
	for (;;) {
		skynet_updatetime();
		skynet_socket_updatetime();
		CHECK_ABORT
		wakeup(m,m->count-1);			// 确保工作线程满负荷运行
		usleep(2500);
		if (SIG) {
			signal_hup();
			SIG = 0;
		}
	}
	// wakeup socket thread
	skynet_socket_exit();
	// wakeup all worker thread
	pthread_mutex_lock(&m->mutex);
	m->quit = 1;
	pthread_cond_broadcast(&m->cond);	// 唤醒所有休眠中的工作线程，告诉他们进程要退出了（下班了）
	pthread_mutex_unlock(&m->mutex);
	return NULL;
}

/**
 * 工作线程启动例程函数
*/
static void *
thread_worker(void *p) {
	struct worker_parm *wp = p;
	int id = wp->id;						// 序号
	int weight = wp->weight;				// 权重
	struct monitor *m = wp->m;				// 监视管理器
	struct skynet_monitor *sm = m->m[id];	// 监视器
	skynet_initthread(THREAD_WORKER);
	struct message_queue * q = NULL;

	// 主循环
	while (!m->quit) {
		q = skynet_context_message_dispatch(sm, q, weight);
		if (q == NULL) {
			// 全局消息队列是空的，工作线程将所有消息都处理完后会变成这样
			if (pthread_mutex_lock(&m->mutex) == 0) {
				++ m->sleep;						// 工作线程：没有工作单了么？那我睡一会
				// "spurious wakeup" is harmless,
				// because skynet_context_message_dispatch() can be call at any time.
				if (!m->quit)
					pthread_cond_wait(&m->cond, &m->mutex);
				-- m->sleep;
				if (pthread_mutex_unlock(&m->mutex)) {
					fprintf(stderr, "unlock mutex error");
					exit(1);
				}
			}
		}
	}
	return NULL;
}

/**
 * 线程模型在这里初始化
*/
static void
start(int thread) {
	pthread_t pid[thread+3];

	////////////////////////////////////////// 监视器管理器，线程模型成功初始化销毁
	struct monitor *m = skynet_malloc(sizeof(*m));
	memset(m, 0, sizeof(*m));
	m->count = thread;
	m->sleep = 0;

	m->m = skynet_malloc(thread * sizeof(struct skynet_monitor *));
	int i;
	for (i=0;i<thread;i++) {
		m->m[i] = skynet_monitor_new();		// 有多少个工作线程就创建多少个监视器
	}
	if (pthread_mutex_init(&m->mutex, NULL)) {
		fprintf(stderr, "Init mutex error");
		exit(1);
	}
	if (pthread_cond_init(&m->cond, NULL)) {
		fprintf(stderr, "Init cond error");
		exit(1);
	}

	// 为什么要thread+3，因为0/1/2有特殊用途
	// thread仅代表woker线程数量
	create_thread(&pid[0], thread_monitor, m);			// 监视器
	create_thread(&pid[1], thread_timer, m);			// 计时器
	create_thread(&pid[2], thread_socket, m);			// 网络

	/**
	 * 权重分 5 级：代表消息循环的每一步（从全局消息队列pop，再到push之间）处理的消息数量n与消息队列有效长度length的关系
	 * -1  n=1
	 *  0  n=length
	 *  1  n=length/2
	 *  2  n=length/4
	 *  3  n=length/8
	*/
	static int weight[] = { 
		-1, -1, -1, -1, 0, 0, 0, 0,
		1, 1, 1, 1, 1, 1, 1, 1, 
		2, 2, 2, 2, 2, 2, 2, 2, 
		3, 3, 3, 3, 3, 3, 3, 3, };
	struct worker_parm wp[thread];						// 工作线程启动例程的参数：监视管理器，序号，权重
	for (i=0;i<thread;i++) {
		wp[i].m = m;
		wp[i].id = i;
		if (i < sizeof(weight)/sizeof(weight[0])) {
			wp[i].weight= weight[i];
		} else {
			wp[i].weight = 0;
		}
		create_thread(&pid[i+3], thread_worker, &wp[i]);
	}

	// 阻塞主进程，等待这几个线程返回
	// 每个worker线程都有个主循环，所以在这一刻，整个线程模型开始工作了
	for (i=0;i<thread+3;i++) {
		pthread_join(pid[i], NULL); 
	}

	free_monitor(m);
}

static void
bootstrap(struct skynet_context * logger, const char * cmdline) {
	int sz = strlen(cmdline);
	char name[sz+1];
	char args[sz+1];
	sscanf(cmdline, "%s %s", name, args);
	struct skynet_context *ctx = skynet_context_new(name, args);
	if (ctx == NULL) {
		skynet_error(NULL, "Bootstrap error : %s\n", cmdline);
		skynet_context_dispatchall(logger);
		exit(1);
	}
}

void 
skynet_start(struct skynet_config * config) {
	// register SIGHUP for log file reopen
	// 利用信号SIGHUP将LOG文件重新打开
	struct sigaction sa;
	sa.sa_handler = &handle_hup;
	sa.sa_flags = SA_RESTART;
	sigfillset(&sa.sa_mask);
	sigaction(SIGHUP, &sa, NULL);

	// config->daemon是个文件名
	// 创建一个守护进程会将进程pid写入到该文件中
	// 即一个守护进程文件名对应一个守护进程
	if (config->daemon) {
		if (daemon_init(config->daemon)) {
			exit(1);
		}
	}

	// 服务器ID
	skynet_harbor_init(config->harbor);
	// 全局服务实例句柄存储
	skynet_handle_init(config->harbor);
	// 全局消息队列
	skynet_mq_init();
	// 模块管理器
	skynet_module_init(config->module_path);
	// 计时器
	skynet_timer_init();
	// 网络
	skynet_socket_init();
	// 分析器开关
	skynet_profile_enable(config->profile);

	/////////////////////////////////////// 第一个服务实例：日志系统，别名logger
	/////////////////////////////////////// config->logservice 服务名称(logger)
	/////////////////////////////////////// config->logger 日志文件名
	struct skynet_context *ctx = skynet_context_new(config->logservice, config->logger);
	if (ctx == NULL) {
		fprintf(stderr, "Can't launch %s service\n", config->logservice);
		exit(1);
	}

	skynet_handle_namehandle(skynet_context_handle(ctx), "logger");
	///////////////////////////////////////

	/////////////////////////////////////// 第二个服务实例配置在bootstrap中，通常为snlua bootstrap（模块名 参数）
	/////////////////////////////////////// 由Makefile获知：
	/////////////////////////////////////// cservice/xxx.so 的源文件为 service-src/service_xxx.c
	/////////////////////////////////////// bootstrap 对应 service/bootstrap.lua
	bootstrap(ctx, config->bootstrap);   // 这里将logger服务传入，只是为了做异常处理

	start(config->thread);				 // 按数量启动线程，主逻辑

	// harbor_exit may call socket send, so it should exit before socket_free
	skynet_harbor_exit();
	skynet_socket_free();
	if (config->daemon) {
		daemon_exit(config->daemon);
	}
}
