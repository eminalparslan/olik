#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include "list.h"

// Reference: https://www.catch22.net/tuts/neatpad/piece-chains/

typedef struct {
  char *elems; // TODO: free buffer elems
  size_t size;
  size_t capacity;
} Buffer; 

typedef enum { Original, Add } WhichBuffer;

typedef struct PIECE {
  size_t offset;
  size_t length;
  WhichBuffer which;
  struct PIECE *next;
  struct PIECE *prev;
} Piece;

typedef enum { Insert, Delete, Nop } Action;

typedef struct {
  Piece *first;
  Piece *last;
  bool boundary;
  Action action;
  size_t sequence_length;
} PieceRange; // TODO: free ranges

typedef struct {
  PieceRange **elems;
  size_t size;
  size_t capacity;
} RangeStack;

typedef struct {
  Buffer original;
  Buffer add;
  Piece *head;
  Piece *tail;
  RangeStack undo_stack; // TODO: init and free
  RangeStack redo_stack; // TODO: init and free
  Action last_action;
  size_t sequence_length;
} PieceTable;


PieceTable *ptCreate(const char *original_buffer, size_t buffer_length);
void ptFree(PieceTable *pt);
bool ptUndo(PieceTable *pt);
bool ptRedo(PieceTable *pt);
void ptInsertChars(PieceTable *pt, size_t index, const char *chars, size_t length);
void ptInsertChar(PieceTable *pt, size_t index, char c);
void ptDeleteChars(PieceTable *pt, size_t start_index, size_t end_index);
void ptDeleteChar(PieceTable *pt, size_t index);
void ptReplaceChars(PieceTable *pt, size_t start_index, size_t end_index, const char *chars, size_t length);
void ptReplaceChar(PieceTable *pt, size_t index, char c);
size_t ptGetChars(PieceTable *pt, char *dest, size_t start_index, size_t end_index);
void ptPrint(PieceTable *pt);

