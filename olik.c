#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <termios.h>
#include <sys/ioctl.h>
#include "gapbuffer.h"

#define CTRL_KEY(k) ((k) & 0x1f)

#define EDITOR_LINES_CAP 8

enum EditorMode { Normal, Insert };

/*
  TODO:
   - working prototype
   - free lines after closing file
   - skip list for lines
  FIXME:
   - inserting chars segfault
*/

struct Lines {
  struct GapBuffer **bufs;
  size_t size;
  size_t capacity;
};

struct Editor {
  struct Lines lines;
  int width, height;
  int row, col, offset;
  enum EditorMode mode;
  char *filename;
};

struct termios orig_termios;

void clearScreen() {
  printf("\x1b[2J");
  printf("\x1b[H");
  // write(STDOUT_FILENO, "\x1b[2J", 4);
  // write(STDOUT_FILENO, "\x1b[H", 3);
}

void die(const char *s) {
  clearScreen();
  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) die("tcsetattr");
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw_termios = orig_termios;
  raw_termios.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
  raw_termios.c_oflag &= ~(OPOST);
  raw_termios.c_cflag |= (CS8);
  raw_termios.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  raw_termios.c_cc[VMIN] = 0;
  raw_termios.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_termios) == -1) die("tcsetattr");

  setbuf(stdout, NULL);
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    return -1;
  } else {
    *rows = ws.ws_row;
    *cols = ws.ws_col;
    return 0;
  }
}

void linesAppend(struct Lines *l, struct GapBuffer *buf) {
  if (l->size >= l->capacity) {
    l->capacity = l->capacity == 0 ? EDITOR_LINES_CAP : l->capacity * 2;
    l->bufs = (struct GapBuffer **) realloc(l->bufs, l->capacity * sizeof(*(l->bufs)));
    assert(l->bufs != NULL);
  }
  l->bufs[l->size++] = buf;
}

void linesInsert(struct Lines *l, struct GapBuffer *buf, int pos) {
  if (l->size >= l->capacity) {
    l->capacity = l->capacity == 0 ? EDITOR_LINES_CAP : l->capacity * 2;
    l->bufs = (struct GapBuffer **) realloc(l->bufs, l->capacity * sizeof(*(l->bufs)));
    assert(l->bufs != NULL);
  }
  memmove(l->bufs + pos + 1, l->bufs + pos, l->size - pos);
  l->bufs[pos] = buf;
  l->size++;
}

void linesDelete(struct Lines *l, int pos) {
  gbFree(l->bufs[pos]);
  memmove(l->bufs + pos, l->bufs + pos + 1, l->size - pos - 1);
  l->size--;
}

struct GapBuffer *getLine(struct Editor *e, int row) {
  return e->lines.bufs[row];
}

void initEditor(struct Editor *e) {
  linesAppend(&e->lines, gbCreate());
  if (getWindowSize(&e->height, &e->width) == -1) die("getWindowSize");
}

int min(int a, int b) {
  if (a < b) {
    return a;
  }
  return b;
}

void cursorLeft(struct Editor *e, int n) {
  if (e->col - n >= 0) {
    printf("\x1B[%dD", n);
    e->col -= n;
  }
}

void cursorDown(struct Editor *e, int n) {
  if (e->row - e->offset + n < min(e->height, e->lines.size - e->offset)) {
    e->row += n;
    int len = gbLen(getLine(e, e->row));
    if (e->col > len) {
      e->col = len;
    }
    printf("\x1B[%d;%df", e->row+1, e->col+1);
  } else {
    // TODO: scroll
  }
}

void cursorUp(struct Editor *e, int n) {
  if (e->row - e->offset - n >= 0) {
    e->row -= n;
    int len = gbLen(getLine(e, e->row));
    if (e->col > len) {
      e->col = len;
    }
    printf("\x1B[%d;%df", e->row+1, e->col+1);
  } else {
    // TODO: scroll
  }
}

void cursorRight(struct Editor *e, int n) {
  if (e->col < gbLen(getLine(e, e->row))) {
    printf("\x1B[%dC", n);
    e->col += n;
  }
}

