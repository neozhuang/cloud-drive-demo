#include "service/auth_service.h"

#include <crypt.h>
#include <stdio.h>
#include <string.h>

#include "common/utils.h"
#include "domain/session.h"
#include "transport/packet_io.h"
#include "protocol/protocol.h"
#include "ui/term_ui.h"

#define CLIENT_USER_NAME_LEN 64
#define CLIENT_USER_PASSWORD_LEN 128
#define CLIENT_USER_CREDENTIALS_LEN (CLIENT_USER_NAME_LEN + CLIENT_USER_PASSWORD_LEN + 2)

static const char *client_auth_build_password_hash(const char *password,
                                                    const char *salt,
                                                    char *buffer,
                                                    size_t buffer_size)
{
    char *hashed_password;
    size_t hashed_password_length;

    if (password == NULL || salt == NULL || buffer == NULL || buffer_size == 0) {
        return NULL;
    }

    hashed_password = crypt(password, salt);
    if (hashed_password == NULL) {
        return NULL;
    }

    hashed_password_length = strlen(hashed_password);
    if (hashed_password_length >= buffer_size) {
        return NULL;
    }

    memcpy(buffer, hashed_password, hashed_password_length + 1);
    return buffer;
}

static int client_user_read_required(const char *prompt, char *buffer,
                                     size_t buffer_size)
{
    if (client_utils_read_line(prompt, buffer, buffer_size) != 0) {
        puts("输入失败。");
        return -1;
    }

    if (buffer[0] == '\0') {
        puts("输入不能为空。");
        return -1;
    }

    return 0;
}


int client_user_login(ClientContext* context)
{
    char username[CLIENT_USER_NAME_LEN];
    char password[CLIENT_USER_PASSWORD_LEN];
    char credentials[CLIENT_USER_CREDENTIALS_LEN];
    char password_hash[PROTOCOL_MAX_PAYLOAD_SIZE];
    int written_length;

    puts("");
    puts("用户登录");
    if (client_user_read_required("用户名: ", username, sizeof(username)) != 0 ||
        client_user_read_required("密码: ", password, sizeof(password)) != 0) {
        return -1;
    }

    printf("已读取登录信息，用户名: %s\n", username);

    ProtocolPacket packet;
    memset(&packet, 0, sizeof(packet));
    // send login request with username
    packet.type = PROTOCOL_PACKET_LOGIN_REQUEST;
    strcpy((char*)packet.payload, username);
    packet.payload_length = (uint32_t)strlen(username);
    send_packet(context->sockfd, &packet);

    // recv salt and status
    recv_packet(context->sockfd, &packet);
    if (packet.status == PROTOCOL_STATUS_OK) {
        ;
    }else if(packet.status == PROTOCOL_STATUS_USERNAME_NOT_EXIST){
        context->state = CLIENT_STATE_LOGIN_FAILURE;
        printf(COLOR_RED "用户名 %s 不存在！\n" COLOR_RESET, username);
        return -1;
    }else {
        context->state = CLIENT_STATE_LOGIN_FAILURE;
        printf(COLOR_RED "其他错误！\n" COLOR_RESET);
        return -2;
    }

    // send login with username and crypted password
    const char *crypted_password = client_auth_build_password_hash(
        password, (char *)packet.payload, password_hash, sizeof(password_hash));
    if (crypted_password == NULL) {
        puts(COLOR_RED "密码加密失败。" COLOR_RESET);
        return -1;
    }
    written_length = snprintf(credentials, sizeof(credentials), 
                             "%s\n%s", username, crypted_password);

    memset(&packet, 0, sizeof(packet));
    packet.type = PROTOCOL_PACKET_LOGIN;
    packet.payload_length = (uint32_t)written_length;
    memcpy(packet.payload, credentials, (size_t)written_length);
    send_packet(context->sockfd, &packet);

    // recv status
    recv_packet(context->sockfd, &packet);
    if (packet.status == PROTOCOL_STATUS_OK) {
        ;
    }else if(packet.status == PROTOCOL_STATUS_PASSWORD_INCORRECT){
        context->state = CLIENT_STATE_LOGIN_FAILURE;
        printf(COLOR_RED "密码错误！\n" COLOR_RESET);
    }else {
        context->state = CLIENT_STATE_LOGIN_FAILURE;
        printf(COLOR_RED "其他错误！\n" COLOR_RESET);
    }

    return packet.status;
}

