#pragma once

#include "common/protocol.h"
#include "server/auth_config.h"

typedef struct {
    int client_fd;
    packet_t packet;
    const auth_config_t *auth_config;
    const char *cloud_drive_root;
} packet_task_t;

typedef struct {
    int epoll_fd;
    int client_fd;
    packet_t packet;
    const auth_config_t *auth_config;
    const char *cloud_drive_root;
} transfer_task_t;

void handle_packet_task(void *arg);

void handle_transfer_task(void* arg);
