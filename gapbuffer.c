#include "gapbuffer.h"

/* Return the length of the gap buffer. */
size_t gbLen(GapBuffer *buf) {
  return buf->head.size + buf->tail.size;
}

/* Prints the gap buffer as a string to file descriptor. */
size_t gbPrint(GapBuffer *buf, FILE *fp) {
  size_t size = gbLen(buf);
  char chars[size + 1];
  chars[size] = '\0';
  if (size < buf->head.size) {
    // Only need to write chars from head string
    memcpy(chars, buf->head.elems, size);
  } else {
    memcpy(chars, buf->head.elems, buf->head.size * sizeof(char));
    memcpy(&chars[buf->head.size], buf->tail.elems, (size - buf->head.size) * sizeof(char));
  }
  return fprintf(fp, "%s", chars);
}

/* Move the gap in the gap buffer to pos. */
void gbMoveGap(GapBuffer *buf, int pos) {
  assert(pos >= 0 && pos <= gbLen(buf));

  int headLength = buf->head.size;
  if (pos < headLength) {
    // Take the chars in head string after pos and prepend them to tail string
    listExtendLeft(&buf->tail, &buf->head.elems[pos], buf->head.size - pos);
    buf->head.size = pos;
  } else if (pos > headLength) {
    pos -= headLength;
    // Append the first pos chars in tail string to head string
    listExtend(&buf->head, buf->tail.elems, pos);
    buf->tail.size -= pos;
    // Shift the tail string chars after pos to the beginning
    memmove(buf->tail.elems, &buf->tail.elems[pos], buf->tail.size * sizeof(char));
  }
}

/* Inserts a character at the gap. */
void gbInsertChar(GapBuffer *buf, char c) {
  listAppend(&buf->head, c);
}

/* Inserts length characters at the gap. */
void gbInsertChars(GapBuffer *buf, const char *cs, int length) {
  listExtend(&buf->head, cs, length);
}

/* Deletes the character before the gap. */
char gbDeleteChar(GapBuffer *buf) {
  assert(buf->head.size > 0);
  return buf->head.elems[--buf->head.size];
}

/* Pushes a character to the end of the gap buffer. */
void gbPushChar(GapBuffer *buf, char c) {
  listAppend(&buf->tail, c);
}

/* Pushes length characters to the end of the gap buffer. */
void gbPushChars(GapBuffer *buf, const char *cs, int length) {
  listExtend(&buf->tail, cs, length);
}

/* Pops a character off the end of the gap buffer. */
char gbPopChar(GapBuffer *buf) {
  assert(gbLen(buf) > 0);
  if (buf->tail.size > 0) {
    return buf->tail.elems[--buf->tail.size];
  } else {
    return buf->head.elems[--buf->head.size];
  }
}

/* Appends the characters of src to dst. */
void gbConcat(GapBuffer *dst, GapBuffer *src) {
  gbPushChars(dst, src->head.elems, src->head.size);
  gbPushChars(dst, src->tail.elems, src->tail.size);
}

/* Splits src at gap and pushes tail from src to dst. */
void gbSplit(GapBuffer *dst, GapBuffer *src) {
  gbPushChars(dst, src->tail.elems, src->tail.size);
  listClear(&src->tail);
}

void gbClearTail(GapBuffer *buf) {
  listClear(&buf->tail);
}

char gbGetChar(GapBuffer *buf, int pos) {
  assert(gbLen(buf) > 0);
  assert(pos >= 0 && pos < gbLen(buf));
  if (pos < buf->head.size) {
    return buf->head.elems[pos];
  } else {
    return buf->tail.elems[pos - buf->head.size];
  }
}

char *gbGetChars(GapBuffer *buf) {
  char *line = malloc(gbLen(buf) + 1);
  strcpy(line, buf->head.elems);
  strcat(line, buf->tail.elems);
  return line;
}

/* Creates a new gap buffer. */
GapBuffer *gbCreate(void) {
  GapBuffer *gbNew = calloc(1, sizeof(GapBuffer));
  assert(gbNew != NULL);
  return gbNew;
}

GapBuffer *gbCopy(GapBuffer *buf) {
  GapBuffer *gbCopy = gbCreate();
  gbCopy->head = buf->head;
  gbCopy->head.elems = malloc(buf->head.size * sizeof(char));
  memcpy(gbCopy->head.elems, buf->head.elems, buf->head.size * sizeof(char));
  gbCopy->tail = buf->tail;
  gbCopy->tail.elems = malloc(buf->tail.size * sizeof(char));
  memcpy(gbCopy->tail.elems, buf->tail.elems, buf->tail.size * sizeof(char));
  return gbCopy;
}

/* Frees the gap buffer. */
void gbFree(GapBuffer *buf) {
  free(buf->head.elems);
  free(buf->tail.elems);
  free(buf);
}
