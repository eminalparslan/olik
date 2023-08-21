#include "piecetable.h"
#include "list.h"

#define DEBUG 1

#ifndef DEBUG
#define DEBUG 0
#endif
#define debug_print(str, ...) printf("[DEBUG] %s:%d "str"\n", __FILE__, __LINE__, __VA_ARGS__)

// Reference: https://www.catch22.net/tuts/neatpad/piece-chains/

Piece *pieceCreate(size_t offset, size_t length, WhichBuffer which) {
  Piece *p = calloc(1, sizeof(Piece));
  p->which = which;
  p->offset = offset;
  p->length = length;
  return p;
}

void pieceAppend(PieceRange *pr, Piece *piece) {
  if (pr->first == NULL) {
    pr->first = piece;
  } else {
    pr->last->next = piece;
    piece->prev = pr->last;
  }
  pr->last = piece;
}

void pieceRemove(Piece *piece) {
  piece->prev->next = piece->next;
  piece->next->prev = piece->prev;
  free(piece);
}

PieceRange *rangeCreate(PieceTable *pt, Piece *first, Piece *last, bool boundary) {
  PieceRange *pr = malloc(sizeof(PieceRange));
  pr->first = first;
  pr->last = last;
  pr->boundary = boundary;
  pr->sequence_length = pt->sequence_length;
  return pr;
}

void rangeExtend(PieceRange *src, PieceRange *dest) {
  if (!src->boundary) {
    if (dest->boundary) {
      dest->first = src->first;
      dest->last = src->last;
      dest->boundary = false;
    } else {
      src->last->next = dest->first;
      dest->first->prev = src->last;
      dest->first = src->first;
    }
  }
}

void rangeSwap(PieceRange *src, PieceRange *dest) {
  if (src->boundary) {
    if (!dest->boundary) {
      src->first->next = dest->first;
      src->last->prev  = dest->last;
      dest->first->prev = src->first;
      dest->last->next  = src->last;
    }
  } else {
    if (dest->boundary) {
      src->first->prev->next = src->last->next;
      src->last->next->prev  = src->first->prev;
    } else {
      src->first->prev->next = dest->first;
      src->last->next->prev  = dest->last;
      dest->first->prev = src->first->prev;
      dest->last->next = src->last->next;
    }
  }
}

void rangeSwapBack(PieceTable *pt, PieceRange *pr) {
  if (pr->boundary) {
    // This PieceRange has two elements first and last which refer to Pieces
    // on either side of what used to be a boundary. The boundary had stuff
    // inserted into it, meaning pr->first->next refers to a Piece inserted
    // in the boundary currently in the PieceTable.

    // here we get the first and last Pieces inserted into the boundary.
    Piece *first = pr->first->next;
    Piece *last = pr->last->prev;

    // we recreate the boundary
    pr->first->next = pr->last;
    pr->last->prev = pr->first;

    // here we store what was initially inserted
    // this PieceRange is in the opposite stack
    pr->first = first;
    pr->last = last;
    pr->boundary = false;
  } else {
    // get Pieces from PieceTable that surrounded pr
    Piece *first = pr->first->prev;
    Piece *last = pr->last->next;

    // check for boundary case
    if (first->next == last) {
      // relink current PieceRange
      first->next = pr->first;
      last->prev = pr->last;

      // store this PieceRange as boundary
      pr->first = first;
      pr->last = last;
      pr->boundary = true;
    } else {
      // replacing non-boundary with non-boundary
      // get first and last Pieces that replaced pr
      first = first->next;
      last = last->prev;

      // relink pr back into the PieceTable
      first->prev->next = pr->first;
      last->next->prev = pr->last;
      /* first->next = pr->first; */
      /* last->prev = pr->last; */

      // store this PieceRange
      pr->first = first;
      pr->last = last;
      /* pr->first = first->next; */
      /* pr->last = last->prev; */
      pr->boundary = false;
    }
  }

  // restore sequence length and save current sequence length
  size_t new_sequence_length = pr->sequence_length;
  pr->sequence_length = pt->sequence_length;
  pt->sequence_length = new_sequence_length;
}

PieceTable *ptCreate(const char *original_buffer, size_t buffer_length) {
  PieceTable *pt = calloc(1, sizeof(PieceTable));
  pt->original.elems = (char *) original_buffer;
  pt->original.size = buffer_length;
  pt->original.capacity = buffer_length;
  pt->head = calloc(1, sizeof(Piece));
  pt->tail = calloc(1, sizeof(Piece));
  pt->head->next = pt->tail;
  pt->tail->prev = pt->head;
  // add piece for original buffer
  PieceRange oldPR = {
    .first = pt->head,
    .last = pt->tail,
    .boundary = true,
  };
  Piece *newPiece = pieceCreate(0, buffer_length, Original);
  PieceRange newPR = {
    .first = newPiece,
    .last = newPiece,
    .boundary = false,
  };
  // and place it in the piece table
  rangeSwap(&oldPR, &newPR);
  pt->sequence_length = buffer_length;
  return pt;
}

