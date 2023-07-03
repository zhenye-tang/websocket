#ifndef __WEBSOCKET_SERVICE_H__
#define __WEBSOCKET_SERVICE_H__

#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
#include "websocket.h"

struct cache
{
    char *buf;
    size_t length;
    size_t recv_index;
};

struct websocket_kv
{
    char *key;
    char *value;\
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
    const char *url;
    const char *subprotocol;
    struct websocket_session session;
    struct cache cache;
    struct websocket_callback callback;
    struct websocket_kv_table kv;
    void *userdata;
    pthread_t thread; // pthread object
};

struct websocket_frame
{
    void *data;
    size_t length;
    websocket_frame_type_t type;
};

int websocket_connect_server(struct websocket *ws, const char *url, const char *subprotocol);
int websocket_read_data(struct websocket *ws, struct websocket_frame *frame);
int websocket_write_data(struct websocket *ws, struct websocket_frame *frame);
int websocket_header(struct websocket *ws, const char *key, const char *value);



#ifdef __cplusplus
}
#endif

#endif //__WEBSOCKET_SERVICE_H__