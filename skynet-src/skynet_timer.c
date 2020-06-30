#include "skynet.h"

#include "skynet_timer.h"
#include "skynet_mq.h"
#include "skynet_server.h"
#include "skynet_handle.h"
#include "spinlock.h"

#include <time.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#if defined(__APPLE__)
#include <AvailabilityMacros.h>
#include <sys/time.h>
#include <mach/task.h>
#include <mach/mach.h>
#endif

typedef void (*timer_execute_func)(void *ud,void *arg);

/**
 * 时间轮算法参考 : Linux 时钟处理机制
 * https://www.ibm.com/developerworks/cn/linux/l-cn-clocks/index.html
*/
#define TIME_NEAR_SHIFT 8
#define TIME_NEAR (1 << TIME_NEAR_SHIFT)		// 256		0001 0000 0000B
#define TIME_LEVEL_SHIFT 6
#define TIME_LEVEL (1 << TIME_LEVEL_SHIFT)		// 64		0100 0000B
#define TIME_NEAR_MASK (TIME_NEAR-1)			// 255		1111 1111B
#define TIME_LEVEL_MASK (TIME_LEVEL-1)			// 63		0011 1111B

struct timer_event {
	uint32_t handle;
	int session;
};

struct timer_node {
	struct timer_node *next;
	uint32_t expire;
};

struct link_list {
	struct timer_node head;
	struct timer_node *tail;
};

////////////////////////////////////// 计时器
// 1秒 = 100厘秒
// 1厘秒 = 10毫秒
struct timer {
	struct link_list near[TIME_NEAR];		// 256 个链表
	struct link_list t[4][TIME_LEVEL];
	struct spinlock lock;
	uint32_t time;							// timer_update 累计执行的次数。通常为系统流逝的厘秒数，即与current有关
	uint32_t starttime;						// 系统启动时间。单位：秒
	uint64_t current;						// 系统运行时间（累计值）。单位：厘秒
	uint64_t current_point;					// 当前时间。单位：厘秒
};

static struct timer * TI = NULL;
//////////////////////////////////////

/**
 * --------------------<-|
 *     head   [next]=0   |
 *            expire     |
 * ---[tail]-------------|
*/
static inline struct timer_node *
link_clear(struct link_list *list) {
	struct timer_node * ret = list->head.next;
	list->head.next = 0;
	list->tail = &(list->head);

	return ret;
}

/**
 * link_list                node
 * ----------------------|->---------------------
 *     head   [next]-----|    head   [next]=0   |
 *            expire     |           expire     |
 * ---[tail]-------------|---[tail]--------------
*/
static inline void
link(struct link_list *list,struct timer_node *node) {
	list->tail->next = node;
	list->tail = node;
	node->next=0;
}

/**
 * 通过
 * node 的终止时间
 * T 的当前时间
 * 将新 node 加到缓存中
*/
static void
add_node(struct timer *T,struct timer_node *node) {
	// 都是4字节
	uint32_t time = node->expire;
	uint32_t current_time = T->time;
	
	if ((time|TIME_NEAR_MASK)==(current_time|TIME_NEAR_MASK)) {
		// 高位3字节都相同, 也就是最多低位1字节不同
		// 就表示是个近时间, 最多需要 tick 255下就会走到的
		link(&T->near[time&TIME_NEAR_MASK],node);

	} else {
		int i;
		// 高位3字节, 共24位, 最多左移6位3次.
		uint32_t mask = TIME_NEAR << TIME_LEVEL_SHIFT; 
		//                                0100 0000   0000 0000B
		//                    0001 0000   0000 0000   0000 0000B
		//             0100   0000 0000   0000 0000   0000 0000B
		// 0001   0000 0000   0000 0000   0000 0000   0000 0000B
		//
		// 对于时间的分段
		//        3333 3322   2222 1111   1100 0000   nnnn nnnnB
		// 近时间 nnnn nnnn
		// 远时间 分成了4个象限, 每个象限64个节点
		for (i=0;i<3;i++) {
			if ((time|(mask-1))==(current_time|(mask-1))) {
				break;
			}
			mask <<= TIME_LEVEL_SHIFT;
		}
		// i = 0, 1, 2, 3
		// 如果没有 break, i=3
		// i = 0
		//        3333 3322   2222 1111   11XX XXXX   nnnn nnnnB
		// i = 1
		//        3333 3322   2222 XXXX   XX00 0000   nnnn nnnnB
		// i = 2
		//        3333 33XX   XXXX 1111   1100 0000   nnnn nnnnB
		// i = 3
		//        XXXX XX22   2222 1111   1100 0000   nnnn nnnnB
		link(&T->t[i][((time>>(TIME_NEAR_SHIFT + i*TIME_LEVEL_SHIFT)) & TIME_LEVEL_MASK)],node);
	}
}

// sz 就是 arg 的内存长度
// time 指的是 delay time 延迟时间
static void
timer_add(struct timer *T,void *arg,size_t sz,int time) {
	struct timer_node *node = (struct timer_node *)skynet_malloc(sizeof(*node)+sz);
	memcpy(node+1,arg,sz);		// 这直接就+1了, 将arg的数据放到timer_node的尾部

	SPIN_LOCK(T);

		node->expire=time+T->time;
		add_node(T,node);

	SPIN_UNLOCK(T);
}

