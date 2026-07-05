#pragma once

typedef enum {
    DAO_OK = 0,

    DAO_NOT_FOUND = 1,
    DAO_ALREADY_EXISTS = 2,
    DAO_TYPE_MISMATCH = 3,
    DAO_SHOULD_DELETE_PHYSICAL = 4,

    DAO_BAD_ARG = -1,
    DAO_DB_ERROR = -2,
    DAO_CONFLICT = -3
} dao_status_t;
