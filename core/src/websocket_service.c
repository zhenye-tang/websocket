/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date          Author       Notes
 * 2023-7-3      tzy          first implementation
 */
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <poll.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include "websocket_service.h"

#define APP_WEBSOCKET_POLLFD_MAX 10

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

struct websocket_callback
{
    int (*onmessage)(struct app_websocket *);
    int (*onopen)(struct app_websocket *);
    int (*onclose)(struct app_websocket *);
    int (*onerror)(struct app_websocket *);
};

struct app_websocket_close_status
{
    char *reason;
    websocket_status_code_t status_code;
};

struct app_websocket_server_status
{
    struct app_websocket_close_status status;
    uint16_t server_close;
};

struct app_websocket_client_status
{
    struct app_websocket_close_status status;
};

struct websocket
{
    char *url;
    char *subprotocol;
    struct app_websocket *app_websocket;
    struct websocket_session session;
    struct app_websocket_server_status server_status;
    struct app_websocket_client_status client_status;
    struct cache cache;
    struct websocket_callback callback;
    struct websocket_kv_table kv;
    void *userdata;
    const char *error_reason;
    int state;
    int is_connect;
    pthread_t tid;
    pthread_mutex_t lock;
    ws_list_t node;
    int recv_size;
};

struct websocket_worker
{
    pthread_t tid;
    int pipe[2];
    struct pollfd poll[APP_WEBSOCKET_POLLFD_MAX];
    pthread_mutex_t lock;
};

enum FSM_WEBSOCKET_STATE
{
    WEBSOCKET_STATE_INIT = 0,
    WEBSOCKET_STATE_READ,
    WEBSOCKET_STATE_CLOSE,
    WEBSOCKET_STATE_MONITOR,
    WEBSOCKET_STATE_ERROR,
    WEBSOCKET_STATE_EXIT
};

#define WEBSOCKAET_APPEND_CACHE_SIZE        (1024)

static ws_list_t websocket_session_list = WS_LIST_OBJECT_INIT(websocket_session_list);

static struct websocket_worker worker;

int app_websocket_enter_critical(struct websocket *session)
{
    return session ? pthread_mutex_lock(&session->lock) : -EINVAL;
}

void app_websocket_exit_critical(struct websocket *session)
{
    pthread_mutex_unlock(&session->lock);
}

static void app_websocket_session_clean(struct websocket *app_ws_session)
{
    ws_list_remove(&app_ws_session->node);

    if (app_ws_session->url)
    {
        WEBSOCKET_FREE((void *)app_ws_session->url);
    }

    if (app_ws_session->subprotocol)
    {
        WEBSOCKET_FREE((void *)app_ws_session->subprotocol);
    }

    if(app_ws_session->cache.buf)
        WEBSOCKET_FREE(app_ws_session->cache.buf);

    if (app_ws_session->kv.kv_tab)
    {
        for (int i = 0; i < app_ws_session->kv.kv_use; i++)
        {
            WEBSOCKET_FREE(app_ws_session->kv.kv_tab[i].key);
            WEBSOCKET_FREE(app_ws_session->kv.kv_tab[i].value);
        }
        WEBSOCKET_FREE(app_ws_session->kv.kv_tab);
    }

    if (app_ws_session->server_status.status.reason)
    {
        WEBSOCKET_FREE(app_ws_session->server_status.status.reason);
    }

    if (app_ws_session->client_status.status.reason)
    {
        WEBSOCKET_FREE(app_ws_session->client_status.status.reason);
    }

    WEBSOCKET_MEMSET(app_ws_session, 0, sizeof(struct websocket));
    WEBSOCKET_FREE(app_ws_session);
}