static void
move_list(struct timer *T, int level, int idx) {
	struct timer_node *current = link_clear(&T->t[level][idx]);
	while (current) {
		struct timer_node *temp=current->next;
		add_node(T,current);
		current=temp;
	}
}

/**
 * 一个32位无符号整型的重置
 * 至少也要 
 * 2^32 / 100 / 60 / 60 / 24 = 497.1026962962962962962962962963 .... 天
*/

static void
timer_shift(struct timer *T) {
	int mask = TIME_NEAR;
	uint32_t ct = ++T->time;
	if (ct == 0) {
/**
 * 超时设置时传入的time参数为int类型，且负数时直接返回，只有time是正数时才会添加到timer中
 * T->time最高位为0时，expiretime不会溢出。
 * 当T->time最高位为1时，'expiretime'可能溢出，但溢出后最高位肯定为0，
 * 所以这两个时间最高位肯定不相等，会被加入到T->t[3]中，ct==0时, 重新添加T->t[3][0]的节点
 * t[3][i]的节点也会在之后更新T->time时得到处理。所以没有问题。
*/
		// time 重置了!
		// T->t[3][0] 需要重排
		// i = 3
		//        0000 0022   2222 1111   1100 0000   nnnn nnnnB
		move_list(T, 3, 0);
	} else {
		// ct : current time 当前时间, 如果是整点状态才会满足while 如
		//        XXXX XXXX   XXXX XXXX   XXTT TTTT   0000 0000
		//        XXXX XXXX   XXXX TTTT   TT00 0000   0000 0000
		//        XXXX XXTT   TTTT 0000   0000 0000   0000 0000
		//        TTTT TT00   0000 0000   0000 0000   0000 0000
		// time: 
		//        3333 3322   2222 1111   11XX XXXX
		//        3333 3322   2222 XXXX   XX
		//        3333 33XX   XXXX
		//        XXXX XX
		// 从 time 能确定 TTTTTT 的值, 也就是 idx, 如果为 0 表示还可以进入下一个while, 不为零 则表示这一处的时间需要重排
		// mask:
		//        3333 3322   2222 1111   1100 0000   XXXX XXXX
		//        3333 3322   2222 1111   11XX XXXX   XXXX XXXX
		//        3333 3322   2222 XXXX   XXXX XXXX   XXXX XXXX
		//        3333 33XX   XXXX XXXX   XXXX XXXX   XXXX XXXX
		//        XXXX XXXX   XXXX XXXX   XXXX XXXX   XXXX XXXX
		uint32_t time = ct >> TIME_NEAR_SHIFT;
		int i=0;

		while ((ct & (mask-1))==0) {
			int idx = time & TIME_LEVEL_MASK;
			if (idx != 0) {
				move_list(T, i, idx);
				break;				
			}
			mask <<= TIME_LEVEL_SHIFT;
			time >>= TIME_LEVEL_SHIFT;
			++i;
		}
	}
}

static inline void
dispatch_list(struct timer_node *current) {
	do {
		struct timer_event * event = (struct timer_event *)(current+1);
		struct skynet_message message;
		message.source = 0;
		message.session = event->session;
		message.data = NULL;
		message.sz = (size_t)PTYPE_RESPONSE << MESSAGE_TYPE_SHIFT;

		// 向注册定时器的服务发 RESPONSE 消息
		skynet_context_push(event->handle, &message);
		
		struct timer_node * temp = current;
		current=current->next;
		skynet_free(temp);	
	} while (current);
}

static inline void
timer_execute(struct timer *T) {
	int idx = T->time & TIME_NEAR_MASK;
	
	while (T->near[idx].head.next) {
		// 直接从近时间链表中取了一条链下来
		struct timer_node *current = link_clear(&T->near[idx]);
		SPIN_UNLOCK(T);
		// dispatch_list don't need lock T
		dispatch_list(current);
		SPIN_LOCK(T);
	}
}

static void 
timer_update(struct timer *T) {
	SPIN_LOCK(T);

	// try to dispatch timeout 0 (rare condition)
	timer_execute(T);		// 处理 timeout 0 的情况, 虽然这里说的是低概率, 但是 manager.lua 就干这种事情啊

	// shift time first, and then dispatch timer message
	timer_shift(T);

	timer_execute(T);

	SPIN_UNLOCK(T);
}

static struct timer *
timer_create_timer() {
	struct timer *r=(struct timer *)skynet_malloc(sizeof(struct timer));
	memset(r,0,sizeof(*r));

	int i,j;

	for (i=0;i<TIME_NEAR;i++) {
		link_clear(&r->near[i]);
	}

	for (i=0;i<4;i++) {
		for (j=0;j<TIME_LEVEL;j++) {
			link_clear(&r->t[i][j]);
		}
	}

	SPIN_INIT(r)

	r->current = 0;

	return r;
}

