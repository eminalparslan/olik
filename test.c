#include <stdio.h>
#include <assert.h>
#include "gapbuffer.h"

int main() {
  GapBuffer *gb = gbCreate();
  gbPushChars(gb, "testing123", 11);
  assert(gbLen(gb) == 11);
  gbPrint(gb);
  gbMoveGap(gb, 7);
  gbInsertChar(gb, '0');
  gbPrint(gb);
  return 0;
}

