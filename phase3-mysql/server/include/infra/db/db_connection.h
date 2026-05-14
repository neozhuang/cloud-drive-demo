#ifndef CLOUD_DRIVE_DB_CONNECTION_H
#define CLOUD_DRIVE_DB_CONNECTION_H

#include <mysql/mysql.h>

#include "config/config.h"

int db_library_init(void);
void db_library_end(void);

MYSQL *db_connect(const MysqlConfig *config);
int db_ensure_schema(const MysqlConfig *config);
void db_close(MYSQL *mysql);

#endif
