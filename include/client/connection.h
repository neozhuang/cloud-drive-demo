#pragma once

int client_connection_open(const char *host,
                           const char *port,
                           int connect_timeout_ms,
                           int io_timeout_ms);
void client_connection_close(int fd);
