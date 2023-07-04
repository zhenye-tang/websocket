#ifndef __WEBSOCKET_SERVICE_H__
#define __WEBSOCKET_SERVICE_H__

#include "websocket.h"

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef WEBSOCKET_SERVICE_CACHE_SIZE
#define WEBSOCKET_SERVICE_CACHE_SIZE            (1024)
#endif

#ifndef WEBSOCKET_SERVICE_KV_TABLE_LENGTH
#define WEBSOCKET_SERVICE_KV_TABLE_LENGTH        (20)
#endif

#define WEBSOCKET_MALLOC     malloc
#define WEBSOCKET_CALLOC     calloc
#define WEBSOCKET_REALLOC    realloc
#define WEBSOCKET_FREE       free
#define WEBSOCKET_STRDUP     strdup

struct cache
{
    char *buf;
    size_t length;
    size_t recv_index;
};

struct websocket_kv
{
    char *key;
    char *value;
};

struct websocket_kv_table
{
    struct websocket_kv *kv_tab;
    uint16_t kv_total;
    uint16_t kv_use;    
};

struct websocket;
struct websocket_callback
{
    int (*onmessage)(struct websocket *);
    int (*onopen)(struct websocket *);
    int (*onclose)(struct websocket *);
    int (*onerror)(struct websocket *);
};

struct websocket
{
    char *url;
    char *subprotocol;
    struct websocket_session session;
    struct cache cache;
    struct websocket_callback callback;
    struct websocket_kv_table kv;
    void *userdata;
    pthread_t tid;
    pthread_mutex_t lock;
};

struct websocket_frame
{
    void *data;
    size_t length;
    websocket_frame_type_t type;
};

int websocket_init(struct websocket *ws);
void websocket_deinit(struct websocket *ws);
int websocket_set_url(struct websocket *ws, const char *url);
int websocket_set_subprotocol(struct websocket *ws, const char *subprotocol);
int websocket_add_header(struct websocket *ws, const char *key, const char *value);
int websocket_kv_table_init(struct websocket_kv_table *kv_tab, uint16_t tab_size);
int websocket_kv_put(struct websocket_kv_table *kv_tab, const char *key, const char *value);

int websocket_connect_server(struct websocket *ws);
int websocket_disconnect_server(struct websocket *ws);
int websocket_read_data(struct websocket *ws, struct websocket_frame *frame);
int websocket_write_data(struct websocket *ws, struct websocket_frame *frame);

/* event notify */
void websocket_message_event(struct websocket *ws, int (*onmessage)(struct websocket *ws));
void websocket_open_event(struct websocket *ws, int (*onopen)(struct websocket *ws));
void websocket_close_event(struct websocket *ws, int (*onclose)(struct websocket *ws));
void websocket_error_event(struct websocket *ws, int (*onerror)(struct websocket *ws));

#ifdef __cplusplus
}
#endif

#endif //__WEBSOCKET_SERVICE_H__