#ifndef CLIENT_TRANSPORT_CONNECTION_H
#define CLIENT_TRANSPORT_CONNECTION_H

#include "domain/session.h"
#include "config/config.h"

int client_net_connect(const ClientServerConfig *config, ClientContext *context);
void client_net_close(int fd);

#endif
