/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date          Author       Notes
 * 2023-7-3      tzy          first implementation
 */

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <poll.h>
#include <errno.h>
#include "websocket.h"
#include "websocket_service.h"

#define WEBSOCKAET_APPEND_CACHE_SIZE        (1024)

static int websocket_enter_critical(struct websocket *ws)
{
    return ws ? pthread_mutex_lock(&ws->lock) : -EINVAL;
}

static int websocket_exit_critical(struct websocket *ws)
{
    return ws ? pthread_mutex_unlock(&ws->lock) : -EINVAL;
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

int websocket_init(struct websocket *ws)
{
    int err = -WEBSOCKET_ERROR;
    memset(ws, 0, sizeof(struct websocket));
    ws->cache.buf = WEBSOCKET_MALLOC(WEBSOCKET_SERVICE_CACHE_SIZE);
    if(ws->cache.buf)
    {
        ws->cache.length = WEBSOCKET_SERVICE_CACHE_SIZE;
        ws->cache.recv_index = 0;
        websocket_session_init(&ws->session);
        pthread_mutex_init(&ws->lock, NULL);
        err = WEBSOCKET_OK;
    }
    return err;
}

void websocket_deinit(struct websocket *ws)
{
    WEBSOCKET_FREE(ws->url);
    WEBSOCKET_FREE(ws->subprotocol);
    WEBSOCKET_FREE(ws->cache.buf);
    websocket_kv_table_deinit(&ws->kv);
    pthread_mutex_destroy(&ws->lock);
}

int websocket_set_url(struct websocket *ws, const char *url)
{
    WEBSOCKET_FREE(ws->url);
    ws->url = WEBSOCKET_STRDUP(url);
    return ws->url ? 0 : -1;
}

int websocket_set_subprotocol(struct websocket *ws, const char *subprotocol)
{
    WEBSOCKET_FREE(ws->subprotocol);
    ws->subprotocol = WEBSOCKET_STRDUP(subprotocol);
    return ws->subprotocol ? 0 : -1;
}

int websocket_add_header(struct websocket *ws, const char *key, const char *value)
{
    int err = WEBSOCKET_OK;
    if(!ws->kv.kv_tab)
    {
        if(websocket_kv_table_init(&ws->kv, WEBSOCKET_SERVICE_KV_TABLE_LENGTH) != WEBSOCKET_SERVICE_KV_TABLE_LENGTH)
        {
            err = -WEBSOCKET_ERROR;
        }
    }

    if(!err)
    {
        websocket_kv_put(&ws->kv, key, value);
    }

    return err;
}

static void *websocket_thread(void *prma)
{
    struct websocket *ws = prma;
    struct pollfd fds;
    int nfd;

    for(int i = 0; i < ws->kv.kv_use; i++)
    {
        websocket_header_fields_add(&ws->session, "%s: %s\r\n", ws->kv.kv_tab[i].key, ws->kv.kv_tab[i].value);
    }

    if(websocket_connect(&ws->session, ws->url, ws->subprotocol) != WEBSOCKET_OK)
        goto __exit;

    if(ws->callback.onopen)
        ws->callback.onopen(ws);

    fds.fd = ws->session.socket_fd;
    fds.events = POLLIN | POLLERR;
    while(1)
    {
        fds.revents = 0;
        nfd = poll(&fds, 1, -1);
        if(nfd < 0)
        {
            if (errno == ERANGE)
                continue;
        }

        if(fds.revents & POLLIN)
        {
            if(websocket_enter_critical(ws) == 0)
            {
                if(ws->callback.onmessage)
                    ws->callback.onmessage(ws);
                websocket_exit_critical(ws);
            }
        }
        else if(fds.revents & POLLERR)
        {
            if(websocket_enter_critical(ws) == 0)
            {
                if(ws->callback.onerror)
                    ws->callback.onerror(ws);
                websocket_exit_critical(ws);
            }
            break;
        }
    }
__exit:
    pthread_exit(NULL);
    return (void *)ws;
}

int websocket_connect_server(struct websocket *ws)
{
    pthread_t tid;
    int err =  pthread_create(&tid, NULL, websocket_thread, ws);
    if(err)
    {
        fprintf(stderr, "pthread_create():%s\n", strerror(err));
    }
    ws->tid = tid;
    pthread_mutex_init(&ws->lock, NULL);

    return err;
}

int websocket_disconnect_server(struct websocket *ws)
{
    int err = -WEBSOCKET_ERROR;
    if((err = websocket_enter_critical(ws)) == 0)
    {
        err = pthread_detach(ws->tid);
        if(err)
        {
            fprintf(stderr, "pthread_detach():%s\n", strerror(err));
            exit(1);
        }
        printf("websocket_disconnect_server\n");
        websocket_exit_critical(ws);
        websocket_disconnect(&ws->session);
    }

    return err;
}

static int websocket_control_frame_handle(struct websocket *ws)
{
    struct websocket_frame_info *info = &ws->session.info;
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

static int websocket_read_data_frame(struct websocket *ws)
{
    int ret = WEBSOCKET_OK;
    struct websocket_frame_info *info = &ws->session.info;
    int free_mem = ws->cache.length - ws->cache.recv_index;
    int append_cnt;
    int cache_size;
    if(info->remain_len >= free_mem)
    {
        append_cnt = (info->remain_len - free_mem) / WEBSOCKAET_APPEND_CACHE_SIZE + 1;
        cache_size = ws->cache.length + append_cnt * WEBSOCKAET_APPEND_CACHE_SIZE;
        if(cache_size < WEBSOCKET_SERVICE_CACHE_SIZE_MAX)
        {
            char *temp = WEBSOCKET_REALLOC((void *)ws->cache.buf, cache_size);
            if(temp)
            {
                ws->cache.buf = temp;
                ret = -WEBSOCKET_ERROR;
            }
        }
        else
        {
            ret = -WEBSOCKET_ERROR;
        }
    }

    if(ret == WEBSOCKET_OK)
    {
        if((ret = websocket_read(&ws->session, ws->cache.buf, info->remain_len)) > 0)
            ws->cache.recv_index += ret;
    }

    return ret;
}

int websocket_read_data(struct websocket *ws, struct websocket_frame *frame)
{
    struct websocket_frame_info *info = &ws->session.info;
    int res = websocket_get_block_info_raw(&ws->session);
    if(!res && frame)
    {
        if(info->frame_type == WEBSOCKET_CLOSE_FRAME || info->frame_type == WEBSOCKET_PING_FRAME \
                    || info->frame_type == WEBSOCKET_PONG_FRAME)
        {
            res = websocket_control_frame_handle(ws);
        }
        else
        {
            res = websocket_read_data_frame(ws);
            if(res > 0)
            {
                frame->data = ws->cache.buf;
                *((char *)frame->data + res) = '\0';
                frame->length = res;
            }
        }
        frame->type = info->frame_type;
    }

    return res;
}

int websocket_write_data(struct websocket *ws, struct websocket_frame *frame)
{
    return websocket_write(&ws->session, frame->data, frame->length, frame->type);
}

void websocket_message_event(struct websocket *ws, int (*onmessage)(struct websocket *ws))
{
    if(ws)
        ws->callback.onmessage = onmessage;
}

void websocket_open_event(struct websocket *ws, int (*onopen)(struct websocket *ws))
{
    if(ws)
        ws->callback.onopen = onopen;
}

void websocket_close_event(struct websocket *ws, int (*onclose)(struct websocket *ws))
{
    if(ws)
        ws->callback.onclose = onclose;
}

void websocket_error_event(struct websocket *ws, int (*onerror)(struct websocket *ws))
{
    if(ws)
        ws->callback.onerror = onerror;
}