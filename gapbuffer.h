#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include <stdio.h>

typedef struct {
  char *chars;
  size_t size;
  size_t capacity;
} String;

typedef struct {
  String head, tail;
} GapBuffer;

size_t gbLen(GapBuffer *buf);
size_t gbWrite(int fildes, GapBuffer *buf, size_t nbyte);
void gbMoveGap(GapBuffer *buf, int pos);
void gbInsertChar(GapBuffer *buf, char c);
void gbInsertChars(GapBuffer *buf, const char *cs, int length);
char gbDeleteChar(GapBuffer *buf);
void gbPushChar(GapBuffer *buf, char c);
void gbPushChars(GapBuffer *buf, const char *cs, int length);
char gbPopChar(GapBuffer *buf);
void gbConcat(GapBuffer *buf1, GapBuffer *buf2);
void gbSplit(GapBuffer *dst, GapBuffer *src);
void gbPrint(GapBuffer *buf, int fildes);
GapBuffer *gbCreate();
void gbFree(GapBuffer *buf);
