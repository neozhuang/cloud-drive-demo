#include "dispatch/packet_router.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "service/auth_service.h"

#include "transport/packet_io.h"

static void handle_quit_packet(const PacketTaskContext *context, uint16_t *status,
                               const char **message)
{
    (void)context;

    *status = PROTOCOL_STATUS_OK;
    *message = "BYE";
}

static void handle_pwd_packet(const PacketTaskContext *context, MYSQL *mysql,
                              uint16_t *status, const char **message)
{
    (void)context;
    (void)mysql;

    *status = PROTOCOL_STATUS_INTERNAL_ERROR;
    *message = "pwd handler not implemented";
}

static void handle_cd_packet(const PacketTaskContext *context, MYSQL *mysql,
                             uint16_t *status, const char **message)
{
    (void)context;
    (void)mysql;

    *status = PROTOCOL_STATUS_INTERNAL_ERROR;
    *message = "cd handler not implemented";
}

static void handle_ls_packet(const PacketTaskContext *context, MYSQL *mysql,
                             uint16_t *status, const char **message)
{
    (void)context;
    (void)mysql;

    *status = PROTOCOL_STATUS_INTERNAL_ERROR;
    *message = "ls handler not implemented";
}

static void handle_ll_packet(const PacketTaskContext *context, MYSQL *mysql,
                             uint16_t *status, const char **message)
{
    (void)context;
    (void)mysql;

    *status = PROTOCOL_STATUS_INTERNAL_ERROR;
    *message = "ll handler not implemented";
}

static void handle_tree_packet(const PacketTaskContext *context, MYSQL *mysql,
                               uint16_t *status, const char **message)
{
    (void)context;
    (void)mysql;

    *status = PROTOCOL_STATUS_INTERNAL_ERROR;
    *message = "tree handler not implemented";
}

static void handle_mkdir_packet(const PacketTaskContext *context, MYSQL *mysql,
                                uint16_t *status, const char **message)
{
    (void)context;
    (void)mysql;

    *status = PROTOCOL_STATUS_INTERNAL_ERROR;
    *message = "mkdir handler not implemented";
}

static void handle_rmdir_packet(const PacketTaskContext *context, MYSQL *mysql,
                                uint16_t *status, const char **message)
{
    (void)context;
    (void)mysql;

    *status = PROTOCOL_STATUS_INTERNAL_ERROR;
    *message = "rmdir handler not implemented";
}

static void handle_rm_packet(const PacketTaskContext *context, MYSQL *mysql,
                             uint16_t *status, const char **message)
{
    (void)context;
    (void)mysql;

    *status = PROTOCOL_STATUS_INTERNAL_ERROR;
    *message = "rm handler not implemented";
}

static void handle_cat_packet(const PacketTaskContext *context, MYSQL *mysql,
                              uint16_t *status, const char **message)
{
    (void)context;
    (void)mysql;

    *status = PROTOCOL_STATUS_INTERNAL_ERROR;
    *message = "cat handler not implemented";
}

static void handle_upload_packet(const PacketTaskContext *context, MYSQL *mysql,
                                 uint16_t *status, const char **message)
{
    (void)context;
    (void)mysql;

    *status = PROTOCOL_STATUS_INTERNAL_ERROR;
    *message = "upload handler not implemented";
}

static void handle_download_packet(const PacketTaskContext *context, MYSQL *mysql,
                                   uint16_t *status, const char **message)
{
    (void)context;
    (void)mysql;

    *status = PROTOCOL_STATUS_INTERNAL_ERROR;
    *message = "download handler not implemented";
}

static void handle_invalid_packet(const PacketTaskContext *context, uint16_t *status,
                                  const char **message)
{
    (void)context;

    *status = PROTOCOL_STATUS_INTERNAL_ERROR;
    *message = "unsupported packet type";
}

PacketTaskContext *packet_task_context_create(int client_fd,
                                              const ProtocolPacket *packet)
{
    PacketTaskContext *context;

    if (packet == NULL) {
        errno = EINVAL;
        return NULL;
    }

    context = (PacketTaskContext *)malloc(sizeof(PacketTaskContext));
    if (context == NULL) {
        return NULL;
    }

    context->client_fd = client_fd;
    context->packet = *packet;
    return context;
}

void packet_task_context_destroy(PacketTaskContext *context)
{
    free(context);
}

void packet_router_handle_task(void *arg, MYSQL *mysql)
{
    PacketTaskContext *context = (PacketTaskContext *)arg;
    unsigned long worker_id;
    uint16_t status = PROTOCOL_STATUS_INTERNAL_ERROR;
    const char *message = "handler not implemented";
    uint16_t request_type;

    if (context == NULL) {
        return;
    }

    worker_id = (unsigned long)pthread_self();
    printf("Worker %lu handling %s request with %u payload bytes from client fd %d\n",
           worker_id, protocol_packet_type_name(context->packet.type),
           context->packet.payload_length, context->client_fd);

    request_type = context->packet.type;

    switch (request_type) {
    case PROTOCOL_PACKET_QUIT:
        handle_quit_packet(context, &status, &message);
        break;
    case PROTOCOL_PACKET_REGISTER_REQUEST:
        auth_handle_register_request(context, mysql);
        break;
    case PROTOCOL_PACKET_REGISTER:
        auth_handle_register(context, mysql, &status, &message);
        break;
    case PROTOCOL_PACKET_LOGIN_REQUEST:
        auth_handle_login_request(context, mysql, &status, &message);
        break;
    case PROTOCOL_PACKET_LOGIN:
        auth_handle_login(context, mysql, &status, &message);
        break;
    case PROTOCOL_PACKET_DELETE_REQUEST:
        auth_handle_delete_request(context, mysql, &status, &message);
        break;
    case PROTOCOL_PACKET_DELETE:
        auth_handle_delete(context, mysql, &status, &message);
        break;
    case PROTOCOL_PACKET_LOGOUT:
        auth_handle_logout(context, mysql, &status, &message);
        break;
    case PROTOCOL_PACKET_PWD:
        handle_pwd_packet(context, mysql, &status, &message);
        break;
    case PROTOCOL_PACKET_CD:
        handle_cd_packet(context, mysql, &status, &message);
        break;
    case PROTOCOL_PACKET_LS:
        handle_ls_packet(context, mysql, &status, &message);
        break;
    case PROTOCOL_PACKET_LL:
        handle_ll_packet(context, mysql, &status, &message);
        break;
    case PROTOCOL_PACKET_TREE:
        handle_tree_packet(context, mysql, &status, &message);
        break;
    case PROTOCOL_PACKET_MKDIR:
        handle_mkdir_packet(context, mysql, &status, &message);
        break;
    case PROTOCOL_PACKET_RMDIR:
        handle_rmdir_packet(context, mysql, &status, &message);
        break;
    case PROTOCOL_PACKET_RM:
        handle_rm_packet(context, mysql, &status, &message);
        break;
    case PROTOCOL_PACKET_CAT:
        handle_cat_packet(context, mysql, &status, &message);
        break;
    case PROTOCOL_PACKET_UPLOAD:
        handle_upload_packet(context, mysql, &status, &message);
        break;
    case PROTOCOL_PACKET_DOWNLOAD:
        handle_download_packet(context, mysql, &status, &message);
        break;
    default:
        handle_invalid_packet(context, &status, &message);
        break;
    }
    packet_task_context_destroy(context);
}