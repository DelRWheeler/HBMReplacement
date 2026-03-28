# Makefile for FIT7 Load Cell Simulator (Linux)
#
# Usage:
#   make          - Build the simulator
#   make clean    - Clean build artifacts
#   make run      - Build and run

CC = gcc
CFLAGS = -Wall -Wextra -O2 -DSIM_BUILD -I./src -I./config
LDFLAGS = -lpthread -lm -lutil

SRC_DIR = src
SIM_DIR = sim
BUILD_DIR = build

# Source files
SOURCES = \
	$(SRC_DIR)/main.c \
	$(SRC_DIR)/ring_buffer.c \
	$(SRC_DIR)/fit7_protocol.c \
	$(SRC_DIR)/config_store.c \
	$(SIM_DIR)/hal_sim.c

OBJECTS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCES))
TARGET = $(BUILD_DIR)/fit7_sim

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)
	@echo "Built: $@"

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) sim_config.bin

run: $(TARGET)
	./$(TARGET)
