CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g
TARGETS = twmailer-server twmailer-client

all: $(TARGETS)

twmailer-server: server.c
	$(CC) $(CFLAGS) -o twmailer-server server.c

twmailer-client: client.c
	$(CC) $(CFLAGS) -o twmailer-client client.c

clean:
	rm -f $(TARGETS)

.PHONY: all clean