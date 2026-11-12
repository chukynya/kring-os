# Makefile - Build automation for UNIX Time-Sharing System Simulator

CC	= gcc
CFLAGS	= -Wall -Wextra -Werror -std=c11 -pedantic
TARGET	= timeshare_sim
SRCS	= main.c scheduler.c worker.c
OBJS	= $(SRCS:.c=.o)
HDRS	= scheduler.h

.PHONY: all run demo test clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

demo: $(TARGET)
	printf "3\n400\n" | ./$(TARGET)

test: $(TARGET)
	@echo "--- [TEST 1] Standard Round-Robin (3 processes, 50 ms quantum) ---"
	printf "3\n50\n" | ./$(TARGET)
	@echo "--- [TEST 2] Single Process boundary (1 process, 20 ms quantum) ---"
	printf "1\n20\n" | ./$(TARGET)
	@echo "--- [TEST 3] CLI arguments mode (4 processes, 30 ms quantum) ---"
	./$(TARGET) 4 30
	@echo "--- [TEST 4] Max processes & min quantum (10 processes, 10 ms) ---"
	./$(TARGET) 10 10 > /dev/null
	@echo "--- [TEST 5] Invalid input rejection ---"
	! printf "0\n100\n" | ./$(TARGET)
	! printf "11\n100\n" | ./$(TARGET)
	! printf "3\n5\n" | ./$(TARGET)
	! printf "abc\n100\n" | ./$(TARGET)
	@echo "=== All automated tests passed ==="

clean:
	$(RM) $(OBJS) $(TARGET)
