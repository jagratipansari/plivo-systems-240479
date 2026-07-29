CC ?= gcc

CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic
LDLIBS ?= -pthread

TARGETS := sender receiver

.PHONY: all clean

all: $(TARGETS)

sender: sender.c
	$(CC) $(CFLAGS) sender.c -o sender $(LDLIBS)

receiver: receiver.c
	$(CC) $(CFLAGS) receiver.c -o receiver $(LDLIBS)

clean:
	rm -f $(TARGETS)