static int fsm_driver(struct websocket *app_ws_session)
{
    int err = WEBSOCKET_OK;
    switch (app_ws_session->state)
    {
    case WEBSOCKET_STATE_INIT:
    {
        struct websocket_kv *kv_tab = app_ws_session->kv.kv_tab;
        websocket_session_init(&app_ws_session->session);
        if (kv_tab != NULL)
        {
            for (int i = 0; i < app_ws_session->kv.kv_use; i++)
            {
                websocket_header_fields_add(&app_ws_session->session, "%s: %s\r\n", kv_tab->key, kv_tab->value);
                kv_tab += 1;
            }
        }

        if (websocket_connect(&app_ws_session->session, app_ws_session->url, app_ws_session->subprotocol) == WEBSOCKET_OK)
        {
            ioctl(app_ws_session->session.socket_fd, FIONBIO, 1);
            err = app_websocket_enter_critical(app_ws_session);
            if (err == WEBSOCKET_OK)
            {
                if (app_ws_session->app_websocket)
                {
                    app_ws_session->state = WEBSOCKET_STATE_MONITOR;
                    app_ws_session->is_connect = 1;
                    if (app_ws_session->callback.onopen)
                    {
                        app_ws_session->callback.onopen(app_ws_session->app_websocket);
                    }
                }
                else
                {
                    app_ws_session->state = WEBSOCKET_STATE_CLOSE;
                    app_ws_session->is_connect = 0;
                }
                app_websocket_exit_critical(app_ws_session);
            }
        }
        else
        {
            err = app_websocket_enter_critical(app_ws_session);
            if (err == WEBSOCKET_OK)
            {
                if (app_ws_session->app_websocket)
                {
                    app_ws_session->state = WEBSOCKET_STATE_ERROR;
                    app_ws_session->error_reason = "Failed to connect to the server!!";
                }
                else
                {
                    app_ws_session->state = WEBSOCKET_STATE_CLOSE;
                }
                app_websocket_exit_critical(app_ws_session);
            }
        }
    }
    break;
    case WEBSOCKET_STATE_MONITOR:
        break;
    case WEBSOCKET_STATE_CLOSE:
    {
        if (app_ws_session->is_connect && !app_ws_session->server_status.server_close)
        {
            int reason_len = 0;
            if (app_ws_session->client_status.status.reason)
            {
                reason_len = strlen(app_ws_session->client_status.status.reason);
            }
            websocket_send_close(&app_ws_session->session, app_ws_session->client_status.status.status_code, app_ws_session->client_status.status.reason, reason_len);
        }
        app_ws_session->is_connect = 0;
        websocket_disconnect(&app_ws_session->session);
        app_ws_session->state = WEBSOCKET_STATE_EXIT;
    }
    break;
    case WEBSOCKET_STATE_READ:
    {
        err = app_websocket_enter_critical(app_ws_session);
        if (err == WEBSOCKET_OK)
        {
            if (app_ws_session->app_websocket)
            {
                if (app_ws_session->callback.onmessage)
                {
                    if (app_ws_session->callback.onmessage(app_ws_session->app_websocket) == WEBSOCKET_OK)
                    {
                        app_ws_session->state = WEBSOCKET_STATE_MONITOR;
                    }
                    else
                    {
                        app_ws_session->state = WEBSOCKET_STATE_ERROR;
                    }
                }
            }
            else
            {
                app_ws_session->state = WEBSOCKET_STATE_CLOSE;
                app_ws_session->is_connect = 0;
            }
            app_websocket_exit_critical(app_ws_session);
        }
    }
    break;
    case WEBSOCKET_STATE_ERROR:
    {
        err = app_websocket_enter_critical(app_ws_session);
        if (err == WEBSOCKET_OK)
        {
            if (app_ws_session->app_websocket)
            {
                if (app_ws_session->callback.onerror)
                {
                    app_ws_session->callback.onerror(app_ws_session->app_websocket);
                }
            }
            app_websocket_exit_critical(app_ws_session);
        }
        websocket_disconnect(&app_ws_session->session);
        app_ws_session->state = WEBSOCKET_STATE_MONITOR;
    }
    break;
    case WEBSOCKET_STATE_EXIT:
    {
        char app_close = 0;
        err = app_websocket_enter_critical(app_ws_session);
        if (err == WEBSOCKET_OK)
        {
            if (app_ws_session->app_websocket)
            {
                if (app_ws_session->callback.onclose)
                {
                    app_ws_session->callback.onclose(app_ws_session->app_websocket);
                }
            }
            else
            {
                app_close = 1;
            }
            app_websocket_exit_critical(app_ws_session);
        }

        if (app_close)
        {
            app_websocket_session_clean(app_ws_session);
        }
    }
    break;
    default:
        break;
    }
    return 0;
}

