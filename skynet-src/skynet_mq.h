#ifndef SKYNET_MESSAGE_QUEUE_H
#define SKYNET_MESSAGE_QUEUE_H

#include <stdlib.h>
#include <stdint.h>

/**
 * 消息的结构
*/
struct skynet_message {
	uint32_t source;		// 源：发送消息的服务句柄	      4
	int session;			// 消息的唯一标识				 int
	void * data;			// 数据							pointer
	size_t sz;				// 数据大小						size_t
};

// type is encoding in skynet_message.sz high 8bit
#define MESSAGE_TYPE_SHIFT ((sizeof(size_t)-1) * 8)			// 用size_t的最高位的字节表示消息类型（MESSAGE_TYPE）
#define MESSAGE_TYPE_MASK (SIZE_MAX >> 8)					// 所以的消息最大尺寸少了8位

struct message_queue;

void skynet_globalmq_push(struct message_queue * queue);
struct message_queue * skynet_globalmq_pop(void);

struct message_queue * skynet_mq_create(uint32_t handle);
void skynet_mq_mark_release(struct message_queue *q);

typedef void (*message_drop)(struct skynet_message *, void *);

void skynet_mq_release(struct message_queue *q, message_drop drop_func, void *ud);
uint32_t skynet_mq_handle(struct message_queue *);

// 0 for success
int skynet_mq_pop(struct message_queue *q, struct skynet_message *message);
void skynet_mq_push(struct message_queue *q, struct skynet_message *message);

// return the length of message queue, for debug
int skynet_mq_length(struct message_queue *q);
int skynet_mq_overload(struct message_queue *q);

void skynet_mq_init();

#endif
