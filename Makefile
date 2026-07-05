CC := gcc
CFLAGS := -Wall -Wextra -g -Iinclude -Ithird_party -pthread

COMMON_SRCS := \
	src/common/utils.c \
	src/common/protocol.c \
	src/common/log.c \
	src/common/tui.c
	
SERVER_SRCS := \
	src/server/main.c \
	src/server/config.c \
	src/server/network.c \
	src/server/database.c \
	src/server/database_pool.c \
	src/server/thread_pool.c \
	src/server/session.c \
	src/server/handler_event.c \
	src/server/handler_basic.c \
	src/server/handler_transfer.c \
	src/server/dao_auth.c \
	src/server/dao_basic.c \
	src/server/dao_transfer.c
	

CLIENT_SRCS := \
	src/client/main.c \
	src/client/config.c \
	src/client/network.c \
	src/client/user_auth.c \
	src/client/handler.c \
	src/client/menu.c

THIRD_PARTY_SRCS := \
	third_party/inih/ini.c


COMMON_OBJS := $(patsubst src/%.c,build/%.o,$(COMMON_SRCS))
SERVER_OBJS := $(patsubst src/%.c,build/%.o,$(SERVER_SRCS))
CLIENT_OBJS := $(patsubst src/%.c,build/%.o,$(CLIENT_SRCS))
THIRD_PARTY_OBJS := $(patsubst third_party/%.c,build/third_party/%.o,$(THIRD_PARTY_SRCS))

.PHONY: all server client clean rebuild bear

all: server client

server: bin/server-cdd

client: bin/client-cdd

bin/server-cdd: $(COMMON_OBJS) $(THIRD_PARTY_OBJS) $(SERVER_OBJS)
	mkdir -p bin
	$(CC) $(CFLAGS) $^ -o $@ -lpthread -lmysqlclient -lcrypt -lcrypto

bin/client-cdd: $(COMMON_OBJS) $(THIRD_PARTY_OBJS) $(CLIENT_OBJS)
	mkdir -p bin
	$(CC) $(CFLAGS) $^ -o $@ -lcrypt -lcrypto

build/%.o: src/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/third_party/%.o: third_party/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build
	rm -f bin/server-cdd bin/client-cdd

rebuild: clean all

bear:
	bear -- $(MAKE) clean all