void renderLine(struct Editor *e) {
  printf("\x1B[2K\r");
  struct GapBuffer *gb = getLine(e, e->row);
  gbWrite(STDOUT_FILENO, gb, gbLen(gb));
}

void renderLinesAfter(struct Editor *e, int startRow) {
  printf("\x1B[%d;%df", startRow, 0);
  printf("\x1B[0J");
  for (int row = startRow; row < e->height + e->offset; row++) {
    printf("\x1B[%d;%df", row, 0);
    struct GapBuffer *gb = getLine(e, row);
    gbWrite(STDOUT_FILENO, gb, gbLen(gb));
  }
  printf("\x1B[%d;%df", e->row+1, e->col+1);
}

void backspace(struct Editor *e) {
  struct GapBuffer *gbCur = getLine(e, e->row);
  if (e->col == 0) {
    if (e->row == 0) return;
    struct GapBuffer *gbPrev = getLine(e, e->row - 1);
    gbConcat(gbCur, gbPrev);
    linesDelete(&e->lines, e->row);
    cursorUp(e, 1);
    cursorRight(e, gbLen(gbPrev));
    renderLinesAfter(e, e->row);
  } else if (e->col == gbLen(gbCur)) {
    gbPopChar(gbCur);
    e->col--;
    renderLine(e);
  } else if (e->col < gbLen(gbCur)) {
    gbMoveGap(gbCur, e->col);
    gbDeleteChar(gbCur);
    e->col--;
    renderLine(e);
  }
}

void newLine(struct Editor *e) {
  struct GapBuffer *gbCur = getLine(e, e->row);
  gbMoveGap(gbCur, e->col);

  struct GapBuffer *gbNew = gbCreate();
  gbSplit(gbNew, gbCur);
  renderLine(e);

  if (e->row == e->lines.size) {
    linesAppend(&e->lines, gbNew);
  } else {
    linesInsert(&e->lines, gbNew, e->row + 1);
  }
  cursorDown(e, 1);
  e->col = 0;
  renderLinesAfter(e, e->row);
}

void loadFile(struct Editor *e) {

}

void saveFile(struct Editor *e) {

}

void writeCh(struct Editor *e, char ch) {
  struct GapBuffer *gb = getLine(e, e->row);
  int lineLength = gbLen(gb);
  assert(e->col <= lineLength);
  if (e->col == lineLength) {
    gbPushChar(gb, ch);
  } else {
    gbMoveGap(gb, e->col);
    gbInsertChar(gb, ch);
  }
  e->col++;
  renderLine(e);
}

char getCh() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }
  return c;
}

bool processChar(struct Editor *e) {
  char c = getCh();
  if (e->mode == Normal) {
    switch (c) {
      case 'q':
        return true;
      case 'i':
        e->mode = Insert;
        break;
      case 'h':
        cursorLeft(e, 1);
        break;
      case 'j':
        cursorDown(e, 1);
        break;
      case 'k':
        cursorUp(e, 1);
        break;
      case 'l':
        cursorRight(e, 1);
        break;
      default:
        break;
    }
  } else if (e->mode == Insert) {
    if (isprint(c)) {
      writeCh(e, c);
    } else {
      switch (c) {
        case 27:
          e->mode = Normal;
          break;
        case 127:
          backspace(e);
          break;
        case 13:
          newLine(e);
          break;
        case 9:
          for (int i = 0; i < 2; i++) {
            writeCh(e, ' ');
          }
          break;
        default:
          printf("%d", c);
          break;
      }
    }
  }
  return false;
}

int main() {
  enableRawMode();
  clearScreen();

  struct Editor *e;
  e = (struct Editor *) calloc(1, sizeof(struct Editor));
  if (e == NULL) {
    fprintf(stderr, "Allocation failed\n");
    exit(1);
  }
  initEditor(e);

  bool quit = false;
  while (!quit) {
    quit = processChar(e);
  };

  clearScreen();
  return 0;
}
