#include "websocket_service.h"
#include "websocket.h"
#include <pthread.h>
#include <string.h>

#define WEBSOCKET_SERVICE_CACHE_SIZE        (1024)


int websocket_header(struct websocket *ws, const char *key, const char *value);
int websocket_init(struct websocket *ws);
int websocket_deinit(struct websocket *ws);

int websocket_read_data(struct websocket *ws, struct websocket_frame *frame);
int websocket_write_data(struct websocket *ws, struct websocket_frame *frame);

static void *websocket_thread(void *prma)
{
    struct websocket *ws = prma;

    int err= websocket_connect(&ws->session, ws->url, ws->subprotocol);

    if(err != WEBSOCKET_OK)
        goto __exit;

    while(1)
    {

    }
__exit:

    return NULL;
}

int websocket_connect_server(struct websocket *ws, const char *url, const char *subprotocol)
{
    /* 创建线程 */
    pthread_t pid;
    int err =  pthread_create(&pid, NULL, websocket_thread, ws);
    if(err)
    {
        fprintf(stderr, "pthread_create():%s\n", strerror(err));
        exit(1);
    }
    ws->thread = pid;

    return 0;
}
