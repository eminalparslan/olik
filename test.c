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
}

