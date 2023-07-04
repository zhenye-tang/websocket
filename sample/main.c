#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include "websocket_service.h"

#define CMDLINE_MAX     (200)
static struct websocket ws;

static void ctrl_c(int s)
{
    printf("byby!!!\n");
    websocket_disconnect_server(&ws);
    websocket_deinit(&ws);
    exit(0);
}

static int onmessage(struct websocket *ws)
{
    char buf[200];
    int len = websocket_read(&ws->session, buf, 200);
    buf[len] = '\0';
    printf("onmessage recv:%s\n", buf);
    return len;
}

static int onopen(struct websocket *ws)
{
    printf("connect websocket server success!!!\n");
    return websocket_write(&ws->session,"hello server", strlen("hello server"), WEBSOCKET_TEXT_FRAME);   
}

int main(int argc, char *argv[])
{
    char cmdline[CMDLINE_MAX]= {0};
    signal(SIGINT, ctrl_c);

    int err = websocket_init(&ws);
    if(!err)
        err = websocket_add_header(&ws, "Origin", "http://coolaf.com");
    if(!err)
        err = websocket_set_url(&ws, "ws://82.157.123.54:9010/ajaxchattest");
    if(!err)
        err = websocket_connect_server(&ws);
    if(!err)
        websocket_message_event(&ws, onmessage);
    if(!err)
        websocket_open_event(&ws, onopen);

    while(!err)
    {
        fgets(cmdline,CMDLINE_MAX,stdin);
        cmdline[strlen(cmdline)-1]='\0';

        if(strcmp(cmdline, "exit") == 0)
        {
            websocket_disconnect_server(&ws);
            websocket_deinit(&ws);
        }
        else
        {
            if (websocket_write(&ws.session,cmdline, strlen(cmdline), WEBSOCKET_TEXT_FRAME) < 0)
            {
                fprintf(stderr,"write error, please check connect!!!\n");
                exit(1);
            }
            else
            {
                printf("write [%s] success!!!!\n", cmdline);
            }
        }
    }

    exit(0);
}

