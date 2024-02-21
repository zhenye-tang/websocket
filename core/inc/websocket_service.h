/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date          Author       Notes
 * 2023-7-3      tzy          first implementation
 */

#ifndef __WEBSOCKET_SERVICE_H__
#define __WEBSOCKET_SERVICE_H__

#include "websocket.h"
#include "websocket_list.h"
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

#ifndef WEBSOCKET_SERVICE_CACHE_SIZE_MAX
#define WEBSOCKET_SERVICE_CACHE_SIZE_MAX            (1024*8)
#endif

#define WEBSOCKET_MALLOC     malloc
#define WEBSOCKET_CALLOC     calloc
#define WEBSOCKET_REALLOC    realloc
#define WEBSOCKET_FREE       free
#define WEBSOCKET_STRDUP     strdup
#define WEBSOCKET_MEMSET     memset

struct websocket;
struct app_websocket
{
    struct websocket *websocket_session;
};

struct app_websocket_frame
{
    void *data;
    size_t length;
    websocket_frame_type_t type;
};

int app_websocket_worker_init(void);
int app_websocket_worker_deinit(void);
int app_websocket_init(struct app_websocket *ws);
void app_websocket_deinit(struct app_websocket *ws);
int app_websocket_set_url(struct app_websocket *ws, const char *url);
int app_websocket_set_subprotocol(struct app_websocket *ws, const char *subprotocol);
int app_websocket_add_header(struct app_websocket *ws, const char *key, const char *value);
int app_websocket_set_close_reason(struct app_websocket *websocket, websocket_status_code_t code, const char *reason);
int app_websocket_get_close_reason(struct app_websocket *websocket, websocket_status_code_t *code, const char **reason);

int app_websocket_connect_server(struct app_websocket *ws);
int app_websocket_disconnect_server(struct app_websocket *ws);
int app_websocket_read_data(struct app_websocket *ws, struct app_websocket_frame *frame);
int app_websocket_write_data(struct app_websocket *ws, struct app_websocket_frame *frame);

/* event notify */
void app_websocket_message_event(struct app_websocket *ws, int (*onmessage)(struct app_websocket *ws));
void app_websocket_open_event(struct app_websocket *ws, int (*onopen)(struct app_websocket *ws));
void app_websocket_close_event(struct app_websocket *ws, int (*onclose)(struct app_websocket *ws));
void app_websocket_error_event(struct app_websocket *ws, int (*onerror)(struct app_websocket *ws));

#ifdef __cplusplus
}
#endif

#endif //__WEBSOCKET_SERVICE_H__