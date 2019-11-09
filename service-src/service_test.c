#include "skynet.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// 为了保证打印有序，避免使用printf
// 统一使用skynet_error

struct test {
    int n;
};

struct test *
test_create(void) {
    struct test *inst = skynet_malloc(sizeof(*inst));
    inst->n = 0;
    return inst;
}

void
test_release(struct test *inst) {
    skynet_free(inst);
}

static int
test_cb(struct skynet_context * context, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
    struct test *inst = ud;
    const char *smsg = msg;
//  for (int i=0; i<sz; ++i)
//      printf("%c %02x;", smsg[i], smsg[i]);
//  printf("\n");
    switch(type) {
        case PTYPE_TEXT:
            skynet_error(context, "service [.test] message from [:%08x]: [%s] sz=%ld session=%d", source, smsg, sz, session);
            //printf("service [.test] message from [:%08x]: [%s] sz=%ld\n", source, smsg, sz);

            // response
            char *retstr = skynet_malloc(20);
            sprintf(retstr, "my n is %d", inst->n);
            skynet_send(context, 0, source, PTYPE_TEXT, 0, retstr, strlen(retstr));
            break;
    }
    return 0;
}

int
test_init(struct test *inst, struct skynet_context *ctx, const char *param) {
    if (param) {
        inst->n = atoi(param);
        //printf("test_init n=%d\n", inst->n);
        skynet_error(ctx, "test_init n=%d", inst->n);
    }
    skynet_callback(ctx, inst, test_cb);
    skynet_command(ctx, "REG", ".test");
    return 0;
}
