#include <stdio.h>
#include <assert.h>
#include "piecetable.h"

int main(void) {
  const char text[] = "Hello world";
  PieceTable *pt = ptCreate(text, sizeof(text));
  ptPrint(pt);
  ptInsertChars(pt, 2, "abc", 3);
  ptPrint(pt);
  ptInsertChars(pt, 2, "xyz", 3);
  ptPrint(pt);
  ptInsertChars(pt, 2, "ooo", 3);
  ptPrint(pt);
  ptInsertChars(pt, 5, "iii", 3);
  ptPrint(pt);
  ptInsertChars(pt, 0, "ppp", 3);
  ptPrint(pt);
  ptInsertChars(pt, 0, "u", 1);
  ptPrint(pt);
  ptInsertChars(pt, 27, "u", 1);
  ptPrint(pt);
  ptInsertChars(pt, 28, "v", 1);
  ptPrint(pt);

  char dest[4];
  dest[sizeof(dest)-1] = '\0';
  size_t total;

  total = ptGetChars(pt, dest, 5, 3);
  printf("%s: %zu\n", dest, total);
  total = ptGetChars(pt, dest, 5, 3);
  printf("%s: %zu\n", dest, total);
  total = ptGetChars(pt, dest, 0, 3);
  printf("%s: %zu\n", dest, total);

  char dest2[pt->sequence_length+1];
  dest[sizeof(dest)-1] = '\0';

  total = ptGetChars(pt, dest2, 0, pt->sequence_length);
  printf("%s: %zu\n", dest2, total);

  bool status;
  status = ptRedo(pt);
  assert(status == false);

  printf("Undo/Redo\n");
  ptPrint(pt);
  ptUndo(pt);
  ptPrint(pt);
  ptRedo(pt);
  ptPrint(pt);
  ptUndo(pt);
  ptPrint(pt);
  ptUndo(pt);
  ptPrint(pt);
  ptUndo(pt);
  ptPrint(pt);
  ptUndo(pt);
  ptPrint(pt);
  ptUndo(pt);
  ptPrint(pt);
  ptUndo(pt);
  ptPrint(pt);
  ptRedo(pt);
  ptPrint(pt);
  ptRedo(pt);
  ptPrint(pt);
  ptUndo(pt);
  ptPrint(pt);
  ptUndo(pt);
  ptPrint(pt);
}