static void *worker_entry(void *prma)
{
    struct websocket_worker *_worker = (struct websocket_worker *)prma;
    struct websocket *websocket_session;
    struct pollfd *fds = _worker->poll;
    int nfds;
    ws_list_t worker_list = WS_LIST_OBJECT_INIT(worker_list);
    ws_list_t *pos, *node;

    for (int i = 0; i < APP_WEBSOCKET_POLLFD_MAX; i++)
    {
        fds[i].revents = 0;
        fds[i].events = POLLIN | POLLERR;
    }
    fds[0].fd = _worker->pipe[0];

    while(1)
    {
        nfds = 1;
        ws_list_for_each(node, &worker_list)
        {
            struct websocket *ws_obj = ws_container_of(node, struct websocket, node);
            fds[nfds].fd = ws_obj->session.socket_fd;
            fds[nfds].revents = 0;
            nfds += 1;
        }

        if((nfds = poll(fds, nfds, -1)) < 0)
        {
            if (errno == EINTR)
                continue;
            else
                break;
        }

        if (fds[0].revents & POLLIN)
        {
            char recv[4];
            read(fds[0].fd, recv, 1);
            if(recv[0] == 'q')
                break;
        }
        nfds = 1;

        pthread_mutex_lock(&_worker->lock);
        ws_list_for_each_safe(pos, node, &websocket_session_list)
        {
            ws_list_remove(pos);
            ws_list_insert_before(&worker_list, pos);
        }
        pthread_mutex_unlock(&_worker->lock);

        ws_list_for_each_safe(pos, node, &worker_list)
        {
            websocket_session = ws_container_of(pos, struct websocket, node);
            if (fds[nfds].revents & POLLIN)
            {
                if (websocket_session->server_status.server_close == 0)
                {
                    websocket_session->state = WEBSOCKET_STATE_READ;
                }
                else
                {
                    websocket_session->state = WEBSOCKET_STATE_CLOSE;
                }
            }
            else if (fds[nfds].revents & POLLERR)
            {
                websocket_session->state = WEBSOCKET_STATE_ERROR;
            }

            if (websocket_session->state < WEBSOCKET_STATE_MONITOR)
            {
                fsm_driver(websocket_session);
            }
            nfds += 1;
        }

        ws_list_for_each_safe(pos, node, &worker_list)
        {
            websocket_session = ws_container_of(pos, struct websocket, node);
            if (websocket_session->state > WEBSOCKET_STATE_MONITOR)
            {
                fsm_driver(websocket_session);
            }
        }
    }

    return "byby";
}

int app_websocket_worker_init(void)
{
    memset(&worker, 0, sizeof(worker));
    pipe(worker.pipe);
    pthread_mutex_init(&worker.lock, NULL);
    pthread_create(&worker.tid, NULL, worker_entry, &worker);
    return 0;
}

int app_websocket_worker_deinit(void)
{
    write(worker.pipe[1], "q", 1);
    pthread_join(worker.tid, NULL);
    pthread_detach(worker.tid);
    pthread_mutex_destroy(&worker.lock);
    return 0;
}

void websocket_kv_table_deinit(struct websocket_kv_table *kv_tab)
{
    for(int i = 0; i < kv_tab->kv_use; i++)
    {
        WEBSOCKET_FREE(kv_tab->kv_tab[i].key);
        WEBSOCKET_FREE(kv_tab->kv_tab[i].value);
    }
    WEBSOCKET_FREE(kv_tab->kv_tab);
}

int websocket_kv_table_init(struct websocket_kv_table *kv_tab, uint16_t tab_size)
{
    websocket_kv_table_deinit(kv_tab);
    kv_tab->kv_tab = WEBSOCKET_MALLOC(sizeof(struct websocket_kv) * tab_size);
    if(kv_tab->kv_tab)
    {
        memset(kv_tab->kv_tab, 0, sizeof(struct websocket_kv) * tab_size);
        kv_tab->kv_total = tab_size;
        kv_tab->kv_use = 0;
    }
    return kv_tab->kv_tab ? tab_size : -1;
}

