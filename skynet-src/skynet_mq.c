#include "skynet.h"
#include "skynet_mq.h"
#include "skynet_handle.h"
#include "spinlock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#define DEFAULT_QUEUE_SIZE 64
#define MAX_GLOBAL_MQ 0x10000

// 0 means mq is not in global mq.
// 1 means mq is in global mq , or the message is dispatching.

#define MQ_IN_GLOBAL 1
#define MQ_OVERLOAD 1024

/**
 * 一个服务（Actor）对应的消息队列
*/
struct message_queue {
	struct spinlock lock;				// 自旋锁
	uint32_t handle;					// 所属的服务句柄
	int cap;							// 消息队列的容量(capacity)
	int head;
	int tail;
	int release;						// 释放标记
	int in_global;						// 属于全局消息队列的标记
	int overload;						// 消息超载标记
	int overload_threshold;				// 消息超载阈值
	struct skynet_message *queue;		// 次级消息队列，循环队列（head、tail假溢出操作）
	struct message_queue *next;
};

////////////////////////////////////////////////// 全局消息队列（一级消息队列）
/*****************************************************************************
 * global_queue -------------| head
 * 		|				message_queue => queue[]
 * 		|					 |
 * 		|				message_queue => queue[]
 * 		|					 |
 * 		|				message_queue => queue[]
 * 		|--------------------| tail
******************************************************************************/
struct global_queue {
	struct message_queue *head;				// 头结点，取出操作
	struct message_queue *tail;				// 尾结点，插入操作
	struct spinlock lock;
};

static struct global_queue *Q = NULL;
//////////////////////////////////////////////////

/**
 * 向全局消息队列尾部加入一个消息队列
*/
void 
skynet_globalmq_push(struct message_queue * queue) {
	struct global_queue *q = Q;

	SPIN_LOCK(q)
	assert(queue->next == NULL);
	if(q->tail) {
		q->tail->next = queue;
		q->tail = queue;
	} else {
		q->head = q->tail = queue;
	}
	SPIN_UNLOCK(q)
}


/**
 * 向全局消息队列头部取出一个消息队列
*/
struct message_queue * 
skynet_globalmq_pop() {
	struct global_queue *q = Q;

	SPIN_LOCK(q)
	struct message_queue *mq = q->head;
	if(mq) {
		q->head = mq->next;
		if(q->head == NULL) {
			assert(mq == q->tail);
			q->tail = NULL;
		}
		mq->next = NULL;
	}
	SPIN_UNLOCK(q)

	return mq;
}

/**
 * 为指定句柄的服务创建一个消息队列
*/
struct message_queue * 
skynet_mq_create(uint32_t handle) {
	struct message_queue *q = skynet_malloc(sizeof(*q));
	q->handle = handle;
	q->cap = DEFAULT_QUEUE_SIZE;
	q->head = 0;
	q->tail = 0;
	SPIN_INIT(q)
	// When the queue is create (always between service create and service init),
	// set in_global flag to avoid push it to global queue.
	// If the service init success, skynet_context_new will call skynet_mq_push to push it to global queue.
	q->in_global = MQ_IN_GLOBAL;
	q->release = 0;
	q->overload = 0;
	q->overload_threshold = MQ_OVERLOAD;
	q->queue = skynet_malloc(sizeof(struct skynet_message) * q->cap);
	q->next = NULL;

	return q;
}

static void 
_release(struct message_queue *q) {
	assert(q->next == NULL);
	SPIN_DESTROY(q)
	skynet_free(q->queue);
	skynet_free(q);
}

/**
 * 消息队列所属服务的句柄
*/
uint32_t 
skynet_mq_handle(struct message_queue *q) {
	return q->handle;
}

/**
 * 获取消息队列的有效长度
*/
int
skynet_mq_length(struct message_queue *q) {
	int head, tail,cap;

	SPIN_LOCK(q)
	head = q->head;
	tail = q->tail;
	cap = q->cap;
	SPIN_UNLOCK(q)
	
	if (head <= tail) {
		return tail - head;
	}
	return tail + cap - head;
}

/**
 * 获取消息队列超载标记，同时清空此标记
 * 可知超载只产生报警，不产生异常
*/
int
skynet_mq_overload(struct message_queue *q) {
	if (q->overload) {
		int overload = q->overload;
		q->overload = 0;
		return overload;
	} 
	return 0;
}

