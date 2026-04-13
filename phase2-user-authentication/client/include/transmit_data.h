#ifndef __TRANSMIT_DATA_H__
#define __TRANSMIT_DATA_H__

#include "packet.h"

int sendn(int sockfd, const void * buff, int len);
int recvn(int sockfd, void * buff, int len);

int send_packet(int sockfd, const packet_t *packet);
int recv_packet(int sockfd, packet_t *packet);

int gets_file_client(int sockfd, const packet_t *first_packet, const char* filename, const char* download_path);
int puts_file_client(int sockfd, const char* filename);

int recv_stream_content(int sockfd);
int recv_cat_stream(int sockfd, const packet_t *first_packet);

#endif /* __TRANSMIT_DATA_H__ */