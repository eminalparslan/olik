#include <stddef.h>
#define LIST_ELEMS buffer
#include "list.h"

typedef struct {
  char *buffer;
  size_t length;
  size_t capacity;
} Buffer; 

typedef struct PIECE {
  enum { OrgBuffer, AddBuffer } type;
  size_t start;
  size_t length;
  struct PIECE *next;
  struct PIECE *prev;
} Piece;

typedef struct {
  Buffer *org;
  Buffer *add;
  Piece *head;
  Piece *tail;
} PieceTable;
