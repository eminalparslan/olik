#include "gapbuffer.h"

// Initial capacity of a string
#define STRING_INIT_CAP 16

/* Append a character to a string s. */
void strAppendChar(String *s, char c) {
  // Check if capacity needs to grow
  if (s->size >= s->capacity) {
    s->capacity = s->capacity == 0 ? STRING_INIT_CAP : s->capacity * 2;
    s->chars = (char *) realloc(s->chars, s->capacity * sizeof(*(s->chars)));
    assert(s->chars != NULL);
  }
  s->chars[s->size++] = c;
}

/* Append many characters to a string s. */
void strAppendChars(String *s, const char *cs, int length) {
  // Check if capacity needs to grow
  if (s->size + length > s->capacity) {
    s->capacity = s->capacity == 0 ? STRING_INIT_CAP + length : (s->size + length) * 2;
    s->chars = (char *) realloc(s->chars, s->capacity * sizeof(*(s->chars)));
    assert(s->chars != NULL);
  }
  // Copy chars to strings
  memcpy(&s->chars[s->size], cs, length * sizeof(*(s->chars)));
  s->size += length;
}

/* Prepend characters to a string s. */
void strPrependChars(String *s, const char *cs, int length) {
  // Check if capacity needs to grow
  if (s->size + length > s->capacity) {
    s->capacity = (s->size + length) * 2;
    s->chars = (char *) realloc(s->chars, s->capacity * sizeof(*(s->chars)));
    assert(s->chars != NULL);
  }
  // Shift string chars to make room at the beginning
  memmove(&s->chars[length], s->chars, s->size * sizeof(*(s->chars)));
  // Copy chars to beginning
  memcpy(s->chars, cs, length * sizeof(*(s->chars)));
  s->size += length;
}

/* Empty the string. */
void strClear(String *s) {
  s->size = 0;
}

/* Return the length of the gap buffer. */
size_t gbLen(GapBuffer *buf) {
  return buf->head.size + buf->tail.size;
}

/* Write nbyte bytes of gap buffer buf to fildes.
   Returns the total bytes written. */
size_t gbWrite(int fildes, GapBuffer *buf, size_t nbyte) {
  assert(nbyte <= gbLen(buf));
  int written = 0;
  if (nbyte < buf->head.size) {
    // Only need to write chars from head string
    written += write(fildes, buf->head.chars, nbyte);
  } else {
    written += write(fildes, buf->head.chars, buf->head.size);
    written += write(fildes, buf->tail.chars, nbyte - buf->head.size);
  }
  return written;
}

/* Move the gap in the gap buffer to pos. */
void gbMoveGap(GapBuffer *buf, int pos) {
  assert(pos >= 0 && pos <= gbLen(buf));

  int headLength = buf->head.size;
  if (pos < headLength) {
    // Take the chars in head string after pos and prepend them to tail string
    strPrependChars(&buf->tail, &buf->head.chars[pos], buf->head.size - pos);
    buf->head.size = pos;
  } else if (pos > headLength) {
    pos -= headLength;
    // Append the first pos chars in tail string to head string
    strAppendChars(&buf->head, buf->tail.chars, pos);
    buf->tail.size -= pos;
    // Shift the tail string chars after pos to the beginning
    memmove(buf->tail.chars, &buf->tail.chars[pos], buf->tail.size * sizeof(*(buf->tail.chars)));
  }
}

/* Inserts a character at the gap. */
void gbInsertChar(GapBuffer *buf, char c) {
  strAppendChar(&buf->head, c);
}

/* Inserts length characters at the gap. */
void gbInsertChars(GapBuffer *buf, const char *cs, int length) {
  strAppendChars(&buf->head, cs, length);
}

/* Deletes the character before the gap. */
char gbDeleteChar(GapBuffer *buf) {
  assert(buf->head.size > 0);
  return buf->head.chars[--buf->head.size];
}

/* Pushes a character to the end of the gap buffer. */
void gbPushChar(GapBuffer *buf, char c) {
  strAppendChar(&buf->tail, c);
}

/* Pushes length characters to the end of the gap buffer. */
void gbPushChars(GapBuffer *buf, const char *cs, int length) {
  strAppendChars(&buf->tail, cs, length);
}

/* Pops a character off the end of the gap buffer. */
char gbPopChar(GapBuffer *buf) {
  assert(gbLen(buf) > 0);
  if (buf->tail.size > 0) {
    return buf->tail.chars[--buf->tail.size];
  } else {
    return buf->head.chars[--buf->head.size];
  }
}

/* Appends the characters of src to dst. */
void gbConcat(GapBuffer *dst, GapBuffer *src) {
  gbPushChars(dst, src->head.chars, src->head.size);
  gbPushChars(dst, src->tail.chars, src->tail.size);
}

/* Splits src at gap and pushes tail from src to dst. */
void gbSplit(GapBuffer *dst, GapBuffer *src) {
  gbPushChars(dst, src->tail.chars, src->tail.size);
  strClear(&src->tail);
}

/* Prints the gap buffer at fildes. */
void gbPrint(GapBuffer *buf, int fildes) {
  gbWrite(fildes, buf, gbLen(buf));
  const char nl = '\n';
  write(fildes, &nl, 1);
}

/* Creates a new gap buffer. */
GapBuffer *gbCreate() {
  GapBuffer *gbNew = calloc(1, sizeof(GapBuffer));
  assert(gbNew != NULL);
  return gbNew;
}

/* Frees the gap buffer. */
void gbFree(GapBuffer *buf) {
  free(buf);
}
