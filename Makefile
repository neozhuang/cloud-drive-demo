CC := gcc
CFLAGS := -Wall -Wextra -g -Iinclude -pthread

COMMON_SRCS := \
	src/common/utils.c \
	src/common/protocol.c \
	src/common/log.c
	
SERVER_SRCS := \
	src/server/main.c \
	src/server/config.c \
	src/server/network.c \
	src/server/thread_pool.c \
	src/server/session.c \
	src/server/handler.c \
	src/server/auth_config.c

CLIENT_SRCS := \
	src/client/main.c \
	src/client/config.c \
	src/client/network.c \
	src/client/auth.c \
	src/client/handler.c

COMMON_OBJS := $(patsubst src/%.c,build/%.o,$(COMMON_SRCS))
SERVER_OBJS := $(patsubst src/%.c,build/%.o,$(SERVER_SRCS))
CLIENT_OBJS := $(patsubst src/%.c,build/%.o,$(CLIENT_SRCS))

.PHONY: all server client clean rebuild bear

all: server client

server: bin/cdd-server

client: bin/cdd-client

bin/cdd-server: $(COMMON_OBJS) $(SERVER_OBJS)
	mkdir -p bin
	$(CC) $(CFLAGS) $^ -o $@ -lpthread

bin/cdd-client: $(COMMON_OBJS) $(CLIENT_OBJS)
	mkdir -p bin
	$(CC) $(CFLAGS) $^ -o $@ -lpthread

build/%.o: src/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build
	rm -f bin/cdd-server bin/cdd-client

rebuild: clean all

bear:
	bear -- $(MAKE) clean all
