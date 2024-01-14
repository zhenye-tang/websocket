/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date          Author       Notes
 * 2023-1-4      tzy          first implementation
 */

#ifndef __WEBSOCKET_H__
#define __WEBSOCKET_H__

#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#define ws_log      printf

enum WEBSOCKET_STATUS
{
    WEBSOCKET_OK,
    WEBSOCKET_ERROR,
    WEBSOCKET_READ_ERROR,
    WEBSOCKET_WRITE_ERROR,
    WEBSOCKET_NO_HEAD,
    WEBSOCKET_TIMEOUT,
    WEBSOCKET_NOMEM,
    WEBSOCKET_NOSOCKET,
    WEBSOCKET_IS_CONNECT,
    WEBSOCKET_CONNECT_FAILED,
    WEBSOCKET_DISCONNECT,
    WEBSOCKET_NOTSUPPORT_WEBSOCKET,
    WEBSOCKET_NOTSUPPORT_SUBPROTOCOL
};

typedef enum  websocket_status_code
{
    WEBSOCKET_STATUS_CLOSE_NORMAL = 1000,       // Indicates a normal closure, meaning that the purpose for which the connection was established has been fulfilled.
    WEBSOCKET_STATUS_CLOSE_GOING_AWAY,          // Indicates that an endpoint is "going away", such as a server going down or a browser having navigated away from a page.
    WEBSOCKET_STATUS_CLOSE_PROTOCOL_ERROR,      // Indicates that an endpoint is terminating the connection due to a protocol error.
    WEBSOCKET_STATUS_CLOSE_UNSUPPORTED,         // Indicates that an endpoint is terminating the connection because it has received a type of data it cannot accept
    WEBSOCKET_STATUS_RESERVE,                   // Reserved. The specific meaning might be defined in the future.
    WEBSOCKET_STATUS_CLOSE_NO_STATUS,           // Indicates that the expected status code is not received
    WEBSOCKET_STATUS_CLOSE_ABNORMAL,            // Indicates that the expected status code is not received
    WEBSOCKET_STATUS_UNSUPPORTED_DATA,          // Indicates that the endpoint is terminating the connection because the data it receives in the message is inconsistent with the message type, for example, non - UTF - 8[RFC3629] data in the text message
    WEBSOCKET_STATUS_POLICY_VIOLATION,          // Indicates that the endpoint is terminating the connection because it received a message that violated its policy.This is a general status code.When there is no other more appropriate status code(for example, 1003 or 1009) or if you need to hide specific details about the policy
    WEBSOCKET_STATUS_CLOSE_TOO_LARGE,           // Indicates that the endpoint is terminating the connection because the message it received is too large to process
    WEBSOCKET_STATUS_MiSSING_EXTENSION,         // The client expects the server to agree on one or more extensions, but the server does not process them, so the client disconnects
    WEBSOCKET_STATUS_INTERNAL_ERROR,            // The client is prevented from completing the request due to unexpected circumstances, so the server is disconnected
    WEBSOCKET_STATUS_SERVICE_RESTART,           // Server disconnected due to restart
    WEBSOCKET_STATUS_TRY_AGAIN_LATER,           // The server is disconnected for temporary reasons.For example, the server is overloadedand some clients are disconnected
    WEBSOCKET_STATUS_RESERVE_TOO,               // Reserved
    WEBSOCKET_STATUS_TLS_HANDSHAKE = 1015       // Indicates that the connection was closed because the TLS handshake could not be completed(for example, the server certificate could not be verified)
} websocket_status_code_t;

typedef enum websocket_frame_type
{
    WEBSOCKET_CONTINUE_FRAME     =   0x0,
    WEBSOCKET_TEXT_FRAME         =   0x1,
    WEBSOCKET_BIN_FRAME          =   0x2,
    WEBSOCKET_CLOSE_FRAME        =   0x8,
    WEBSOCKET_PING_FRAME         =   0x9,
    WEBSOCKET_PONG_FRAME         =   0xA
} websocket_frame_type_t;

typedef enum websocket_slice
{
    WEBSOCKET_WRITE_FIRST_SLICE,
    WEBSOCKET_WRITE_MIDDLE_SLICE,
    WEBSOCKET_WRITE_END_SLICE
} websocket_slice_t;

struct websocket_frame_info
{
    uint64_t total_len;
    uint64_t remain_len;
    websocket_frame_type_t frame_type;
    uint32_t is_slice;
};

struct websocket_session
{
    int socket_fd;
    int is_tls;
    char *subprotocol;
    char *cache;
    size_t cache_len;
    size_t head_len;
    unsigned char key[36];
    struct websocket_frame_info info;
    void *tls_session;
};

int websocket_session_init(struct websocket_session *session);
int websocket_connect(struct websocket_session *session, const char *url, const char *subprotocol);
int websocket_disconnect(struct websocket_session *session);
int websocket_write(struct websocket_session *session, const void *buf, size_t length, websocket_frame_type_t opcode);
int websocket_write_slice(struct websocket_session *session, const void *buf, size_t length, websocket_frame_type_t opcode, websocket_slice_t slice_type);
int websocket_read(struct websocket_session *session, void *buf, size_t length);
int websocket_get_block_info(struct websocket_session *session);
int websocket_get_block_info_raw(struct websocket_session *session);
int websocket_set_timeout(struct websocket_session *session, int second);
int websocket_header_fields_add(struct websocket_session *session, const char *fmt, ...);

/* control frame api */
int websocket_send_ping(struct websocket_session *session, const char *buf, char length);
int websocket_send_pong(struct websocket_session *session, const char *buf, char length);
int websocket_send_close(struct websocket_session *session, websocket_status_code_t status_code, const char *buf, char length);

/* port api */
void *ws_malloc(size_t size);
void ws_free(void *size);
char *ws_strdup(const char *s);
void *ws_memset(void *s, int c, size_t count);
void *ws_memcpy(void *dst, const void *src, size_t count);
int ws_base64_encode(unsigned char *dst, int *dlen, unsigned char *src, int slen);
void ws_sha1(unsigned char *input, size_t ilen, unsigned char output[20]);
void ws_srand_key(unsigned char *buf, int len);
void *ws_memmove(void *dest, const void *src, size_t n);

#ifdef __cplusplus
}
#endif

#endif //__WEBSOCKET_H__
