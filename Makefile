# Nome degli eseguibili
SERVER = TriServer
CLIENT = TriClient

# Opzioni del compilatore
CC = gcc
CFLAGS = -Wall -g

# File sorgente
SRCS_SERVER = TriServer.c
SRCS_CLIENT = TriClient.c

# Obiettivi principali
all: $(SERVER) $(CLIENT)

# Compilazione del server
$(SERVER): $(SRCS_SERVER)
	$(CC) $(CFLAGS) -o $(SERVER) $(SRCS_SERVER)

# Compilazione del client
$(CLIENT): $(SRCS_CLIENT)
	$(CC) $(CFLAGS) -o $(CLIENT) $(SRCS_CLIENT)

# Pulizia degli eseguibili
clean:
	rm -f $(SERVER) $(CLIENT)