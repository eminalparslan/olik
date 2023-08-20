CC = clang
CC_FLAGS = -g -Wall -Werror -pedantic -std=c99

olik: olik.c gapbuffer.o
	${CC} ${CC_FLAGS} olik.c gapbuffer.o -o olik

test: test.c piecetable.o
	${CC} ${CC_FLAGS} test.c piecetable.o -o test

gapbuffer.o: gapbuffer.c gapbuffer.h
	${CC} -c ${CC_FLAGS} gapbuffer.c gapbuffer.h list.h

piecetable.o: piecetable.c piecetable.h
	${CC} -c ${CC_FLAGS} piecetable.c piecetable.h list.h
