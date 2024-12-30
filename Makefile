CC=gcc
CFLAGS=-Wall -Wextra

all: client server

client: client.c
	$(CC) $(CFLAGS) -o client client.c

server: server.c
	$(CC) $(CFLAGS) -o server server.c -lcrypto

clean:
	rm -f client server