static struct websocket_kv *websocket_kv_table_alloc(struct websocket_kv_table *kv_tab)
{
    struct websocket_kv *new_kv = NULL;
    if(kv_tab)
    {
        new_kv = WEBSOCKET_REALLOC(kv_tab->kv_tab, sizeof(struct websocket_kv) * (kv_tab->kv_total + WEBSOCKET_SERVICE_KV_TABLE_LENGTH));
        if(new_kv != NULL)
        {
            kv_tab->kv_tab = new_kv;
            kv_tab->kv_total += WEBSOCKET_SERVICE_KV_TABLE_LENGTH;
            memset(kv_tab->kv_tab + kv_tab->kv_use, 0, sizeof(struct websocket_kv) * WEBSOCKET_SERVICE_KV_TABLE_LENGTH);
        }
    }
    return new_kv;
}

static struct websocket_kv *websocket_kv_find(struct websocket_kv_table *kv_tab, const char *key)
{
    struct websocket_kv *kv = NULL;
    for(int i = 0; i < kv_tab->kv_use; i++)
    {
        if(kv_tab->kv_tab[i].key && !strcmp(kv_tab->kv_tab[i].key, key))
            return &kv_tab->kv_tab[i];
    }

    if(kv_tab->kv_use >= kv_tab->kv_total)
    {
        if(websocket_kv_table_alloc(kv_tab))
            kv = &kv_tab->kv_tab[kv_tab->kv_use];
    }
    else
    {
        kv = &kv_tab->kv_tab[kv_tab->kv_use];
    }

    return kv;
}

int websocket_kv_put(struct websocket_kv_table *kv_tab, const char *key, const char *value)
{
    int res = -WEBSOCKET_ERROR;
    struct websocket_kv *kv = websocket_kv_find(kv_tab, key);
    if(kv)
    {
        struct websocket_kv kv_tmp = { 0 };
        kv_tmp.key = WEBSOCKET_STRDUP(key);
        kv_tmp.value = WEBSOCKET_STRDUP(value);
        if(kv_tmp.key && kv_tmp.value)
        {
            if(kv->key == NULL)
            {
                kv_tab->kv_use += 1;
            }
            WEBSOCKET_FREE(kv->key);
            WEBSOCKET_FREE(kv->value);
            *kv = kv_tmp;
            res = WEBSOCKET_OK;
        }
        else
        {
            WEBSOCKET_FREE(kv_tmp.key);
            WEBSOCKET_FREE(kv_tmp.value);
        }
    }

    return res;
}

int app_websocket_init(struct app_websocket *websocket)
{
    int res = -WEBSOCKET_ERROR;
    char *cache_buf = NULL;
    pthread_mutex_t lock;
    int success = (
        (websocket) &&
        (websocket->websocket_session = WEBSOCKET_MALLOC(sizeof(struct websocket))) &&
        (cache_buf = WEBSOCKET_MALLOC(WEBSOCKET_SERVICE_CACHE_SIZE_MAX)) &&
        (!pthread_mutex_init(&lock, NULL))
    );

    if (success)
    {
        WEBSOCKET_MEMSET(websocket->websocket_session, 0, sizeof(struct websocket));
        ws_list_init(&websocket->websocket_session->node);
        websocket->websocket_session->cache.buf = cache_buf;
        websocket->websocket_session->lock = lock;
        websocket->websocket_session->app_websocket = websocket;
        websocket->websocket_session->state = WEBSOCKET_STATE_INIT;
        websocket->websocket_session->cache.length = WEBSOCKET_SERVICE_CACHE_SIZE_MAX;
        websocket->websocket_session->cache.recv_index = 0;
        WEBSOCKET_MEMSET(websocket->websocket_session->cache.buf, 0, WEBSOCKET_SERVICE_CACHE_SIZE_MAX);
    }
    else 
    {
        WEBSOCKET_FREE(cache_buf);
        WEBSOCKET_FREE(websocket->websocket_session);
    }

    return success ? WEBSOCKET_OK : -WEBSOCKET_ERROR;
}

