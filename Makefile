CC=gcc
CFLAGS=-Wall
INCLUDES=-Iinclude
LDFLAGS=-libverbs
LIBS=-pthread -lrdmacm

SRC_DIR=src
SRC=$(wildcard $(SRC_DIR)/*.c)

OBJ_DIR=obj
OBJS=$(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRC))

TARGET = main

.PHONY: clean

all: $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o : $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET)