#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include <stdio.h>

struct String {
  char *chars;
  size_t size;
  size_t capacity;
};

struct GapBuffer {
  struct String head, tail;
};

int gbLen(struct GapBuffer *buf);
size_t gbWrite(int fildes, struct GapBuffer *buf, size_t nbyte);
void gbMoveGap(struct GapBuffer *buf, int pos);
void gbInsertChar(struct GapBuffer *buf, char c);
void gbInsertChars(struct GapBuffer *buf, const char *cs, int length);
char gbDeleteChar(struct GapBuffer *buf);
void gbPushChar(struct GapBuffer *buf, char c);
void gbPushChars(struct GapBuffer *buf, const char *cs, int length);
char gbPopChar(struct GapBuffer *buf);
void gbConcat(struct GapBuffer *buf1, struct GapBuffer *buf2);
void gbSplit(struct GapBuffer *dst, struct GapBuffer *src);
struct GapBuffer *gbCreate();
void gbFree(struct GapBuffer *buf);
