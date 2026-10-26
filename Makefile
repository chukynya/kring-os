CC = gcc
CFLAGS = -Wall -Wextra -std=c17 -pedantic -D_POSIX_C_SOURCE=200809L
TARGET = scheduler
SRC = src/main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean
