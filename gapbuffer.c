#include "gapbuffer.h"

#define STRING_INIT_CAP 16

void strAppendChar(struct String *s, char c) {
  if (s->size >= s->capacity) {
    s->capacity = s->capacity == 0 ? STRING_INIT_CAP : s->capacity * 2;
    s->chars = (char *) realloc(s->chars, s->capacity * sizeof(*(s->chars)));
    assert(s->chars != NULL);
  }
  s->chars[s->size++] = c;
}

void strAppendChars(struct String *s, const char *cs, int length) {
  if (s->size + length >= s->capacity) {
    s->capacity = (s->size + length) * 2;
    s->chars = (char *) realloc(s->chars, s->capacity * sizeof(*(s->chars)));
    assert(s->chars != NULL);
  }
  memcpy(s->chars + length, cs, length);
  s->size += length;
}

void strPrependChars(struct String *s, const char *cs, int length) {
  if (s->size + length >= s->capacity) {
    s->capacity = (s->size + length) * 2;
    s->chars = (char *) realloc(s->chars, s->capacity * sizeof(*(s->chars)));
    assert(s->chars != NULL);
  }
  memmove(s->chars + length, s->chars, s->size);
  memcpy(s->chars, cs, length);
  s->size += length;
}

char strShift(struct String *s) {
  char c = s->chars[0];
  s->size--;
  memmove(s->chars, s->chars + 1, s->size);
  return c;
}

int gbLen(struct GapBuffer *buf) {
  return buf->head.size + buf->tail.size;
}

size_t gbWrite(int fildes, struct GapBuffer *buf, size_t nbyte) {
  assert(nbyte < gbLen(buf));
  if (nbyte < buf->head.size) {
    write(fildes, buf->head.chars, nbyte);
  } else {
    write(fildes, buf->head.chars, buf->head.size);
    write(fildes, buf->tail.chars, nbyte - buf->head.size);
  }
  write(fildes, "\r\n", 2);
}

void gbMoveGap(struct GapBuffer *buf, int pos) {
  assert(pos > 0 && pos < gbLen(buf));

  int headLength = buf->head.size;
  if (pos < headLength) {
    strPrependChars(&buf->tail, buf->head.chars + pos, buf->head.size - pos);
    buf->head.size = pos;
  } else if (pos > headLength) {
    pos -= headLength;
    strAppendChars(&buf->head, buf->tail.chars, pos);
    buf->tail.size -= pos;
    memmove(buf->tail.chars, buf->tail.chars + pos, buf->tail.size);
  }
}

void gbInsertChar(struct GapBuffer *buf, char c) {
  strAppendChar(&buf->head, c);
}

void gbInsertChars(struct GapBuffer *buf, const char *cs, int length) {
  strAppendChars(&buf->head, cs, length);
}

char gbDeleteChar(struct GapBuffer *buf) {
  assert(buf->head.size > 0);
  return buf->head.chars[--buf->head.size];
}

void gbPushChar(struct GapBuffer *buf, char c) {
  strAppendChar(&buf->tail, c);
}

void gbPushChars(struct GapBuffer *buf, const char *cs, int length) {
  strAppendChars(&buf->tail, cs, length);
}

char gbPopChar(struct GapBuffer *buf) {
  if (buf->tail.size > 0) {
    return buf->tail.chars[--buf->tail.size];
  } else if (buf->head.size > 0) {
    return buf->head.chars[--buf->head.size];
  }
}
