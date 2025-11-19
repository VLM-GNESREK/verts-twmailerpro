# Makefile
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g

all: twmailer-server twmailer-client

twmailer-server: server.c Headers/common.h
	$(CC) $(CFLAGS) -o twmailer-server server.c -lldap -llber

twmailer-client: client.c Headers/common.h
	$(CC) $(CFLAGS) -o twmailer-client client.c

clean:
	rm -f twmailer-server twmailer-client