void ptFree(PieceTable *pt) {
  if (pt->add.capacity > 0) free(pt->add.elems);
  free(pt->original.elems);
  for (Piece *p = pt->head->next; p; p = p->next) free(p->prev);
  free(pt->tail);
  free(pt);
}

bool ptUndo(PieceTable *pt) {
  if (DEBUG) debug_print("Undo: %zu", pt->undo_stack.size);
  if (pt->undo_stack.size == 0) return false;
  // prevent optimized actions
  pt->last_action = Nop;

  PieceRange *pr = listPop(&pt->undo_stack);
  listAppend(&pt->redo_stack, pr);
  rangeSwapBack(pt, pr);
  return true;
}

bool ptRedo(PieceTable *pt) {
  if (DEBUG) debug_print("Redo: %zu", pt->redo_stack.size);
  if (pt->redo_stack.size == 0) return false;
  // prevent optimized actions
  pt->last_action = Nop;

  PieceRange *pr = listPop(&pt->redo_stack);
  listAppend(&pt->undo_stack, pr);
  rangeSwapBack(pt, pr);
  return true;
}

void ptInsertChars(PieceTable *pt, size_t index, const char *chars, size_t length) {
  if (DEBUG) debug_print("Insert: index=%zu chars='%s' length=%zu", index, chars, length);
  assert(0 <= index && index <= pt->sequence_length);
  if (length <= 0) return;

  // keep track of current offset in 'add' buffer
  size_t add_offset = pt->add.size;
  // add chars to 'add' buffer
  listExtend(&pt->add, chars, length);

  // clear redo stack
  listClear(&pt->redo_stack);

  Piece *piece;
  size_t current_index = 0;
  // keep track of the index of the end of the last insertion
  static size_t prev_end_index = -1;
  // iterate over piece table
  for (piece = pt->head->next; piece->next && current_index < index; piece = piece->next) {
    size_t in_piece_offset = index - current_index;
    current_index += piece->length;
    if (current_index > index) {
      // inserting in the middle of a Piece, so break this piece apart
      // first, save the current Piece in a PieceRange
      PieceRange *oldPR = rangeCreate(pt, piece, piece, false);
      listAppend(&pt->undo_stack, oldPR);
      // next, create new Pieces
      Piece *leftPiece = pieceCreate(piece->offset, in_piece_offset, piece->which);
      Piece *newPiece = pieceCreate(add_offset, length, Add);
      Piece *rightPiece = pieceCreate(piece->offset + in_piece_offset, piece->length - in_piece_offset, piece->which);
      // link them up
      PieceRange newPR = { .boundary = false };
      pieceAppend(&newPR, leftPiece);
      pieceAppend(&newPR, newPiece);
      pieceAppend(&newPR, rightPiece);
      // swap the ranges
      rangeSwap(oldPR, &newPR);
    } else if (current_index == index) {
      // insert after this piece at boundary
      if (index == prev_end_index && pt->last_action == Insert) {
        if (DEBUG) debug_print("Insert:     optimized at index=%zu", index);
        // we can just extend the last Piece since our last insert ended here
        piece->length += length;
      } else {
        // add current state to undo stack
        PieceRange *oldPR = rangeCreate(pt, piece, piece->next, true);
        listAppend(&pt->undo_stack, oldPR);
        // create new piece
        Piece *newPiece = pieceCreate(add_offset, length, Add);
        PieceRange newPR = {
          .first = newPiece,
          .last = newPiece,
          .boundary = false,
        };
        // and place it in the piece table
        rangeSwap(oldPR, &newPR);
      }
    }
  }

  // deal with case where index == 0
  // can't optimize here since prev_end_index can never be == 0
  if (index == 0) {
    // add current state to undo stack
    PieceRange *oldPR = rangeCreate(pt, pt->head, pt->head->next, true);
    listAppend(&pt->undo_stack, oldPR);
    // create new piece
    Piece *newPiece = pieceCreate(add_offset, length, Add);
    PieceRange newPR = {
      .first = newPiece,
      .last = newPiece,
      .boundary = false,
    };
    // and place it in the piece table
    rangeSwap(oldPR, &newPR);
  }
  
  prev_end_index = index + length;
  pt->sequence_length += length;
  pt->last_action = Insert;
}

void ptInsertChar(PieceTable *pt, size_t index, char c) {
  ptInsertChars(pt, index, &c, 1);
}

