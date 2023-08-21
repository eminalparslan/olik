#include <stdio.h>
#include <assert.h>

#include "piecetable.h"

int main(void) {
  const char text[] = "Hello world";
  PieceTable *pt = ptCreate(text, sizeof(text)-1);
  char dest[30];
  assert(pt->sequence_length == 11);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "Hello world", pt->sequence_length) == 0);

  ptInsertChars(pt, 0, "world  ", 6);
  ptInsertChars(pt, 0, "Hello ", 6);
  assert(pt->sequence_length == 23);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "Hello world Hello world", pt->sequence_length) == 0);

  ptInsertChars(pt, 5, " good", 5);
  assert(pt->sequence_length == 28);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "Hello good world Hello world", pt->sequence_length) == 0);

  bool status;
  status = ptRedo(pt);
  assert(status == false);

  ptUndo(pt);
  assert(pt->sequence_length == 23);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "Hello world Hello world", pt->sequence_length) == 0);

  ptRedo(pt);
  assert(pt->sequence_length == 28);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "Hello good world Hello world", pt->sequence_length) == 0);

  ptUndo(pt);
  assert(pt->sequence_length == 23);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "Hello world Hello world", pt->sequence_length) == 0);

  ptUndo(pt);
  assert(pt->sequence_length == 17);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "world Hello world", pt->sequence_length) == 0);

  ptRedo(pt);
  assert(pt->sequence_length == 23);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "Hello world Hello world", pt->sequence_length) == 0);

  ptUndo(pt);
  assert(pt->sequence_length == 17);
  ptUndo(pt);
  assert(pt->sequence_length == 11);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "Hello world", pt->sequence_length) == 0);

  ptRedo(pt);
  ptRedo(pt);
  ptRedo(pt);
  assert(pt->sequence_length == 28);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "Hello good world Hello world", pt->sequence_length) == 0);

  ptDeleteChars(pt, 6, 17);
  assert(pt->sequence_length == 11);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "Hello world", pt->sequence_length) == 0);

  ptUndo(pt);
  assert(pt->sequence_length == 28);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "Hello good world Hello world", pt->sequence_length) == 0);

  ptDeleteChars(pt, 5, 18);
  assert(pt->sequence_length == 10);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "Helloworld", pt->sequence_length) == 0);

  ptDeleteChars(pt, 0, 10);
  assert(pt->sequence_length == 0);

  ptUndo(pt);
  ptPrint(pt);
  assert(pt->sequence_length == 10);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "Helloworld", pt->sequence_length) == 0);

  ptInsertChars(pt, 5, " ", 1);
  assert(pt->sequence_length == 11);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "Hello world", pt->sequence_length) == 0);

  // should go through optimized path for inserting
  ptInsertChars(pt, 6, " ", 1);
  assert(pt->sequence_length == 12);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "Hello  world", pt->sequence_length) == 0);

  ptUndo(pt); // optimized (consecutive) actions are grouped in undo/redo
  ptInsertChars(pt, 10, "s", 1);
  assert(pt->sequence_length == 11);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "Helloworlds", pt->sequence_length) == 0);

  ptInsertChar(pt, 0, ' ');
  assert(pt->sequence_length == 12);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, " Helloworlds", pt->sequence_length) == 0);

  // should go through optimized path for inserting
  ptInsertChar(pt, 1, ' ');
  assert(pt->sequence_length == 13);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "  Helloworlds", pt->sequence_length) == 0);

  // should go through optimized path for inserting
  ptInsertChar(pt, 2, ' ');
  assert(pt->sequence_length == 14);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "   Helloworlds", pt->sequence_length) == 0);

  ptDeleteChar(pt, 2);
  assert(pt->sequence_length == 13);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "  Helloworlds", pt->sequence_length) == 0);

  // should go through optimized path for deleting
  ptDeleteChar(pt, 1);
  assert(pt->sequence_length == 12);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, " Helloworlds", pt->sequence_length) == 0);

  // should go through optimized path for deleting
  ptDeleteChar(pt, 0);
  assert(pt->sequence_length == 11);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "Helloworlds", pt->sequence_length) == 0);

  ptDeleteChar(pt, 0);
  assert(pt->sequence_length == 10);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "elloworlds", pt->sequence_length) == 0);

  ptDeleteChar(pt, 9);
  assert(pt->sequence_length == 9);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "elloworld", pt->sequence_length) == 0);

  ptUndo(pt);
  ptUndo(pt);
  assert(pt->sequence_length == 11);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "Helloworlds", pt->sequence_length) == 0);

  ptRedo(pt);
  ptRedo(pt);
  assert(pt->sequence_length == 9);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "elloworld", pt->sequence_length) == 0);

  ptInsertChar(pt, 0, 'H');
  assert(pt->sequence_length == 10);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "Helloworld", pt->sequence_length) == 0);
  
  ptInsertChar(pt, 5, ' ');
  assert(pt->sequence_length == 11);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "Hello world", pt->sequence_length) == 0);

  ptDeleteChars(pt, 8, 3);
  assert(pt->sequence_length == 8);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "Hello wo", pt->sequence_length) == 0);

  ptDeleteChars(pt, 5, 3);
  assert(pt->sequence_length == 5);
  ptGetChars(pt, dest, 0, pt->sequence_length);
  assert(memcmp(dest, "Hello", pt->sequence_length) == 0);

  printf("PASSED ALL TESTS\n");
  return 0;
}

