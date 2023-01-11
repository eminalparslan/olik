CC = clang -g
CC_FLAGS = -Wall -Werror -pedantic -std=c99

olik: olik.c
	${CC} ${CC_FLAGS} olik.c -o olik
