#ifndef __COMMAND_HANDLERS_H__
#define __COMMAND_HANDLERS_H__

#include "packet.h"

int handle_response(int sockfd, const packet_t *response, char *prompt, const char *parameter);

#endif /* __COMMAND_HANDLERS_H__ */