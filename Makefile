CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude -pthread -O2
DEBUG_FLAGS = -Wall -Wextra -Wpedantic -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude -pthread -g3 -O0 -fsanitize=address,undefined

SRC_COMMON = src/common/network.c src/common/board.c src/common/utils.c
SRC_SERVER = src/server/server.c src/server/lobby.c src/server/game.c $(SRC_COMMON)
SRC_CLIENT = src/client/client.c src/client/ui.c $(SRC_COMMON)

SERVER_BIN = server
CLIENT_BIN = client

.PHONY: all debug clean run-server run-client

all: $(SERVER_BIN) $(CLIENT_BIN)

$(SERVER_BIN): $(SRC_SERVER)
	$(CC) $(CFLAGS) -o $@ $(SRC_SERVER)

$(CLIENT_BIN): $(SRC_CLIENT)
	$(CC) $(CFLAGS) -o $@ $(SRC_CLIENT)

debug: CFLAGS = $(DEBUG_FLAGS)
debug: clean all

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN) *.o

run-server: $(SERVER_BIN)
	./$(SERVER_BIN)

run-client: $(CLIENT_BIN)
	./$(CLIENT_BIN)
