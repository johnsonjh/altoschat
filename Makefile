SHELL = /bin/sh
CFLAGS =
all:	chat serv
chat.o serv.o:	chat.h
lint:;	lint chat.c ; lint serv.c
tape:;	tar cv README INSTALL Makefile chat.h chat.c serv.c
shar:;	shar README INSTALL Makefile chat.h chat.c serv.c > chat.shar
