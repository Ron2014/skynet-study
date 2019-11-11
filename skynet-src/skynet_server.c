#include "skynet.h"

#include "skynet_server.h"
#include "skynet_module.h"
#include "skynet_handle.h"
#include "skynet_mq.h"
#include "skynet_timer.h"
#include "skynet_harbor.h"
#include "skynet_env.h"
#include "skynet_monitor.h"
#include "skynet_imp.h"
#include "skynet_log.h"
#include "skynet_timer.h"
#include "spinlock.h"
#include "atomic.h"

#include <pthread.h>

#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#ifdef CALLING_CHECK

#define CHECKCALLING_BEGIN(ctx)             \
    if (!(spinlock_trylock(&ctx->calling))) \
    {                                       \
        assert(0);                          \
    }
#define CHECKCALLING_END(ctx) spinlock_unlock(&ctx->calling);
#define CHECKCALLING_INIT(ctx) spinlock_init(&ctx->calling);
#define CHECKCALLING_DESTROY(ctx) spinlock_destroy(&ctx->calling);
#define CHECKCALLING_DECL struct spinlock calling;

#else

#define CHECKCALLING_BEGIN(ctx)
#define CHECKCALLING_END(ctx)
#define CHECKCALLING_INIT(ctx)
#define CHECKCALLING_DESTROY(ctx)
#define CHECKCALLING_DECL

#endif

/**
 * 服务，Actor，亦可理解为独立的沙盒环境。
 * 每个服务对应一个C模块、消息队列
*/
struct skynet_context
{
    void *instance;					// 模块实例
    struct skynet_module *mod;		// 模块信息，方便访问到create/init/release/signal函数

    void *cb_ud;							// 回调函数的参数userdata，一般是instance指针
    skynet_cb cb;							// 服务的消息回调函数，一般在skynet_module的init函数里指定
    struct message_queue *queue;			// （次级）消息队列
    FILE *logfile;							// log文件
    uint64_t cpu_cost;  // in microsec      cpu消耗，累计值
    uint64_t cpu_start; // in microsec      上一次消息处理时，进行callback的起始时间
    char result[32];                        // cmd命令的返回值
    uint32_t handle;						// 服务实例的句柄（全局唯一）
    int session_id;                         // 消息的ID，是个累加值
    int ref;								// 引用计数
    int message_count;						// 处理过的消息数量（统计信息）
    bool init;                              // 成功初始化标记
    bool endless;                           // 消息是否堵住
    bool profile;                           // 调试信息标记

    CHECKCALLING_DECL
};

////////////////////////////////////////////// 服务共享数据
struct skynet_node
{
    int total;                              // 除去常驻服务的服务实例数量
    int init;                               // 初始化标记
    uint32_t monitor_exit;                  // 服务实例句柄。这个服务用来监控服务下线情况
    pthread_key_t handle_key;               // 线程私有数据的key
    bool profile; // default is off
};

static struct skynet_node G_NODE;
//////////////////////////////////////////////

int skynet_context_total()
{
    return G_NODE.total;
}

static void
context_inc()
{
    ATOM_INC(&G_NODE.total);
}

static void
context_dec()
{
    ATOM_DEC(&G_NODE.total);
}

uint32_t
skynet_current_handle(void)
{
    if (G_NODE.init)
    {
        void *handle = pthread_getspecific(G_NODE.handle_key);
        return (uint32_t)(uintptr_t)handle;
    }
    else
    {
        uint32_t v = (uint32_t)(-THREAD_MAIN);
        return v;
    }
}

static void
id_to_hex(char *str, uint32_t id)
{
    int i;
    static char hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    str[0] = ':';
    for (i = 0; i < 8; i++)
    {
        str[i + 1] = hex[(id >> ((7 - i) * 4)) & 0xf];
    }
    str[9] = '\0';
}

struct drop_t
{
    uint32_t handle;
};

static void
drop_message(struct skynet_message *msg, void *ud)
{
    struct drop_t *d = ud;
    skynet_free(msg->data);
    uint32_t source = d->handle;        // 这个消息所在的消息队列所属的服务句柄，即应该由这个服务处理这个消息的
    assert(source);
    // report error to the message source
    // 这里做了一个反向的操作：通知消息的源服务实例，此处发生了异常
    skynet_send(NULL, source, msg->source, PTYPE_ERROR, 0, NULL, 0);
}

