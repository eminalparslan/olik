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
#define DEBUG 1

enum EditorMode { Normal, Insert };

/*
  TODO:
   - working prototype
   - free lines after closing file
   - skip list for lines
   - error checking
*/

typedef struct {
  GapBuffer **bufs;
  size_t size;
  size_t capacity;
} Lines;

typedef struct {
  Lines lines;
  int width, height;
  int row, col, offset;
  enum EditorMode mode;
  char *filename;
} Editor;

struct termios orig_termios;

void eraseScreen() { printf("\x1b[2J"); }
void eraseRestScreen() { printf("\x1B[0J"); }
void eraseLine() { printf("\x1B[2K\r"); }
void moveCursorHome() { printf("\x1b[H"); }
void moveCursorLeft(int n) { printf("\x1B[%dD", n); }
void moveCursorRight(int n) { printf("\x1B[%dC", n); }
void setCursorPos(int row, int col) { printf("\x1B[%d;%df", row+1, col+1); }
void setCursorCol(int col) { printf("\x1B[%dG", col+1); }

void debugEditor(Editor *e) {
  fprintf(stderr, "struct Editor {\n");
  fprintf(stderr, "  struct Lines {\n");
  for (int i = 0; i < e->lines.size; i++) {
    fprintf(stderr, "    %d: ", i);
    gbPrint(e->lines.bufs[i], STDERR_FILENO);
  }
  fprintf(stderr, "  }\n");
  fprintf(stderr, "  width: %d, height: %d\n", e->width, e->height);
  fprintf(stderr, "  row: %d, col: %d, offset: %d\n", e->row, e->col, e->offset);
  fprintf(stderr, "  mode: %s\n", e->mode == Normal ? "Normal" : "Insert");
  fprintf(stderr, "}\n");
}

void clearScreen() {
  eraseScreen();
  moveCursorHome();
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

void linesAppend(Lines *l, GapBuffer *buf) {
  if (l->size >= l->capacity) {
    l->capacity = l->capacity == 0 ? EDITOR_LINES_CAP : l->capacity * 2;
    l->bufs = (GapBuffer **) realloc(l->bufs, l->capacity * sizeof(*(l->bufs)));
    assert(l->bufs != NULL);
  }
  l->bufs[l->size++] = buf;
}

void linesInsert(Lines *l, GapBuffer *buf, int pos) {
  if (l->size >= l->capacity) {
    l->capacity = l->capacity == 0 ? EDITOR_LINES_CAP : l->capacity * 2;
    l->bufs = (GapBuffer **) realloc(l->bufs, l->capacity * sizeof(*(l->bufs)));
    assert(l->bufs != NULL);
  }
  memmove(&l->bufs[pos+1], &l->bufs[pos], (l->size - pos) * sizeof(*(l->bufs)));
  l->bufs[pos] = buf;
  l->size++;
}

void linesDelete(Lines *l, int pos) {
  gbFree(l->bufs[pos]);
  memmove(&l->bufs[pos], &l->bufs[pos+1], (l->size - pos - 1) * sizeof(*(l->bufs)));
  l->size--;
}

GapBuffer *getLine(Editor *e, int row) {
  if (row < e->lines.size)
    return e->lines.bufs[row];
  return NULL;
}

void initEditor(Editor *e) {
  linesAppend(&e->lines, gbCreate());
  if (getWindowSize(&e->height, &e->width) == -1) die("getWindowSize");
}

int min(int a, int b) {
  if (a < b) {
    return a;
  }
  return b;
}

void cursorLeft(Editor *e, int n) {
  if (e->col - n >= 0) {
    moveCursorLeft(n);
    e->col -= n;
  }
}

void cursorDown(Editor *e, int n) {
  if (e->row - e->offset + n < min(e->height, e->lines.size - e->offset)) {
    e->row += n;
    int len = gbLen(getLine(e, e->row));
    if (e->col > len) {
      e->col = len;
    }
    setCursorPos(e->row, e->col);
  } else {
    // TODO: scroll
  }
}

void cursorUp(Editor *e, int n) {
  if (e->row - e->offset - n >= 0) {
    e->row -= n;
    int len = gbLen(getLine(e, e->row));
    if (e->col > len) {
      e->col = len;
    }
    setCursorPos(e->row, e->col);
  } else {
    // TODO: scroll
  }
}

void cursorRight(Editor *e, int n) {
  if (e->col < gbLen(getLine(e, e->row))) {
    moveCursorRight(n);
    e->col += n;
  }
}

void renderLine(Editor *e) {
  eraseLine();
  GapBuffer *gb = getLine(e, e->row);
  gbWrite(STDOUT_FILENO, gb, gbLen(gb));
  setCursorCol(e->col);
}

void renderLinesAfter(Editor *e, int startRow) {
  setCursorPos(startRow, 0);
  eraseRestScreen();
  for (int row = startRow; row < e->height + e->offset; row++) {
    setCursorPos(row, 0);
    GapBuffer *gb = getLine(e, row);
    if (gb) gbWrite(STDOUT_FILENO, gb, gbLen(gb));
  }
  setCursorPos(e->row, e->col);
}

void backspace(Editor *e) {
  GapBuffer *gbCur = getLine(e, e->row);
  if (e->col == 0) {
    if (e->row == 0) return;
    GapBuffer *gbPrev = getLine(e, e->row - 1);
    gbConcat(gbPrev, gbCur);
    linesDelete(&e->lines, e->row);
    cursorUp(e, 1);
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

void newLine(Editor *e) {
  GapBuffer *gbCur = getLine(e, e->row);
  gbMoveGap(gbCur, e->col);
  GapBuffer *gbNew = gbCreate();
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

void loadFile(Editor *e) {

}

void saveFile(Editor *e) {

}

void writeCh(Editor *e, char ch) {
  GapBuffer *gb = getLine(e, e->row);
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

bool processChar(Editor *e) {
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
        case 27: // Esc
          e->mode = Normal;
          break;
        case 127: // Backspace
          backspace(e);
          break;
        case 13: // Enter
          newLine(e);
          break;
        case 9: // Tab
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

  Editor *e;
  e = (Editor *) calloc(1, sizeof(Editor));
  if (e == NULL) {
    fprintf(stderr, "Allocation failed\n");
    exit(1);
  }
  initEditor(e);

  bool quit = false;
  while (!quit) {
    quit = processChar(e);
    debugEditor(e);
  };

  clearScreen();
  return 0;
}
