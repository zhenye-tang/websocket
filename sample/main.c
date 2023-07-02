#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include "websocket.h"
#include "mbedtls/aes.h"

#define CMDLINE_MAX     (200)
static struct websocket_session session;

static void ctrl_c(int s)
{
    printf("byby!!!\n");
    websocket_send_close(&session, WEBSOCKET_STATUS_CLOSE_NORMAL, "byby!!", strlen("byby!!"));
    websocket_disconnect(&session);
    exit(0);
}

int main(int argc, char *argv[])
{
    static char recv_buf[512];
    char cmdline[CMDLINE_MAX]= {0};
    int len;
    uint32_t recv_buf_len = sizeof(recv_buf);
    uint32_t send_len;

    signal(SIGINT, ctrl_c);

    websocket_session_init(&session);
    websocket_header_fields_add(&session, "Origin: %s\r\n", "http://coolaf.com");
    if (websocket_connect(&session, "ws://82.157.123.54:9010/ajaxchattest", NULL) != WEBSOCKET_OK)
    {
        fprintf(stderr, "websocket_connect error");
        exit(1);
    }

    while(1)
    {
        fgets(cmdline,CMDLINE_MAX,stdin);
        cmdline[strlen(cmdline)-1]='\0';

        if (websocket_write(&session,cmdline, strlen(cmdline), WEBSOCKET_TEXT_FRAME) < 0)
        {
            fprintf(stderr,"write error, please check connect!!!\n");
            exit(1);
        }
        else
        {
            printf("write [%s] success!!!!\n", cmdline);
        }

        do
        {
            websocket_get_block_info(&session);
            if (session.info.remain_len != 0)
            {
                send_len = recv_buf_len > session.info.remain_len ? session.info.remain_len : recv_buf_len;
                if ((len = websocket_read(&session, recv_buf, send_len)) > 0)
                {
                    recv_buf[len] = '\0';
                    printf("recv server message, message length = %d, content is: %s\n", len, recv_buf);
                }
                else
                {
                    fprintf(stderr,"read error!!!!!\n");
                    exit(1);
                }
            }
        }
        while (session.info.is_slice || session.info.remain_len);
    }

    exit(0);
}