/**
 * 拿到一个模块（so库）并将其实例化。
 * 实例化的结果是生成一个skynet_context（服务）。
 * 每个服务得初始化一个句柄handle，这个句柄有skynet_handle_register指定。
 */
struct skynet_context *
skynet_context_new(const char *name, const char *param)
{
    struct skynet_module *mod = skynet_module_query(name);

    if (mod == NULL)
        return NULL;

    void *inst = skynet_module_instance_create(mod);
    if (inst == NULL)
        return NULL;
    struct skynet_context *ctx = skynet_malloc(sizeof(*ctx));
    CHECKCALLING_INIT(ctx)

    // 一个服务和模块、模块实例相关
    ctx->mod = mod;
    ctx->instance = inst;

    // 因为skynet_module_instance_init成功后会触发一次skynet_context_release（这里会将引用计数减1）
    // 所以初始值是2
    ctx->ref = 2;
    ctx->cb = NULL;
    ctx->cb_ud = NULL;
    ctx->session_id = 0;
    ctx->logfile = NULL;

    ctx->init = false;
    ctx->endless = false;

    ctx->cpu_cost = 0;
    ctx->cpu_start = 0;
    ctx->message_count = 0;
    ctx->profile = G_NODE.profile;
    // Should set to 0 first to avoid skynet_handle_retireall get an uninitialized handle
    ctx->handle = 0;
    ctx->handle = skynet_handle_register(ctx);
    struct message_queue *queue = ctx->queue = skynet_mq_create(ctx->handle);
    // init function maybe use ctx->handle, so it must init at last
    context_inc();

    CHECKCALLING_BEGIN(ctx)
    int r = skynet_module_instance_init(mod, inst, ctx, param);
    CHECKCALLING_END(ctx)
    if (r == 0)
    {
        struct skynet_context *ret = skynet_context_release(ctx);
        if (ret)
        {
            ctx->init = true;
        }
        skynet_globalmq_push(queue);
        if (ret)
        {
            // 借skynet_error打了个LOG，实际上没有error
            skynet_error(ret, "LAUNCH %s %s", name, param ? param : "");
        }
        return ret;
    }
    else
    {
        skynet_error(ctx, "FAILED launch %s", name);
        uint32_t handle = ctx->handle;
        skynet_context_release(ctx);
        skynet_handle_retire(handle);
        struct drop_t d = {handle};
        skynet_mq_release(queue, drop_message, &d);
        return NULL;
    }
}

int skynet_context_newsession(struct skynet_context *ctx)
{
    // session always be a positive number
    int session = ++ctx->session_id;
    if (session <= 0)
    {
        ctx->session_id = 1;
        return 1;
    }
    return session;
}

/**
 * 某些函数会返回 skynet_context *，如 skynet_handle_grab
 * 这表示在函数外与服务实例建立了新的引用关系
 * 此时引用计数需要自增
*/
void skynet_context_grab(struct skynet_context *ctx)
{
    ATOM_INC(&ctx->ref);
}

/**
 * 注册常驻内存的服务实例
*/
void skynet_context_reserve(struct skynet_context *ctx)
{
    skynet_context_grab(ctx);
    // don't count the context reserved, because skynet abort (the worker threads terminate) only when the total context is 0 .
    // the reserved context will be release at last.
    context_dec();
}

/**
 * 服务实例下线了，引用计数为0时触发
*/
static void
delete_context(struct skynet_context *ctx)
{
    if (ctx->logfile)
    {
        fclose(ctx->logfile);
    }
    skynet_module_instance_release(ctx->mod, ctx->instance);
    skynet_mq_mark_release(ctx->queue);
    CHECKCALLING_DESTROY(ctx)
    skynet_free(ctx);
    context_dec();
}

struct skynet_context *
skynet_context_release(struct skynet_context *ctx)
{
    if (ATOM_DEC(&ctx->ref) == 0)
    {
        delete_context(ctx);
        return NULL;
    }
    return ctx;
}

/**
 * 服务实例获得一个新消息
*/
int skynet_context_push(uint32_t handle, struct skynet_message *message)
{
    struct skynet_context *ctx = skynet_handle_grab(handle);
    if (ctx == NULL)
    {
        return -1;
    }
    skynet_mq_push(ctx->queue, message);
    skynet_context_release(ctx);

    return 0;
}

