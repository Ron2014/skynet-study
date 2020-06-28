#include "skynet.h"

#include "skynet_monitor.h"
#include "skynet_server.h"
#include "skynet.h"
#include "atomic.h"

#include <stdlib.h>
#include <string.h>

struct skynet_monitor {
	int version;			// 累计值。每处理一个消息，对应的监视器version就会更新。
	int check_version;		// 记录值。两个version若相等，表示处理一个消息的耗时超过了monitor的sleep时间，视为拥堵。
	uint32_t source;
	uint32_t destination;
};

struct skynet_monitor * 
skynet_monitor_new() {
	struct skynet_monitor * ret = skynet_malloc(sizeof(*ret));
	memset(ret, 0, sizeof(*ret));
	return ret;
}

void 
skynet_monitor_delete(struct skynet_monitor *sm) {
	skynet_free(sm);
}

/**
 * 监视器获取当前消息的服务源（句柄）和处理该消息的服务（句柄）
*/
void 
skynet_monitor_trigger(struct skynet_monitor *sm, uint32_t source, uint32_t destination) {
	sm->source = source;
	sm->destination = destination;
	ATOM_INC(&sm->version);
}

/**
 * 如果version更新，表示休眠后处理的新消息。
 * 若果未更新，表示休眠期间该服务一直在处理同一个消息，即耗时>5秒。此时需要报警
*/
void 
skynet_monitor_check(struct skynet_monitor *sm) {
	if (sm->version == sm->check_version) {
		if (sm->destination) {
			skynet_context_endless(sm->destination);
			skynet_error(NULL, "A message from [ :%08x ] to [ :%08x ] maybe in an endless loop (version = %d)", sm->source , sm->destination, sm->version);
		}
	} else {
		sm->check_version = sm->version;
	}
}
