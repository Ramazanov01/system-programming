CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -pthread

all: BankServer BankClient

BankServer: BankServer.c
	$(CC) $(CFLAGS) -o BankServer BankServer.c $(LDFLAGS)

BankClient: BankClient.c
	$(CC) $(CFLAGS) -o BankClient BankClient.c $(LDFLAGS)

clean:
	rm -f BankServer BankClient *.o Client* AdaBank.bankLog

.PHONY: all clean