// 标记该服务的消息队列堵住了
void skynet_context_endless(uint32_t handle)
{
    struct skynet_context *ctx = skynet_handle_grab(handle);
    if (ctx == NULL)
    {
        return;
    }
    ctx->endless = true;
    skynet_context_release(ctx);
}

/**
 * 判断服务实例是否不在本机
 * 同时获取handle中携带的服务器ID
*/
int skynet_isremote(struct skynet_context *ctx, uint32_t handle, int *harbor)
{
    int ret = skynet_harbor_message_isremote(handle);
    if (harbor)
    {
        *harbor = (int)(handle >> HANDLE_REMOTE_SHIFT);
    }
    return ret;
}

/**
 * 处理一个消息
*/
static void
dispatch_message(struct skynet_context *ctx, struct skynet_message *msg)
{
    assert(ctx->init);
    CHECKCALLING_BEGIN(ctx)
    
    // TLS，设置了服务的handle，malloc_hook就可知是哪个服务申请内存
    pthread_setspecific(G_NODE.handle_key, (void *)(uintptr_t)(ctx->handle));

    int type = msg->sz >> MESSAGE_TYPE_SHIFT;
    size_t sz = msg->sz & MESSAGE_TYPE_MASK;

    if (ctx->logfile)
    {
        skynet_log_output(ctx->logfile, msg->source, type, msg->session, msg->data, sz);
    }
    ++ctx->message_count;

    int reserve_msg;
    if (ctx->profile)
    {
        // 如果开启分析器，需要统计cpu开销
        ctx->cpu_start = skynet_thread_time();
        reserve_msg = ctx->cb(ctx, ctx->cb_ud, type, msg->session, msg->source, msg->data, sz);
        uint64_t cost_time = skynet_thread_time() - ctx->cpu_start;
        ctx->cpu_cost += cost_time;
    }
    else
    {
        reserve_msg = ctx->cb(ctx, ctx->cb_ud, type, msg->session, msg->source, msg->data, sz);
    }
    if (!reserve_msg)
    {
        // 一般callback返回0
        // 保留消息的情况，见于service_gate.c lua-skynet.c
        skynet_free(msg->data);
    }
    CHECKCALLING_END(ctx)
}

/**
 * 在进程异常退出时，需要一次性处理完整个消息队列（logger）
*/
void skynet_context_dispatchall(struct skynet_context *ctx)
{
    // for skynet_error
    struct skynet_message msg;
    struct message_queue *q = ctx->queue;
    while (!skynet_mq_pop(q, &msg))
    {
        dispatch_message(ctx, &msg);
    }
}

/**
 * woker线程的主要工作：消息循环的一个步骤
*/
struct message_queue *
skynet_context_message_dispatch(struct skynet_monitor *sm, struct message_queue *q, int weight)
{
    if (q == NULL)
    {
        // 全局消息队列中取出一个队列
        q = skynet_globalmq_pop();
        if (q == NULL)
            return NULL;
    }

    // 从消息队列，获取服务实例句柄、服务实例
    uint32_t handle = skynet_mq_handle(q);

    // grab操作后，服务实例的引用计数自增1
    struct skynet_context *ctx = skynet_handle_grab(handle);
    if (ctx == NULL)
    {
        struct drop_t d = {handle};
        skynet_mq_release(q, drop_message, &d);
        // 很不幸，第一次取消息队列就是个需要销毁的队列
        // 这里再取一次消息队列，并在下一次主循环处理
        return skynet_globalmq_pop();
    }

    int i, n = 1;
    struct skynet_message msg;

    for (i = 0; i < n; i++)
    {
        if (skynet_mq_pop(q, &msg))
        {
            // 消息队列为空，就不断算将其放回去了
            // 仅恢复引用计数
            skynet_context_release(ctx);
            return skynet_globalmq_pop();
        }
        else if (i == 0 && weight >= 0)
        {
            // 在处理第1个消息之前，重新调整要处理的消息数量
            // n = 消息队列有效长度 / (2^weight)
            n = skynet_mq_length(q);
            n >>= weight;
        }

        // 超载报警
        int overload = skynet_mq_overload(q);
        if (overload)
        {
            skynet_error(ctx, "May overload, message queue length = %d", overload);
        }

        // 消息的源、处理该消息的服务，统统交给监视器
        skynet_monitor_trigger(sm, msg.source, handle);

        if (ctx->cb == NULL)
        {
            skynet_free(msg.data);
        }
        else
        {
            // 处理消息
            dispatch_message(ctx, &msg);
        }

        // 重置监视器的数据
        skynet_monitor_trigger(sm, 0, 0);
    }

    assert(q == ctx->queue);
    struct message_queue *nq = skynet_globalmq_pop();
    if (nq)
    {
        // 为了避免全局消息队列频繁的pop/push，需要判断是不是仅存1个次级消息队列的情况
        // If global mq is not empty , push q back, and return next queue (nq)
        // Else (global mq is empty or block, don't push q back, and return q again (for next dispatch)
        skynet_globalmq_push(q);
        q = nq;
    }
    skynet_context_release(ctx);

    return q;
}