void ptDeleteChars(PieceTable *pt, size_t index, size_t length) {
  if (DEBUG) debug_print("Delete: index=%zu length=%zu seq_length=%zu", index, length, pt->sequence_length);
  assert(index + length <= pt->sequence_length);
  if (length <= 0) return;

  // clear redo stack
  listClear(&pt->redo_stack);

  Piece *piece;
  // PieceRange for replaced pieces
  PieceRange *oldPR = rangeCreate(pt, NULL, NULL, false);
  // PieceRange that will be swapped with oldPR
  PieceRange newPR = { .boundary = false };
  bool update_prev_undo = false;
  size_t current_index = 0;
  size_t remaining_length = length;
  // keep track of the last index we deleted at
  static size_t prev_index = -1;
  // keep track of these for optimizing consecutive deletes
  static Piece *leftPiece = NULL;
  static Piece *rightPiece = NULL;

  // TODO: implement optimization for deleting on other side

  if (index + length == prev_index && pt->last_action == Delete) {
    // we can extend the last delete "backwards"
    if (DEBUG) debug_print("Delete:     optimized at index=%zu", index + length);
    if (leftPiece != NULL) {
      if (length < leftPiece->length) {
        // just shorten the Piece to delete the rest of the Piece
        leftPiece->length -= length;
        pt->sequence_length -= length;
        prev_index = index;
        // this is all we need to do
        debug_print("Delete:     early return; leftPiece->length=%zu", leftPiece->length);
        return;
      } else {
        // this piece can be removed
        remaining_length -= leftPiece->length;
        pieceRemove(leftPiece);
        leftPiece = NULL;
        // we need to update the last undo
        update_prev_undo = true;
      }
    }
  }

  // iterate over piece table
  for (piece = pt->head->next; piece->next && remaining_length > 0; piece = piece->next) {
    int in_piece_offset = index - current_index;
    current_index += piece->length;
    if (current_index >= index) {
      if (in_piece_offset >= 0) {
        // first Piece in remove range
        in_piece_offset = (size_t) in_piece_offset;
        pieceAppend(oldPR, piece);
        if (in_piece_offset > 0) {
          // split and keep first half
          leftPiece = pieceCreate(piece->offset, in_piece_offset, piece->which);
          pieceAppend(&newPR, leftPiece);
        }
        // check if we need to split again and keep last part
        if (in_piece_offset + remaining_length < piece->length) {
          rightPiece = pieceCreate(piece->offset + in_piece_offset + remaining_length,
                                   piece->length - in_piece_offset - remaining_length,
                                   piece->which);
          pieceAppend(&newPR, rightPiece);
          remaining_length = 0;
        } else {
          remaining_length -= piece->length - in_piece_offset;
        }
      } else {
        pieceAppend(oldPR, piece);
        // check if only part of the last piece is removed
        if (remaining_length < piece->length) {
          Piece *newPiece = pieceCreate(piece->offset + remaining_length,
                                        piece->length - remaining_length,
                                        piece->which);
          pieceAppend(&newPR, newPiece);
          remaining_length = 0;
        } else {
          remaining_length -= piece->length;
        }
      }
    }
  }
  
  if (oldPR->first && oldPR->last) {
    if (update_prev_undo) {
      // optimized path: update the last undo
      PieceRange *prevOldPR = listPeek(&pt->undo_stack);
      rangeExtend(oldPR, prevOldPR);
    } else {
      // default: we have a new undo event
      listAppend(&pt->undo_stack, oldPR);
    }
    if (newPR.first && newPR.last) {
      rangeSwap(oldPR, &newPR);
    }
  }

  prev_index = index;
  pt->sequence_length -= length;
  pt->last_action = Delete;
}

void ptDeleteChar(PieceTable *pt, size_t index) {
  ptDeleteChars(pt, index, 1);
}

void ptReplaceChars(PieceTable *pt, size_t index, const char *chars, size_t length) {
  if (DEBUG) debug_print("Replace: index=%zu chars='%s' length=%zu", index, chars, length);

}

void ptReplaceChar(PieceTable *pt, size_t index, char c) {
  ptReplaceChars(pt, index, &c, 1);
}

size_t ptGetChars(PieceTable *pt, char *dest, size_t index, size_t length) {
  assert(index + length <= pt->sequence_length);
  if (length <= 0) return 0;

  Piece *piece;
  size_t current_index = 0;
  size_t total = 0;
  for (piece = pt->head->next; piece->next && current_index < index + length; piece = piece->next) {
    Buffer buf = piece->which == Original ? pt->original : pt->add;
    int in_piece_offset = index - current_index;
    current_index += piece->length;
    if (current_index >= index) {
      size_t current_length = 0;
      if (in_piece_offset > 0) {
        // split first piece in range
        current_length = piece->length - (size_t) in_piece_offset;
        memcpy(dest, &buf.elems[piece->offset+in_piece_offset], current_length * sizeof(*buf.elems));
      } else {
        size_t rest_piece_length = 0;
        if (current_index > index + length) {
          // split last piece in range
          rest_piece_length = current_index - (index + length);
        }
        current_length = piece->length - rest_piece_length;
        memcpy(dest, &buf.elems[piece->offset], current_length * sizeof(*buf.elems));
      }
      total += current_length;
      dest += current_length;
    }
  }
  return total;
}

void ptPrint(PieceTable *pt) {
  Piece *piece;
  for (piece = pt->head->next; piece->next; piece = piece->next) {
    Buffer buf = piece->which == Original ? pt->original : pt->add;
    fwrite(&buf.elems[piece->offset], sizeof(*buf.elems), piece->length, stdout);
  }
  fwrite("\n", sizeof(char), 1, stdout);
  fflush(stdout);
}

