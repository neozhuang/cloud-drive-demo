#ifndef CLOUD_DRIVE_DISPATCH_PACKET_ROUTER_H
#define CLOUD_DRIVE_DISPATCH_PACKET_ROUTER_H

#include <mysql/mysql.h>

#include "protocol/protocol.h"

typedef struct PacketTaskContext {
    int client_fd;
    ProtocolPacket packet;
} PacketTaskContext;

PacketTaskContext *packet_task_context_create(int client_fd,
                                              const ProtocolPacket *packet);
void packet_task_context_destroy(PacketTaskContext *context);

void packet_router_handle_task(void *arg, MYSQL *mysql);

#endif