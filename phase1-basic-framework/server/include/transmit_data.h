#ifndef __TRANSMIT_DATA_H__
#define __TRANSMIT_DATA_H__

#include "packet.h"

int sendn(int sockfd, const void * buff, int len);
int recvn(int sockfd, void * buff, int len);

int send_packet(int sockfd, const packet_t *packet);
int recv_packet(int sockfd, packet_t *packet);

int gets_file_server(int sockfd, const char * command_content, const char * session_path);
int puts_file_server(int sockfd, const char * command_content, const char * session_path);

#endif /* __TRANSMIT_DATA_H__ */
