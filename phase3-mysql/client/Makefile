CC := gcc

TARGET := bin/cloud_drive_client

BUILD_DIR := build
SRC_DIR := src
INC_DIR := include
THIRD_PARTY_DIR := third_party

SRCS := $(sort $(shell find $(SRC_DIR) -name '*.c' -print))

INIH_SRC := $(THIRD_PARTY_DIR)/inih/ini.c
INIH_OBJ := $(BUILD_DIR)/third_party/inih/ini.o

OBJS := \
	$(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS)) \
	$(INIH_OBJ)
DEPS := $(OBJS:.o=.d)

CPPFLAGS := -I$(INC_DIR) -I$(THIRD_PARTY_DIR)/inih
CFLAGS := -Wall -Wextra -Werror -std=c11 -MMD -MP
LDFLAGS :=
LDLIBS := -lcrypt

.PHONY: all clean run rebuild

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

rebuild: clean all

-include $(DEPS)