static void
copy_name(char name[GLOBALNAME_LENGTH], const char *addr)
{
    int i;
    for (i = 0; i < GLOBALNAME_LENGTH && addr[i]; i++)
    {
        name[i] = addr[i];
    }
    for (; i < GLOBALNAME_LENGTH; i++)
    {
        name[i] = '\0';
    }
}

uint32_t
skynet_queryname(struct skynet_context *context, const char *name)
{
    switch (name[0])
    {
    case ':':
        return strtoul(name + 1, NULL, 16);
    case '.':
        return skynet_handle_findname(name + 1);
    }
    skynet_error(context, "Don't support query global name %s", name);
    return 0;
}

/**
 * 下线某个服务。
 * 如果handle为0，就是杀掉自己。
*/
static void
handle_exit(struct skynet_context *context, uint32_t handle)
{
    if (handle == 0)
    {
        handle = context->handle;
        skynet_error(context, "KILL self");
    }
    else
    {
        skynet_error(context, "KILL :%0x", handle);
    }
    if (G_NODE.monitor_exit)
    {
        // 如果有服务监控下线情况，发消息给它
        skynet_send(context, handle, G_NODE.monitor_exit, PTYPE_CLIENT, 0, NULL, 0);
    }
    skynet_handle_retire(handle);
}

////////////////////////////////////////////////////////// skynet 命令
// skynet command

struct command_func
{
    const char *name;
    const char *(*func)(struct skynet_context *context, const char *param);
};


static const char *
cmd_timeout(struct skynet_context *context, const char *param)
{
    char *session_ptr = NULL;
    int ti = strtol(param, &session_ptr, 10);
    int session = skynet_context_newsession(context);
    skynet_timeout(context->handle, ti, session);
    sprintf(context->result, "%d", session);
    return context->result;
}

/**
 * 获取服务实例的句柄
 * 或者设置别名
*/
static const char *
cmd_reg(struct skynet_context *context, const char *param)
{
    if (param == NULL || param[0] == '\0')
    {
        // 如果param无效
        // 则是获取服务实例的句柄 :xxx
        sprintf(context->result, ":%x", context->handle);
        return context->result;
    }
    else if (param[0] == '.')
    {
        // 如果param有效
        // 则是给该服务起个别名
        return skynet_handle_namehandle(context->handle, param + 1);
    }
    else
    {
        skynet_error(context, "Can't register global name %s in C", param);
        return NULL;
    }
}

/**
 * 根据别名，获取服务实例的句柄
 * param格式：.xxx
*/
static const char *
cmd_query(struct skynet_context *context, const char *param)
{
    if (param[0] == '.')
    {
        uint32_t handle = skynet_handle_findname(param + 1);
        if (handle)
        {
            sprintf(context->result, ":%x", handle);
            return context->result;
        }
    }
    return NULL;
}

/**
 * 给服务实例取别名
 * param格式：
 * .xxx :yyy （别名 服务实例句柄）
*/
static const char *
cmd_name(struct skynet_context *context, const char *param)
{
    int size = strlen(param);
    char name[size + 1];
    char handle[size + 1];
    sscanf(param, "%s %s", name, handle);
    if (handle[0] != ':')
    {
        return NULL;
    }
    uint32_t handle_id = strtoul(handle + 1, NULL, 16);
    if (handle_id == 0)
    {
        return NULL;
    }
    if (name[0] == '.')
    {
        return skynet_handle_namehandle(handle_id, name + 1);
    }
    else
    {
        skynet_error(context, "Can't set global name %s in C", name);
    }
    return NULL;
}

/**
 * 服务实例退出（自杀）
 * param无意义
*/
static const char *
cmd_exit(struct skynet_context *context, const char *param)
{
    handle_exit(context, 0);
    return NULL;
}

