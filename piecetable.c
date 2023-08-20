#include "piecetable.h"
#include "list.h"

// Reference: https://www.catch22.net/tuts/neatpad/piece-chains/

Piece *pieceCreate(size_t offset, size_t length, WhichBuffer which) {
  Piece *p = malloc(sizeof(Piece));
  p->which = which;
  p->offset = offset;
  p->length = length;
  return p;
}

PieceRange *rangeCreate(PieceTable *pt, size_t index, size_t length, Action action,
                        Piece *first, Piece *last, bool boundary) {
  PieceRange *pr = malloc(sizeof(PieceRange));
  pr->first = first;
  pr->last = last;
  pr->boundary = boundary;
  pr->action = action;
  pr->sequence_length = pt->sequence_length;
  return pr;
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

  // restore sequence length
  pt->sequence_length = pr->sequence_length;
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
  if (pt->undo_stack.size == 0) return false;
  // prevent optimized actions
  pt->last_action = Nop;

  PieceRange *pr = listPop(&pt->undo_stack);
  listAppend(&pt->redo_stack, pr);
  rangeSwapBack(pt, pr);
  return true;
}

bool ptRedo(PieceTable *pt) {
  if (pt->redo_stack.size == 0) return false;
  // prevent optimized actions
  pt->last_action = Nop;

  PieceRange *pr = listPop(&pt->redo_stack);
  listAppend(&pt->undo_stack, pr);
  rangeSwapBack(pt, pr);
  return true;
}

void ptInsertChars(PieceTable *pt, size_t index, const char *chars, size_t length) {
  assert(0 <= index && index < pt->sequence_length);
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
      PieceRange *oldPR = rangeCreate(pt, index, length, Insert, piece, piece, false);
      listAppend(&pt->undo_stack, oldPR);
      // next, create new Pieces
      Piece *leftPiece = pieceCreate(piece->offset, in_piece_offset, piece->which);
      Piece *newPiece = pieceCreate(add_offset, length, Add);
      Piece *rightPiece = pieceCreate(piece->offset + in_piece_offset, piece->length - in_piece_offset, piece->which);
      // link them up
      leftPiece->next = newPiece;
      newPiece->prev = leftPiece;
      newPiece->next = rightPiece;
      rightPiece->prev = newPiece;
      // add them to a PieceRange
      PieceRange newPR = {
        .first = leftPiece,
        .last = rightPiece,
        .boundary = false,
      };
      // swap the ranges
      rangeSwap(oldPR, &newPR);
    } else if (current_index == index) {
      // insert after this piece at boundary
      if (index == prev_end_index && pt->last_action == Insert) {
        // we can just extend the last Piece since our last insert ended here
        piece->length += length;
      } else {
        // add current state to undo stack
        PieceRange *oldPR = rangeCreate(pt, index, length, Insert, piece, piece->next, true);
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
    PieceRange *oldPR = rangeCreate(pt, index, length, Insert, pt->head, pt->head->next, true);
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

void ptDeleteChars(PieceTable *pt, size_t start_index, size_t end_index) {

}

void ptDeleteChar(PieceTable *pt, size_t index) {
  ptDeleteChars(pt, index, index + 1);
}

void ptReplaceChars(PieceTable *pt, size_t start_index, size_t end_index,
                    const char *chars, size_t length) {

}

void ptReplaceChar(PieceTable *pt, size_t index, char c) {
  ptReplaceChars(pt, index, index + 1, &c, 1);
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

