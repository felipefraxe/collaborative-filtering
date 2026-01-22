CC := clang
CFLAGS := -I include -Wall -Wextra -O2

SRC_DIR := src
INC_DIR := include
BUILD_DIR := build

SRCS := \
	$(SRC_DIR)/main.c \
	$(SRC_DIR)/csr.c \
	$(SRC_DIR)/sparse_comp.c \
	$(SRC_DIR)/thread_pool.c \
	$(SRC_DIR)/htable.c

OBJS := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

TARGET := main

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

re: clean all

.PHONY: all clean re