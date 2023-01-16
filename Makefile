CC = clang -g
CC_FLAGS = -Wall -Werror -pedantic -std=c99

olik: olik.c gapbuffer.o
	${CC} ${CC_FLAGS} olik.c gapbuffer.o -o olik

gapbuffer.o: gapbuffer.c gapbuffer.h
	${CC} -c ${CC_FLAGS} gapbuffer.c gapbuffer.h
