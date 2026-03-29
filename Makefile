CC      = gcc
CFLAGS  = -Wall -Wextra -pedantic -std=c11 \
          $(shell pkg-config --cflags glfw3 gl freetype2 libcurl) \
          -Ithird_party/quickjs/include \
          -Ithird_party/lexbor/include \
          -Ithird_party/stb
LDFLAGS = $(shell pkg-config --libs glfw3 gl freetype2 libcurl) \
          -Lthird_party/quickjs/lib -lqjs -lqjs-libc \
          -Lthird_party/lexbor/lib -llexbor_static \
          -lm -lpthread

SRC_DIR   = src
BUILD_DIR = build
TARGET    = $(BUILD_DIR)/explorer

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))

.PHONY: all clean run dev

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: $(TARGET)
	./$(TARGET)

dev: $(TARGET)
	./$(TARGET) --debug-port 9222

clean:
	rm -rf $(BUILD_DIR)
