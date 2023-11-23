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

static const char *fram_map[11] = {"continue", "text", "bin", "", "", "","", "", "close", "ping", "pong"};

static int onmessage(struct websocket *ws)
{
    struct websocket_frame frame;
    int len = websocket_read_data(ws, &frame);
    if(len > 0)
    {
        printf("frame type is: %s frame, fram data is: %s.\n", fram_map[frame.type], (char *)frame.data);
    }
    else if(len == 0){
        printf("frame type is: %s ", fram_map[frame.type]);
    }
    else {
        printf("recv error!!!!!!!\n");
    }

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

    int success = (
        (websocket_init(&ws) == WEBSOCKET_OK) &&
        (websocket_add_header(&ws, "Origin", "http://coolaf.com") == WEBSOCKET_OK) &&
        (websocket_set_url(&ws, "ws://82.157.123.54:9010/ajaxchattest") == WEBSOCKET_OK) &&
        (websocket_connect_server(&ws) == WEBSOCKET_OK)
    );

    if(success)
    {
        websocket_message_event(&ws, onmessage);
        websocket_open_event(&ws, onopen);
    }

    while(success)
    {
        fgets(cmdline,CMDLINE_MAX,stdin);
        cmdline[strlen(cmdline)-1]='\0';
        printf("cmdline len = %ld\n", strlen(cmdline));
        if(strcmp(cmdline, "exit") == 0)
        {
            websocket_disconnect_server(&ws);
            websocket_deinit(&ws);
        }
        else
        {
            struct websocket_frame frame;
            frame.data = cmdline;
            frame.length = strlen(cmdline);
            frame.type = WEBSOCKET_TEXT_FRAME;
            if (websocket_write_data(&ws, &frame) < 0)
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