/**
 * 从次级消息队列中取出头部的消息
*/
int
skynet_mq_pop(struct message_queue *q, struct skynet_message *message) {
	int ret = 1;			// errno，异常标记，若成功pop，返回0
	SPIN_LOCK(q)

	if (q->head != q->tail) {
		*message = q->queue[q->head++];			// 内存拷贝
		ret = 0;
		int head = q->head;
		int tail = q->tail;
		int cap = q->cap;

		// 假溢出操作
		if (head >= cap) {
			q->head = head = 0;
		}

		// 计算队列有效数据长度，且考虑tail假溢出情况
		int length = tail - head;
		if (length < 0) {
			length += cap;
		}

		// 超载的话，打上标记，并扩大超载阈值
		// 为什么是pop消息的时候判断超载呢？？？
		while (length > q->overload_threshold) {
			q->overload = length;
			q->overload_threshold *= 2;
		}
	} else {
		// reset overload_threshold when queue is empty
		// 消息队列为空时重置超载阈值
		q->overload_threshold = MQ_OVERLOAD;
	}

	if (ret) {
		// 消息队列为空时，就不打算在消息循环中重新将该队列push进去了（降低消息循环的负载）
		// 而是做个标记，在产生新消息时，通过标记重新push到全局消息队列
		// 因为新消息的产生的流程：找到服务实例 -> 找到对应消息队列 -> skynet_mq_push
		// 和消息循环无关
		q->in_global = 0;
	}
	
	SPIN_UNLOCK(q)

	return ret;
}

/**
 * 扩展队列容量
 * 又是内存拷贝
*/
static void
expand_queue(struct message_queue *q) {
	struct skynet_message *new_queue = skynet_malloc(sizeof(struct skynet_message) * q->cap * 2);
	int i;
	for (i=0;i<q->cap;i++) {
		new_queue[i] = q->queue[(q->head + i) % q->cap];
	}
	q->head = 0;
	q->tail = q->cap;
	q->cap *= 2;
	
	skynet_free(q->queue);
	q->queue = new_queue;
}

/**
 * 将消息加入到次级消息队列的尾部
*/
void 
skynet_mq_push(struct message_queue *q, struct skynet_message *message) {
	assert(message);
	SPIN_LOCK(q)

	// 内存拷贝
	q->queue[q->tail] = *message;

	// 假溢出操作
	if (++ q->tail >= q->cap) {
		q->tail = 0;
	}

	// tail追上head，表示容量满了
	if (q->head == q->tail) {
		expand_queue(q);
	}

	// 由标记触发的重新入队全局消息队列
	// 标记的使用得了解调度逻辑
	if (q->in_global == 0) {
		q->in_global = MQ_IN_GLOBAL;
		skynet_globalmq_push(q);
	}
	
	SPIN_UNLOCK(q)
}

void 
skynet_mq_init() {
	struct global_queue *q = skynet_malloc(sizeof(*q));
	memset(q,0,sizeof(*q));
	SPIN_INIT(q);
	Q=q;
}

/**
 * 标记该次级消息队列是可销毁的
 * 在对应的服务实例下线（delete_context）时触发
 * 
 * 需要销毁的消息队列，必须保证它是在全局消息队列的
 * 因为销毁逻辑skynet_mq_release，仅在消息循环时触发skynet_context_message_dispatch
*/
void 
skynet_mq_mark_release(struct message_queue *q) {
	SPIN_LOCK(q)
	assert(q->release == 0);
	q->release = 1;
	if (q->in_global != MQ_IN_GLOBAL) {
		skynet_globalmq_push(q);
	}
	SPIN_UNLOCK(q)
}

static void
_drop_queue(struct message_queue *q, message_drop drop_func, void *ud) {
	struct skynet_message msg;
	while(!skynet_mq_pop(q, &msg)) {
		drop_func(&msg, ud);
	}
	_release(q);
}

/**
 * 销毁次级消息队列
 * 只有在标记了release才有效:
 * skynet_mq_release -> _drop_queue -> _release
 * 
 * _drop_queue：若队列中还有消息，依次取出进行drop_func操作
 * _release：释放内存
 * 
 * 服务实例的销毁和对应消息队列的销毁不是同时的，而是通过标记release的方式做了延迟操作
 * 在消息调度skynet_context_message_dispatch时，通过消息队列记录的服务句柄找不到对应的服务实例
 * 则会触发消息队列的销毁
*/
void 
skynet_mq_release(struct message_queue *q, message_drop drop_func, void *ud) {
	SPIN_LOCK(q)
	
	if (q->release) {
		SPIN_UNLOCK(q)
		_drop_queue(q, drop_func, ud);
	} else {
		skynet_globalmq_push(q);
		SPIN_UNLOCK(q)
	}
}
