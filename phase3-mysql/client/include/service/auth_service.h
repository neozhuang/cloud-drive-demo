#ifndef CLIENT_SERVICE_AUTH_SERVICE_H
#define CLIENT_SERVICE_AUTH_SERVICE_H

#include "domain/session.h"
#include "protocol/protocol.h"

int client_user_login(ClientContext* context);
int client_user_register(ClientContext* context);
int client_user_delete(ClientContext* context);

#endif