void app_websocket_deinit(struct app_websocket *websocket)
{
    if (websocket && websocket->websocket_session)
    {
        ws_list_remove(&websocket->websocket_session->node);
        if (websocket->websocket_session->cache.buf)
            WEBSOCKET_FREE(websocket->websocket_session->cache.buf);
        if (websocket->websocket_session->url)
            WEBSOCKET_FREE((void *)websocket->websocket_session->url);
        if (websocket->websocket_session->subprotocol)
            WEBSOCKET_FREE((void *)websocket->websocket_session->subprotocol);
        if (websocket->websocket_session->server_status.status.reason)
            WEBSOCKET_FREE(websocket->websocket_session->server_status.status.reason);
        if (websocket->websocket_session->client_status.status.reason)
            WEBSOCKET_FREE(websocket->websocket_session->client_status.status.reason);

        if (websocket->websocket_session->kv.kv_tab)
        {
            for (int i = 0; i < websocket->websocket_session->kv.kv_use; i++)
            {
                WEBSOCKET_FREE(websocket->websocket_session->kv.kv_tab[i].key);
                WEBSOCKET_FREE(websocket->websocket_session->kv.kv_tab[i].value);
            }
            WEBSOCKET_FREE(websocket->websocket_session->kv.kv_tab);
        }
        WEBSOCKET_MEMSET(websocket->websocket_session, 0, sizeof(struct websocket));
        WEBSOCKET_FREE(websocket->websocket_session);
    }
}

int app_websocket_set_url(struct app_websocket *websocket, const char *url)
{
    WEBSOCKET_FREE(websocket->websocket_session->url);
    websocket->websocket_session->url = WEBSOCKET_STRDUP(url);
    return websocket->websocket_session->url ? 0 : -1;
}

int app_websocket_set_subprotocol(struct app_websocket *websocket, const char *subprotocol)
{
    WEBSOCKET_FREE(websocket->websocket_session->subprotocol);
    websocket->websocket_session->subprotocol = WEBSOCKET_STRDUP(subprotocol);
    return websocket->websocket_session->subprotocol ? 0 : -1;
}

int app_websocket_set_close_reason(struct app_websocket *websocket, websocket_status_code_t code, const char *reason)
{
    int res = WEBSOCKET_OK;
    if (websocket != NULL)
    {
        websocket->websocket_session->client_status.status.status_code = code;
        if (reason != NULL)
        {
            if (websocket->websocket_session->client_status.status.reason)
            {
                WEBSOCKET_FREE(websocket->websocket_session->client_status.status.reason);
            }

            websocket->websocket_session->client_status.status.reason = WEBSOCKET_STRDUP(reason);
            if (websocket->websocket_session->client_status.status.reason == NULL)
            {
                res = -WEBSOCKET_ERROR;
            }
        }
    }

    return res;
}

int app_websocket_get_close_reason(struct app_websocket *websocket, websocket_status_code_t *code, const char **reason)
{
    int res = -WEBSOCKET_OK;
    if (websocket)
    {
        *code = websocket->websocket_session->server_status.status.status_code;
        *reason = websocket->websocket_session->server_status.status.reason;
        res = -WEBSOCKET_ERROR;
    }
    return res;
}

int app_websocket_add_header(struct app_websocket *websocket, const char *key, const char *value)
{
    int err = WEBSOCKET_OK;
    if(!websocket->websocket_session->kv.kv_tab)
    {
        if(websocket_kv_table_init(&websocket->websocket_session->kv, WEBSOCKET_SERVICE_KV_TABLE_LENGTH) != WEBSOCKET_SERVICE_KV_TABLE_LENGTH)
        {
            err = -WEBSOCKET_ERROR;
        }
    }

    if(!err)
    {
        websocket_kv_put(&websocket->websocket_session->kv, key, value);
    }

    return err;
}

int app_websocket_connect_server(struct app_websocket *websocket)
{
    /* send message */
    struct websocket * ws = websocket->websocket_session;
    pthread_mutex_lock(&worker.lock);
    ws_list_remove(&ws->node);
    ws_list_insert_before(&websocket_session_list, &ws->node);
    write(worker.pipe[1], "1", 1);
    pthread_mutex_unlock(&worker.lock);
    return 0;
}

int app_websocket_disconnect_server(struct app_websocket *websocket)
{
    struct websocket *ws = websocket->websocket_session;
    int err = app_websocket_enter_critical(ws);
    if (err == WEBSOCKET_OK)
    {
        if (ws)
        {
            ws->state = WEBSOCKET_STATE_CLOSE;
            ws->app_websocket = NULL;
            write(worker.pipe[1], "0", 1);
        }
        app_websocket_exit_critical(ws);
    }

    return err;
}