/**
 * 获取服务实例的句柄
 * param有两个格式：
 * :xxx 仅做字符串转换
 * .xxx 指定了服务实例的别名
*/
static uint32_t
tohandle(struct skynet_context *context, const char *param)
{
    uint32_t handle = 0;
    if (param[0] == ':')
    {
        handle = strtoul(param + 1, NULL, 16);
    }
    else if (param[0] == '.')
    {
        handle = skynet_handle_findname(param + 1);
    }
    else
    {
        skynet_error(context, "Can't convert %s to handle", param);
    }

    return handle;
}

/**
 * 杀掉某个服务实例
 * param有两个格式：
 * :xxx 句柄
 * .xxx 别名
*/
static const char *
cmd_kill(struct skynet_context *context, const char *param)
{
    uint32_t handle = tohandle(context, param);
    if (handle)
    {
        handle_exit(context, handle);
    }
    return NULL;
}

/**
 * 启动一个服务实例
*/
static const char *
cmd_launch(struct skynet_context *context, const char *param)
{
    size_t sz = strlen(param);
    char tmp[sz + 1];
    strcpy(tmp, param);
    char *args = tmp;

    char *mod = strsep(&args, " \t\r\n");   // 第一个参数：模块名
    args = strsep(&args, "\r\n");           // 第二个参数：模块实例化成服务时，init要用到的参数
    // args被自身覆盖，即忽略了换行符后面的参数。但是空格、制表符连接的参数还是接受的

    struct skynet_context *inst = skynet_context_new(mod, args);
    if (inst == NULL)
    {
        return NULL;
    }
    else
    {
        id_to_hex(context->result, inst->handle);
        return context->result;
    }
}

static const char *
cmd_getenv(struct skynet_context *context, const char *param)
{
    return skynet_getenv(param);
}

static const char *
cmd_setenv(struct skynet_context *context, const char *param)
{
    size_t sz = strlen(param);
    char key[sz + 1];
    int i;
    for (i = 0; param[i] != ' ' && param[i]; i++)
    {
        key[i] = param[i];
    }
    if (param[i] == '\0')
        return NULL;

    key[i] = '\0';
    param += i + 1;

    skynet_setenv(key, param);
    return NULL;
}

/**
 * 系统启动时间
*/
static const char *
cmd_starttime(struct skynet_context *context, const char *param)
{
    uint32_t sec = skynet_starttime();
    sprintf(context->result, "%u", sec);
    return context->result;
}

/**
 * 进程终止
 * 所有服务下线
*/
static const char *
cmd_abort(struct skynet_context *context, const char *param)
{
    skynet_handle_retireall();
    return NULL;
}

/**
 * 如果param无效
 * 获取监控服务下线情况的服务实例句柄。
 * 
 * 如果param有效
 * 设置监控服务下线情况的服务实例句柄。
*/
static const char *
cmd_monitor(struct skynet_context *context, const char *param)
{
    uint32_t handle = 0;
    if (param == NULL || param[0] == '\0')
    {
        if (G_NODE.monitor_exit)
        {
            // return current monitor serivce
            sprintf(context->result, ":%x", G_NODE.monitor_exit);
            return context->result;
        }
        return NULL;
    }
    else
    {
        handle = tohandle(context, param);
    }
    G_NODE.monitor_exit = handle;
    return NULL;
}

static const char *
cmd_stat(struct skynet_context *context, const char *param)
{
    if (strcmp(param, "mqlen") == 0)
    {
        int len = skynet_mq_length(context->queue);
        sprintf(context->result, "%d", len);
    }
    else if (strcmp(param, "endless") == 0)
    {
        if (context->endless)
        {
            strcpy(context->result, "1");
            context->endless = false;
        }
        else
        {
            strcpy(context->result, "0");
        }
    }
    else if (strcmp(param, "cpu") == 0)
    {
        double t = (double)context->cpu_cost / 1000000.0; // microsec
        sprintf(context->result, "%lf", t);
    }
    else if (strcmp(param, "time") == 0)
    {
        if (context->profile)
        {
            uint64_t ti = skynet_thread_time() - context->cpu_start;
            double t = (double)ti / 1000000.0; // microsec
            sprintf(context->result, "%lf", t);
        }
        else
        {
            strcpy(context->result, "0");
        }
    }
    else if (strcmp(param, "message") == 0)
    {
        sprintf(context->result, "%d", context->message_count);
    }
    else
    {
        context->result[0] = '\0';
    }
    return context->result;
}

