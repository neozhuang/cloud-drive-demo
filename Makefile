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
	src/server/timer_wheel.c \
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
	src/client/user_auth.c \
	src/client/menu.c \
	src/client/runtime.c \
	src/client/connection.c \
	src/client/command.c \
	src/client/transfer.c

THIRD_PARTY_SRCS := \
	third_party/inih/ini.c


COMMON_OBJS := $(patsubst src/%.c,build/%.o,$(COMMON_SRCS))
SERVER_OBJS := $(patsubst src/%.c,build/%.o,$(SERVER_SRCS))
CLIENT_OBJS := $(patsubst src/%.c,build/%.o,$(CLIENT_SRCS))
THIRD_PARTY_OBJS := $(patsubst third_party/%.c,build/third_party/%.o,$(THIRD_PARTY_SRCS))
DEPS := $(COMMON_OBJS:.o=.d) $(SERVER_OBJS:.o=.d) $(CLIENT_OBJS:.o=.d) $(THIRD_PARTY_OBJS:.o=.d)

.PHONY: all server client protocol-test session-test timer-wheel-test client-runtime-test client-connection-test client-command-test clean rebuild bear

all: server client

server: bin/server-cdd

client: bin/client-cdd

protocol-test: build/tests/protocol_session_test
	./build/tests/protocol_session_test

session-test: build/tests/session_test
	./build/tests/session_test

timer-wheel-test: build/tests/timer_wheel_test
	./build/tests/timer_wheel_test

client-runtime-test: build/tests/client_runtime_test
	./build/tests/client_runtime_test

client-connection-test: build/tests/client_connection_test
	./build/tests/client_connection_test

client-command-test: build/tests/client_command_test
	./build/tests/client_command_test

build/tests/protocol_session_test: tests/protocol_session_test.c src/common/protocol.c include/common/protocol.h
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

build/tests/session_test: tests/session_test.c src/server/session.c src/common/protocol.c include/server/session.h include/common/protocol.h
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

build/tests/timer_wheel_test: tests/timer_wheel_test.c src/server/timer_wheel.c include/server/timer_wheel.h
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

build/tests/client_runtime_test: tests/client_runtime_test.c src/client/runtime.c src/client/transfer.c src/client/connection.c src/common/protocol.c src/common/utils.c src/common/log.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ -lcrypto

build/tests/client_connection_test: tests/client_connection_test.c src/client/connection.c include/client/connection.h
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

build/tests/client_command_test: tests/client_command_test.c src/client/command.c src/client/runtime.c src/client/transfer.c src/client/connection.c src/client/menu.c src/common/protocol.c src/common/utils.c src/common/log.c src/common/tui.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@ -lcrypto

bin/server-cdd: $(COMMON_OBJS) $(THIRD_PARTY_OBJS) $(SERVER_OBJS)
	mkdir -p bin
	$(CC) $(CFLAGS) $^ -o $@ -lpthread -lmysqlclient -lcrypt -lcrypto

bin/client-cdd: $(COMMON_OBJS) $(THIRD_PARTY_OBJS) $(CLIENT_OBJS)
	mkdir -p bin
	$(CC) $(CFLAGS) $^ -o $@ -lcrypt -lcrypto

build/%.o: src/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

build/third_party/%.o: third_party/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

clean:
	rm -rf build
	rm -f bin/server-cdd bin/client-cdd

rebuild: clean all

bear:
	bear -- $(MAKE) clean all
