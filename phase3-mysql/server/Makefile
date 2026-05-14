CC := gcc

TARGET := bin/cloud_drive_server

BUILD_DIR := build
SRC_DIR := src
INC_DIR := include
THIRD_PARTY_DIR := third_party

SRCS := $(sort $(shell find $(SRC_DIR) -name '*.c' -print))

INIH_SRC := $(THIRD_PARTY_DIR)/inih/ini.c
INIH_OBJ := $(BUILD_DIR)/third_party/inih/ini.o

OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS)) $(INIH_OBJ)
DEPS := $(OBJS:.o=.d)

MYSQL_CFLAGS := $(shell mysql_config --cflags)
MYSQL_LIBS := $(filter-out -lz,$(shell mysql_config --libs))

CPPFLAGS := -I$(INC_DIR) -I$(THIRD_PARTY_DIR)/inih $(MYSQL_CFLAGS)
CFLAGS := -Wall -Wextra -Werror -std=c11 -MMD -MP
LDFLAGS :=
LDLIBS := -pthread $(MYSQL_LIBS)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(INIH_OBJ): $(INIH_SRC)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

-include $(DEPS)
