CC = gcc
CFLAGS = -Wall -Wextra -lpthread
TARGETS = chatserver chatclient

all: $(TARGETS)

chatserver: chatserver.c
	$(CC) $(CFLAGS) -o chatserver chatserver.c

chatclient: chatclient.c
	$(CC) $(CFLAGS) -o chatclient chatclient.c

clean:
	rm -f $(TARGETS) *.o log.txt

rebuild: clean all