static const char *
cmd_logon(struct skynet_context *context, const char *param)
{
    uint32_t handle = tohandle(context, param);
    if (handle == 0)
        return NULL;
    struct skynet_context *ctx = skynet_handle_grab(handle);
    if (ctx == NULL)
        return NULL;
    FILE *f = NULL;
    FILE *lastf = ctx->logfile;
    if (lastf == NULL)
    {
        f = skynet_log_open(context, handle);
        if (f)
        {
            if (!ATOM_CAS_POINTER(&ctx->logfile, NULL, f))
            {
                // logfile opens in other thread, close this one.
                fclose(f);
            }
        }
    }
    skynet_context_release(ctx);
    return NULL;
}

static const char *
cmd_logoff(struct skynet_context *context, const char *param)
{
    uint32_t handle = tohandle(context, param);
    if (handle == 0)
        return NULL;
    struct skynet_context *ctx = skynet_handle_grab(handle);
    if (ctx == NULL)
        return NULL;
    FILE *f = ctx->logfile;
    if (f)
    {
        // logfile may close in other thread
        if (ATOM_CAS_POINTER(&ctx->logfile, f, NULL))
        {
            skynet_log_close(context, f, handle);
        }
    }
    skynet_context_release(ctx);
    return NULL;
}

static const char *
cmd_signal(struct skynet_context *context, const char *param)
{
    uint32_t handle = tohandle(context, param);
    if (handle == 0)
        return NULL;
    struct skynet_context *ctx = skynet_handle_grab(handle);
    if (ctx == NULL)
        return NULL;
    param = strchr(param, ' ');
    int sig = 0;
    if (param)
    {
        sig = strtol(param, NULL, 0);
    }
    // NOTICE: the signal function should be thread safe.
    skynet_module_instance_signal(ctx->mod, ctx->instance, sig);

    skynet_context_release(ctx);
    return NULL;
}

static struct command_func cmd_funcs[] = {
    {"TIMEOUT", cmd_timeout},
    {"REG", cmd_reg},
    {"QUERY", cmd_query},
    {"NAME", cmd_name},
    {"EXIT", cmd_exit},
    {"KILL", cmd_kill},
    {"LAUNCH", cmd_launch},
    {"GETENV", cmd_getenv},
    {"SETENV", cmd_setenv},
    {"STARTTIME", cmd_starttime},
    {"ABORT", cmd_abort},
    {"MONITOR", cmd_monitor},
    {"STAT", cmd_stat},
    {"LOGON", cmd_logon},
    {"LOGOFF", cmd_logoff},
    {"SIGNAL", cmd_signal},
    {NULL, NULL},
};

/**
 * 执行命令cmd，所有命令的返回值都是 char*
 * 为了避免内存开销，返回值 char* 在必要的时候存放在 context->result 中
*/
const char *
skynet_command(struct skynet_context *context, const char *cmd, const char *param)
{
    struct command_func *method = &cmd_funcs[0];
    while (method->name)
    {
        if (strcmp(cmd, method->name) == 0)
        {
            return method->func(context, param);
        }
        ++method;
    }

    return NULL;
}

//////////////////////////////////////////////////////////

/**
 * 将消息进行加工：
 * 1. 分配消息ID
 * 2. 拷贝数据
*/
static void
_filter_args(struct skynet_context *context, int type, int *session, void **data, size_t *sz)
{
    int needcopy = !(type & PTYPE_TAG_DONTCOPY);
    int allocsession = type & PTYPE_TAG_ALLOCSESSION;
    type &= 0xff;

    if (allocsession)
    {
        assert(*session == 0);
        *session = skynet_context_newsession(context);
    }

    if (needcopy && *data)
    {
        char *msg = skynet_malloc(*sz + 1);
        memcpy(msg, *data, *sz);
        msg[*sz] = '\0';
        *data = msg;
    }

    *sz |= (size_t)type << MESSAGE_TYPE_SHIFT;
}

