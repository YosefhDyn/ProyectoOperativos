CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -pthread
SRC = src
BIN = bin

all: $(BIN)/controlador $(BIN)/agente

$(BIN)/controlador: $(SRC)/controlador.c $(SRC)/protocolo.h
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/controlador.c

$(BIN)/agente: $(SRC)/agente.c $(SRC)/protocolo.h
	mkdir -p $(BIN)
	$(CC) $(CFLAGS) -o $@ $(SRC)/agente.c

clean:
	rm -rf $(BIN)
	rm -f pipe_*

.PHONY: all clean
