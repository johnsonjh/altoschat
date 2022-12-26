SHELL  = /bin/sh
CFLAGS = -Wall

all:    chat serv
chat.o: chat.h
serv.o: chat.h
