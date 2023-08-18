#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "list.h"

// Basic gap buffer implementation
// https://en.wikipedia.org/wiki/Gap_buffer

typedef struct {
  char *elems;
  size_t size;
  size_t capacity;
} String;

typedef struct {
  String head, tail;
} GapBuffer;

size_t gbLen(GapBuffer *buf);
size_t gbPrint(GapBuffer *buf, FILE *fp);
void gbMoveGap(GapBuffer *buf, int pos);
void gbInsertChar(GapBuffer *buf, char c);
void gbInsertChars(GapBuffer *buf, const char *cs, int length);
char gbDeleteChar(GapBuffer *buf);
void gbPushChar(GapBuffer *buf, char c);
void gbPushChars(GapBuffer *buf, const char *cs, int length);
char gbPopChar(GapBuffer *buf);
void gbConcat(GapBuffer *buf1, GapBuffer *buf2);
void gbSplit(GapBuffer *dst, GapBuffer *src);
void gbClearTail(GapBuffer *buf);
char gbGetChar(GapBuffer *buf, int pos);
char *gbGetChars(GapBuffer *buf);
GapBuffer *gbCreate(void);
GapBuffer *gbCopy(GapBuffer *buf);
void gbFree(GapBuffer *buf);
