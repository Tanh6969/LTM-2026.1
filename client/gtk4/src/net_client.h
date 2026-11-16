#ifndef LTM_NET_CLIENT_H
#define LTM_NET_CLIENT_H

#include <stdint.h>

typedef struct NetClient NetClient;

typedef struct {
    uint16_t status;
    char *body;
    char *error;
} NetReply;

NetClient *net_client_new(const char *host, uint16_t port);
void net_client_free(NetClient *client);
NetReply net_client_request(NetClient *client, uint16_t type, const char *json);
void net_reply_clear(NetReply *reply);
void net_client_set_session(NetClient *client, const char *session_id);
void net_client_set_push_handler(NetClient *client, void (*handler)(const char *, void *), void *data);
void net_client_poll(NetClient *client);

#endif
