#include "gapbuffer.h"

#define STRING_INIT_CAP 16

void strAppendChar(String *s, char c) {
  if (s->size >= s->capacity) {
    s->capacity = s->capacity == 0 ? STRING_INIT_CAP : s->capacity * 2;
    s->chars = (char *) realloc(s->chars, s->capacity * sizeof(*(s->chars)));
    assert(s->chars != NULL);
  }
  s->chars[s->size++] = c;
}

void strAppendChars(String *s, const char *cs, int length) {
  if (s->size + length > s->capacity) {
    s->capacity = s->capacity == 0 ? STRING_INIT_CAP + length : (s->size + length) * 2;
    s->chars = (char *) realloc(s->chars, s->capacity * sizeof(*(s->chars)));
    assert(s->chars != NULL);
  }
  memcpy(s->chars + s->size, cs, length * sizeof(*(s->chars)));
  s->size += length;
}

void strPrependChars(String *s, const char *cs, int length) {
  if (s->size + length >= s->capacity) {
    s->capacity = (s->size + length) * 2;
    s->chars = (char *) realloc(s->chars, s->capacity * sizeof(*(s->chars)));
    assert(s->chars != NULL);
  }
  memmove(s->chars + length, s->chars, s->size * sizeof(*(s->chars)));
  memcpy(s->chars, cs, length * sizeof(*(s->chars)));
  s->size += length;
}

void strClear(String *s) {
  s->size = 0;
}

size_t gbLen(GapBuffer *buf) {
  return buf->head.size + buf->tail.size;
}

size_t gbWrite(int fildes, GapBuffer *buf, size_t nbyte) {
  assert(nbyte <= gbLen(buf));
  int written = 0;
  if (nbyte < buf->head.size) {
    written += write(fildes, buf->head.chars, nbyte);
  } else {
    written += write(fildes, buf->head.chars, buf->head.size);
    written += write(fildes, buf->tail.chars, nbyte - buf->head.size);
  }
  return written;
}

void gbMoveGap(GapBuffer *buf, int pos) {
  // FIXME: pos is at len of buf
  assert(pos >= 0 && pos <= gbLen(buf));

  int headLength = buf->head.size;
  if (pos < headLength) {
    strPrependChars(&buf->tail, buf->head.chars + pos, buf->head.size - pos);
    buf->head.size = pos;
  } else if (pos > headLength) {
    pos -= headLength;
    strAppendChars(&buf->head, buf->tail.chars, pos);
    buf->tail.size -= pos;
    memmove(buf->tail.chars, buf->tail.chars + pos, buf->tail.size * sizeof(*(buf->tail.chars)));
  }
}

void gbInsertChar(GapBuffer *buf, char c) {
  strAppendChar(&buf->head, c);
}

void gbInsertChars(GapBuffer *buf, const char *cs, int length) {
  strAppendChars(&buf->head, cs, length);
}

char gbDeleteChar(GapBuffer *buf) {
  assert(buf->head.size > 0);
  return buf->head.chars[--buf->head.size];
}

void gbPushChar(GapBuffer *buf, char c) {
  strAppendChar(&buf->tail, c);
}

void gbPushChars(GapBuffer *buf, const char *cs, int length) {
  strAppendChars(&buf->tail, cs, length);
}

char gbPopChar(GapBuffer *buf) {
  assert(gbLen(buf) > 0);
  if (buf->tail.size > 0) {
    return buf->tail.chars[--buf->tail.size];
  } else {
    return buf->head.chars[--buf->head.size];
  }
}

void gbConcat(GapBuffer *dst, GapBuffer *src) {
  gbPushChars(dst, src->head.chars, src->head.size);
  gbPushChars(dst, src->tail.chars, src->tail.size);
}

void gbSplit(GapBuffer *dst, GapBuffer *src) {
  gbPushChars(dst, src->tail.chars, src->tail.size);
  strClear(&src->tail);
}

void gbPrint(GapBuffer *buf, int fildes) {
  gbWrite(fildes, buf, gbLen(buf));
  const char nl = '\n';
  write(fildes, &nl, 1);
}

GapBuffer *gbCreate() {
  return calloc(1, sizeof(GapBuffer));
}

void gbFree(GapBuffer *buf) {
  free(buf);
}