static int websocket_control_frame_handle(struct app_websocket *websocket)
{
    struct websocket_frame_info *info = &websocket->websocket_session->session.info;
    struct websocket *ws = websocket->websocket_session;
    char cache[128];
    int read_len = 0;
    int res = WEBSOCKET_OK;
    switch(info->frame_type)
    {
    case WEBSOCKET_PING_FRAME:
        if (info->remain_len > 0)
        {
            read_len = websocket_read(&ws->session, cache, info->remain_len);
            if (read_len > 0)
                res = websocket_send_pong(&ws->session, cache, read_len);
            else
                res = -WEBSOCKET_ERROR;
        }
        else
        {
            res = websocket_send_pong(&ws->session, NULL, 0);
        }
        break;
    case WEBSOCKET_PONG_FRAME:
        if (info->remain_len > 0)
        {
            res = websocket_read(&ws->session, cache, info->remain_len);
        }
        break;
    case WEBSOCKET_CLOSE_FRAME:
        if (info->remain_len > 0)
        {
            read_len = websocket_read(&ws->session, cache, info->remain_len);
            if (read_len <= 0)
            {
                res = -WEBSOCKET_ERROR;
                break;
            }
            cache[read_len] = '\0';

            if (read_len > 2)
                websocket_send_close(&ws->session, WEBSOCKET_STATUS_CLOSE_NORMAL, &cache[2], read_len - 2);
            else
                websocket_send_close(&ws->session, WEBSOCKET_STATUS_CLOSE_NORMAL, NULL, 0);
        }
        break;
    default:
        break;
    }

    return res;
}

static int app_websocket_control_frame_handle(struct app_websocket *websocket)
{
    struct websocket_frame_info *info = &websocket->websocket_session->session.info;
    struct websocket_session *session = &websocket->websocket_session->session;
    char cache[128] = { 0 };
    int read_length = 0;
    int res = WEBSOCKET_OK;
    switch (info->frame_type)
    {
    case WEBSOCKET_PING_FRAME:
        if (info->remain_len != 0)
        {
            read_length = websocket_read(session, cache, info->remain_len);
            if (read_length > 0)
            {
                if (websocket_send_pong(session, cache, read_length) != WEBSOCKET_OK)
                {
                    res = -WEBSOCKET_ERROR;
                }
            }
            else
            {
                res = -WEBSOCKET_ERROR;
            }
        }
        else
        {
            if (websocket_send_pong(session, NULL, 0) != WEBSOCKET_OK)
            {
                res = -WEBSOCKET_ERROR;
            }
        }
        break;
    case WEBSOCKET_PONG_FRAME:
        if (info->remain_len != 0)
        {
            read_length = websocket_read(session, cache, info->remain_len);
            if (read_length <= 0)
            {
                res = -WEBSOCKET_ERROR;
            }
        }
        break;
    case WEBSOCKET_CLOSE_FRAME:
        if (info->remain_len != 0)
        {
            read_length = websocket_read(session, cache, info->remain_len);
            if (read_length <= 0)
            {
                res = -WEBSOCKET_ERROR;
                break;
            }

            cache[read_length] = '\0';

            if (read_length > 2)
            {
                websocket_send_close(session, WEBSOCKET_STATUS_CLOSE_NORMAL, &cache[2], read_length - 2);
                if (websocket->websocket_session->server_status.status.reason)
                {
                    WEBSOCKET_FREE(websocket->websocket_session->server_status.status.reason);
                }

                websocket->websocket_session->server_status.status.reason = WEBSOCKET_STRDUP(&cache[2]);
            }
            else
            {
                websocket_send_close(session, WEBSOCKET_STATUS_CLOSE_NORMAL, NULL, 0);
                websocket->websocket_session->server_status.status.reason = NULL;
            }
            memcpy(&websocket->websocket_session->server_status.status.status_code, cache, 2);
            websocket->websocket_session->server_status.status.status_code = ntohs(websocket->websocket_session->server_status.status.status_code);
        }
        websocket->websocket_session->server_status.server_close = 1;
        break;
    default:
        break;
    }

    if (res != WEBSOCKET_OK)
    {
        websocket->websocket_session->error_reason = "Error reading data!!";
    }

    return res;
}