/**
 * 服务source向服务destination发送消息
 * 消息源也可以是context
 * 
 * skynet_send -> _filter_args -> skynet_harbor_send -> skynet_context_send -> skynet_mq_push
 *                             -> skynet_context_push -> skynet_mq_push
*/
int skynet_send(struct skynet_context *context, uint32_t source, uint32_t destination, int type, int session, void *data, size_t sz)
{
    if ((sz & MESSAGE_TYPE_MASK) != sz)
    {
        skynet_error(context, "The message to %x is too large", destination);
        if (type & PTYPE_TAG_DONTCOPY)
        {
            skynet_free(data);
        }
        return -2;
    }
    _filter_args(context, type, &session, (void **)&data, &sz);

    if (source == 0)
    {
        source = context->handle;
    }

    if (destination == 0)
    {
        if (data)
        {
            skynet_error(context, "Destination address can't be 0");
            skynet_free(data);
            return -1;
        }

        return session;
    }
    if (skynet_harbor_message_isremote(destination))
    {
        struct remote_message *rmsg = skynet_malloc(sizeof(*rmsg));
        rmsg->destination.handle = destination;
        rmsg->message = data;
        rmsg->sz = sz & MESSAGE_TYPE_MASK;
        rmsg->type = sz >> MESSAGE_TYPE_SHIFT;
        skynet_harbor_send(rmsg, source, session);
    }
    else
    {
        struct skynet_message smsg;
        smsg.source = source;
        smsg.session = session;
        smsg.data = data;
        smsg.sz = sz;

        if (skynet_context_push(destination, &smsg))
        {
            skynet_free(data);
            return -1;
        }
    }
    return session;
}

/**
 * 通过addr确定destination的skynet_send
*/
int skynet_sendname(struct skynet_context *context, uint32_t source, const char *addr, int type, int session, void *data, size_t sz)
{
    if (source == 0)
    {
        source = context->handle;
    }

    // 处理addr，变成相应的服务实例句柄
    uint32_t des = 0;
    if (addr[0] == ':')
    {
        // :handle
        des = strtoul(addr + 1, NULL, 16);
    }
    else if (addr[0] == '.')
    {
        // .name
        des = skynet_handle_findname(addr + 1);
        if (des == 0)
        {
            if (type & PTYPE_TAG_DONTCOPY)
            {
                skynet_free(data);
            }
            return -1;
        }
    }
    else
    {
        // 远程服务实例
        if ((sz & MESSAGE_TYPE_MASK) != sz)
        {
            skynet_error(context, "The message to %s is too large", addr);
            if (type & PTYPE_TAG_DONTCOPY)
            {
                skynet_free(data);
            }
            return -2;
        }
        _filter_args(context, type, &session, (void **)&data, &sz);

        struct remote_message *rmsg = skynet_malloc(sizeof(*rmsg));
        copy_name(rmsg->destination.name, addr);
        rmsg->destination.handle = 0;
        rmsg->message = data;
        rmsg->sz = sz & MESSAGE_TYPE_MASK;
        rmsg->type = sz >> MESSAGE_TYPE_SHIFT;

        skynet_harbor_send(rmsg, source, session);
        return session;
    }

    return skynet_send(context, source, des, type, session, data, sz);
}

uint32_t
skynet_context_handle(struct skynet_context *ctx)
{
    return ctx->handle;
}

void skynet_callback(struct skynet_context *context, void *ud, skynet_cb cb)
{
    context->cb = cb;
    context->cb_ud = ud;
}

void skynet_context_send(struct skynet_context *ctx, void *msg, size_t sz, uint32_t source, int type, int session)
{
    struct skynet_message smsg;
    smsg.source = source;
    smsg.session = session;
    smsg.data = msg;
    smsg.sz = sz | (size_t)type << MESSAGE_TYPE_SHIFT;

    skynet_mq_push(ctx->queue, &smsg);
}

void skynet_globalinit(void)
{
    G_NODE.total = 0;
    G_NODE.monitor_exit = 0;
    G_NODE.init = 1;
    if (pthread_key_create(&G_NODE.handle_key, NULL))
    {
        fprintf(stderr, "pthread_key_create failed");
        exit(1);
    }
    // set mainthread's key
    skynet_initthread(THREAD_MAIN);
}

void skynet_globalexit(void)
{
    pthread_key_delete(G_NODE.handle_key);
}

void skynet_initthread(int m)
{
    uintptr_t v = (uint32_t)(-m);
    pthread_setspecific(G_NODE.handle_key, (void *)v);
}

void skynet_profile_enable(int enable)
{
    G_NODE.profile = (bool)enable;
}