int
skynet_timeout(uint32_t handle, int time, int session) {
	if (time <= 0) {
		struct skynet_message message;
		message.source = 0;
		message.session = session;
		message.data = NULL;
		message.sz = (size_t)PTYPE_RESPONSE << MESSAGE_TYPE_SHIFT;

		if (skynet_context_push(handle, &message)) {
			return -1;
		}
	} else {
		struct timer_event event;
		event.handle = handle;
		event.session = session;
		/**
		 * 一个 timer_node 一共存了三个数据
		 * handle 	服务实例ID
		 * session 	消息ID
		 * expire 	终止时间
		*/
		timer_add(TI, &event, sizeof(event), time);
	}

	return session;
}

/**
 * 3处系统调用：
 * systime -> clock_gettime(CLOCK_REALTIME, &ti);						挂钟时间。即wall time，这个值可以人为改变
 * gettime -> clock_gettime(CLOCK_MONOTONIC, &ti);						单调时间。系统启动后流动的时间
 * skynet_thread_time -> clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ti); 	调用线程的CPU时间。
*/

// centisecond: 1/100 second
// 挂钟时间。返回秒，厘秒
static void
systime(uint32_t *sec, uint32_t *cs) {
#if !defined(__APPLE__) || defined(AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER)
	struct timespec ti;
	clock_gettime(CLOCK_REALTIME, &ti);
	*sec = (uint32_t)ti.tv_sec;
	*cs = (uint32_t)(ti.tv_nsec / 10000000);
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	*sec = tv.tv_sec;
	*cs = tv.tv_usec / 10000;
#endif
}

// 单调时间。返回一个值，单位：厘秒。
static uint64_t
gettime() {
	uint64_t t;
#if !defined(__APPLE__) || defined(AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER)
	struct timespec ti;
	clock_gettime(CLOCK_MONOTONIC, &ti);
	t = (uint64_t)ti.tv_sec * 100;
	t += ti.tv_nsec / 10000000;
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	t = (uint64_t)tv.tv_sec * 100;
	t += tv.tv_usec / 10000;
#endif
	return t;
}

void
skynet_updatetime(void) {
	uint64_t cp = gettime();
	if(cp < TI->current_point) {
		// 系统调用的值比记录的值要小，报个警
		skynet_error(NULL, "time diff error: change from %lld to %lld", cp, TI->current_point);
		TI->current_point = cp;
	} else if (cp != TI->current_point) {
		uint32_t diff = (uint32_t)(cp - TI->current_point);
		TI->current_point = cp;		// 记录单调时间
		TI->current += diff;		// 累计运行时间

		// 根据流逝的厘秒量，执行逻辑
		// current 先行
		// 后面 TI->time 跟上
		int i;
		for (i=0;i<diff;i++) {
			timer_update(TI);
		}
	}
}

/**
 * 系统启动时间点。单位：秒
*/
uint32_t
skynet_starttime(void) {
	return TI->starttime;
}

/**
 * 系统运行时间。累计值，单位：厘秒
 * 我们用64位存的厘秒, 关于它的重置时间, 要经过
 * 2^64 / 100 / 60 / 60 / 24 / 365 = 5849424173.5507203247082699137494 .... 年
 * 可以说几乎不会重置 (一开始的[0,99]误差也不会导致提前重置)
*/
uint64_t 
skynet_now(void) {
	return TI->current;
}

void 
skynet_timer_init(void) {
	TI = timer_create_timer();
	uint32_t current = 0;
	systime(&TI->starttime, &current);

	// 因为这是唯一系统调用 systime 取的厘秒, 它的值范围在 [0,99] 之间
	// 服务器启动时间 starttime 精度只算到秒
	// 所以理论上, 服务器一启动, 我们已经累计运行了 current 个厘秒.
	TI->current = current;					// 不太明白，这个累计运行时间为啥不从0开始算？？？
	TI->current_point = gettime();
}

// for profile，为了分析器统计CPU时间，需要的精度更高
// dispatch_message -> skynet_thread_time

#define NANOSEC 1000000000		// 纳秒
#define MICROSEC 1000000		// 微秒

// 调用线程CPU时间。返回一个值，单位：微秒
uint64_t
skynet_thread_time(void) {
#if  !defined(__APPLE__) || defined(AVAILABLE_MAC_OS_X_VERSION_10_12_AND_LATER)
	struct timespec ti;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ti);

	return (uint64_t)ti.tv_sec * MICROSEC + (uint64_t)ti.tv_nsec / (NANOSEC / MICROSEC);
#else
	struct task_thread_times_info aTaskInfo;
	mach_msg_type_number_t aTaskInfoCount = TASK_THREAD_TIMES_INFO_COUNT;
	if (KERN_SUCCESS != task_info(mach_task_self(), TASK_THREAD_TIMES_INFO, (task_info_t )&aTaskInfo, &aTaskInfoCount)) {
		return 0;
	}

	return (uint64_t)(aTaskInfo.user_time.seconds) + (uint64_t)aTaskInfo.user_time.microseconds;
#endif
}