static int app_websocket_recive_data(struct app_websocket *websocket)
{
    struct websocket *app_session = websocket->websocket_session;
    struct websocket_session *session = &websocket->websocket_session->session;
    struct websocket_frame_info *info = &session->info;
    int read_length = 0;
    int res = WEBSOCKET_OK;
    unsigned int free_spacce = app_session->cache.length - app_session->cache.recv_index;

    if ((unsigned int)info->remain_len >= free_spacce)
    {
        char *new_addr;
        int append_mem_cnt = (info->remain_len - free_spacce) / WEBSOCKET_SERVICE_CACHE_SIZE_MAX;
        unsigned int buf_size = app_session->cache.length + (append_mem_cnt + 1) * WEBSOCKET_SERVICE_CACHE_SIZE_MAX;

        if (buf_size <= WEBSOCKET_SERVICE_CACHE_SIZE_MAX)
        {
            new_addr = WEBSOCKET_REALLOC(app_session->cache.buf, buf_size);
            if (new_addr != NULL)
            {
                app_session->cache.length = buf_size;
                app_session->cache.buf = new_addr;
            }
            else
            {
                res = -WEBSOCKET_ERROR;
            }
        }
        else
        {
            res = -WEBSOCKET_ERROR;
        }

        if (res != WEBSOCKET_OK)
        {
            app_session->error_reason = "Resource Starvation!!";
        }
    }

    if (res == WEBSOCKET_OK)
    {
        do
        {
            if ((read_length = websocket_read(session, app_session->cache.buf + app_session->recv_size, info->remain_len)) > 0)
            {
                app_session->recv_size += read_length;
            }
            else
            {
                app_session->error_reason = "Error reading data!!";
                res = -WEBSOCKET_ERROR;
                break;
            }
        }
        while (info->remain_len);
    }

    return res;
}

int app_websocket_read_data(struct app_websocket *websocket, struct app_websocket_frame *frame)
{
    struct websocket_frame_info *info;
    struct websocket_session *session = &websocket->websocket_session->session;
    struct websocket *app_session = websocket->websocket_session;
    int res = -WEBSOCKET_ERROR;

    res = websocket_get_block_info_raw(session);
    if (res == WEBSOCKET_OK && frame)
    {
        info = &session->info;
        if (info->frame_type == WEBSOCKET_PING_FRAME || info->frame_type == WEBSOCKET_PONG_FRAME \
                || info->frame_type == WEBSOCKET_CLOSE_FRAME)
        {
            res = app_websocket_control_frame_handle(websocket);
        }
        else if (info->frame_type == WEBSOCKET_CONTINUE_FRAME || info->is_slice)
        {
            res = app_websocket_recive_data(websocket);
            if (!info->is_slice && res == WEBSOCKET_OK)
            {
                app_session->cache.buf[app_session->recv_size] = '\0';
                frame->data = app_session->cache.buf;
                frame->length = app_session->recv_size;
                frame->type = info->frame_type;
                res = app_session->recv_size;
                app_session->recv_size = 0;
            }
        }
        else
        {
            res = app_websocket_recive_data(websocket);
            if (res == WEBSOCKET_OK)
            {
                app_session->cache.buf[app_session->recv_size] = '\0';
                frame->data = app_session->cache.buf;
                frame->length = app_session->recv_size;
                frame->type = info->frame_type;
                res = app_session->recv_size;
                app_session->recv_size = 0;
            }
        }
    }
    else
    {
        websocket->websocket_session->error_reason = "Error reading data!!";
    }
    return res;
}

int app_websocket_write_data(struct app_websocket *websocket, struct app_websocket_frame *frame)
{
    return websocket_write(&websocket->websocket_session->session, frame->data, frame->length, frame->type);
}

void app_websocket_message_event(struct app_websocket *websocket, int (*onmessage)(struct app_websocket *ws))
{
    if(websocket && websocket->websocket_session)
        websocket->websocket_session->callback.onmessage = onmessage;
}

void app_websocket_open_event(struct app_websocket *websocket, int (*onopen)(struct app_websocket *ws))
{
    if(websocket && websocket->websocket_session)
        websocket->websocket_session->callback.onopen = onopen;
}

void app_websocket_close_event(struct app_websocket *websocket, int (*onclose)(struct app_websocket *ws))
{
    if(websocket && websocket->websocket_session)
        websocket->websocket_session->callback.onclose = onclose;
}

void app_websocket_error_event(struct app_websocket *websocket, int (*onerror)(struct app_websocket *ws))
{
    if(websocket && websocket->websocket_session)
        websocket->websocket_session->callback.onerror = onerror;
}