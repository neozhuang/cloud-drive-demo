#ifndef CLOUD_DRIVE_SERVICE_AUTH_SERVICE_H
#define CLOUD_DRIVE_SERVICE_AUTH_SERVICE_H

#include <mysql/mysql.h>

#include "dispatch/packet_router.h"

int auth_handle_register_request(const PacketTaskContext *context, MYSQL *mysql);
void auth_handle_register(const PacketTaskContext *context, MYSQL *mysql,
                          uint16_t *status, const char **message);
void auth_handle_login_request(const PacketTaskContext *context, MYSQL *mysql,
                               uint16_t *status, const char **message);
void auth_handle_login(const PacketTaskContext *context, MYSQL *mysql,
                       uint16_t *status, const char **message);
void auth_handle_delete_request(const PacketTaskContext *context, MYSQL *mysql,
                                uint16_t *status, const char **message);
void auth_handle_delete(const PacketTaskContext *context, MYSQL *mysql,
                        uint16_t *status, const char **message);
void auth_handle_logout(const PacketTaskContext *context, MYSQL *mysql,
                        uint16_t *status, const char **message);

#endif