int client_user_register(ClientContext* context)
{
    char username[CLIENT_USER_NAME_LEN];
    char password[CLIENT_USER_PASSWORD_LEN];
    char confirm_password[CLIENT_USER_PASSWORD_LEN];
    char credentials[CLIENT_USER_CREDENTIALS_LEN];
    char password_hash[PROTOCOL_MAX_PAYLOAD_SIZE];
    int written_length;

    puts("");
    puts("用户注册");
    if (client_user_read_required("用户名: ", username, sizeof(username)) != 0 ||
        client_user_read_required("密码: ", password, sizeof(password)) != 0 ||
        client_user_read_required("确认密码: ", confirm_password,
                                  sizeof(confirm_password)) != 0) {
        return -1;
    }

    if (strcmp(password, confirm_password) != 0) {
        puts("两次输入的密码不一致。");
        return -1;
    }

    printf("已读取注册信息，用户名: %s\n", username);

    ProtocolPacket packet;
    memset(&packet, 0, sizeof(packet));
    // 1. send register request with username
    packet.type = PROTOCOL_PACKET_REGISTER_REQUEST;
    strcpy((char*)packet.payload, username);
    packet.payload_length = (uint32_t)strlen(username);
    send_packet(context->sockfd, &packet);

    // 2. recv status and salt/error
    recv_packet(context->sockfd, &packet);
    if (packet.status == PROTOCOL_STATUS_OK) {
        ;
    }else if(packet.status == PROTOCOL_STATUS_USERNAME_ALREADY_EXISTS){
        context->state = CLIENT_STATE_REGISTER_FAILURE;
        printf(COLOR_RED "用户名 %s 已存在！\n" COLOR_RESET, username);
        return -1;
    }else {
        context->state = CLIENT_STATE_REGISTER_FAILURE;
        printf(COLOR_RED "其他错误！\n" COLOR_RESET);
        return -2;
    }

    // 3. send register with username and crypted password
    const char *crypted_password = client_auth_build_password_hash(
        password, (char *)packet.payload, password_hash, sizeof(password_hash));
    if (crypted_password == NULL) {
        puts(COLOR_RED "密码加密失败。" COLOR_RESET);
        return -1;
    }
    // trainload = username + \n + crypted_password
    written_length = snprintf(credentials, sizeof(credentials), 
                             "%s\n%s", username, crypted_password);

    memset(&packet, 0, sizeof(packet));
    packet.type = PROTOCOL_PACKET_REGISTER;
    packet.payload_length = (uint32_t)written_length;
    memcpy(packet.payload, credentials, (size_t)written_length);
    send_packet(context->sockfd, &packet);

    // 4. recv status
    recv_packet(context->sockfd, &packet);
    if (packet.status == PROTOCOL_STATUS_OK) {
        ;
    }else {
        context->state = CLIENT_STATE_REGISTER_FAILURE;
        printf(COLOR_RED "其他错误！\n" COLOR_RESET);
    }

    return packet.status;
}

int client_user_delete(ClientContext* context)
{
    char username[CLIENT_USER_NAME_LEN];
    char password[CLIENT_USER_PASSWORD_LEN];
    char credentials[CLIENT_USER_CREDENTIALS_LEN];
    char password_hash[PROTOCOL_MAX_PAYLOAD_SIZE];
    int written_length;

    puts("");
    puts("删除用户");
    if (client_user_read_required("用户名: ", username, sizeof(username)) != 0 ||
        client_user_read_required("密码: ", password, sizeof(password)) != 0) {
        return -1;
    }

    printf("已读取删除用户信息，用户名: %s\n", username);

    ProtocolPacket packet;
    memset(&packet, 0, sizeof(packet));
    // send delete request with username
    packet.type = PROTOCOL_PACKET_DELETE_REQUEST;
    strcpy((char*)packet.payload, username);
    packet.payload_length = (uint32_t)strlen(username);
    send_packet(context->sockfd, &packet);

    // recv salt and status
    recv_packet(context->sockfd, &packet);
    if (packet.status == PROTOCOL_STATUS_OK) {
        ;
    }else if(packet.status == PROTOCOL_STATUS_USERNAME_NOT_EXIST){
        context->state = CLIENT_STATE_LOGIN_FAILURE;
        printf(COLOR_RED "用户名 %s 不存在！\n" COLOR_RESET, username);
        return -1;
    }else {
        context->state = CLIENT_STATE_LOGIN_FAILURE;
        printf(COLOR_RED "其他错误！\n" COLOR_RESET);
        return -2;
    }

    // send delete with username and crypted password
    const char *crypted_password = client_auth_build_password_hash(
        password, (char *)packet.payload, password_hash, sizeof(password_hash));
    if (crypted_password == NULL) {
        puts(COLOR_RED "密码加密失败。" COLOR_RESET);
        return -1;
    }
    // train load: username + \n + crypted_password
    written_length = snprintf(credentials, sizeof(credentials), 
                             "%s\n%s", username, crypted_password);

    memset(&packet, 0, sizeof(packet));
    packet.type = PROTOCOL_PACKET_DELETE;
    packet.payload_length = (uint32_t)written_length;
    memcpy(packet.payload, credentials, (size_t)written_length);
    send_packet(context->sockfd, &packet);

    // recv status
    recv_packet(context->sockfd, &packet);
    if (packet.status == PROTOCOL_STATUS_OK) {
        ;
    }else if(packet.status == PROTOCOL_STATUS_PASSWORD_INCORRECT){
        context->state = CLIENT_STATE_DELETE_FAILURE;
        printf(COLOR_RED "密码错误！\n" COLOR_RESET);
    }else {
        context->state = CLIENT_STATE_DELETE_FAILURE;
        printf(COLOR_RED "其他错误！\n" COLOR_RESET);
    }

    return packet.status;
}
