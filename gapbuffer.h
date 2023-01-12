#include <stdlib.h>
#include <assert.h>
#include <string.h>

struct String {
  char *chars;
  size_t size;
  size_t capacity;
};

struct GapBuffer {
  struct String head, tail;
};

