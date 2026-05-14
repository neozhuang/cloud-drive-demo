#include "domain/session.h"

#include <string.h>

int client_context_init(ClientContext *context)
{
    context->sockfd = -1;
    context->state = CLIENT_STATE_INIT;
    strcpy(context->username, "");
    strcpy(context->cwd, "");
    return 0;
}
