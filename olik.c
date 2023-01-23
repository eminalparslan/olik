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

// Starting capacity of lines in editor
#define EDITOR_LINES_CAP 8

/*
  TODO:
   - opening and saving files
   - free lines after closing file
   - skip list for lines
   - error checking
*/

enum EditorMode { Normal, Insert };

// Stores the lines of the files as an arary of gap buffers
typedef struct {
  GapBuffer **bufs;
  size_t size;
  size_t capacity;
} Lines;

// Editor state and contents
typedef struct {
  Lines lines;
  int width, height;
  int row, col, offset;
  enum EditorMode mode;
  char *filename;
} Editor;

struct termios orig_termios;

// ANSI escape wrapper functions: https://gist.github.com/fnky/458719343aabd01cfb17a3a4f7296797
void eraseScreen() { printf("\x1b[2J"); }
void eraseRestScreen() { printf("\x1B[0J"); }
void eraseLine() { printf("\x1B[2K\r"); }
void moveCursorHome() { printf("\x1b[H"); }
void moveCursorLeft(int n) { printf("\x1B[%dD", n); }
void moveCursorRight(int n) { printf("\x1B[%dC", n); }
void setCursorPos(int row, int col) { printf("\x1B[%d;%df", row+1, col+1); }
void setCursorCol(int col) { printf("\x1B[%dG", col+1); }

/* Prints the editor content to stderr. */
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

/* Clears the screen and moves the cursor home. */
void clearScreen() {
  eraseScreen();
  moveCursorHome();
}

/* Helper function for error checking. */
void die(const char *s) {
  clearScreen();
  perror(s);
  exit(1);
}

/* Disables raw mode. Called at program exit. */
void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) die("tcsetattr");
  clearScreen();
}

/* Enables raw mode and saves original termios state. */
void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
  // Restore terminal to original state at program exit
  atexit(disableRawMode);

  // Raw mode settings
  struct termios raw_termios = orig_termios;
  raw_termios.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
  raw_termios.c_oflag &= ~(OPOST);
  raw_termios.c_cflag |= (CS8);
  raw_termios.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  raw_termios.c_cc[VMIN] = 0;
  raw_termios.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_termios) == -1) die("tcsetattr");
  // turn off stdout buffer
  setbuf(stdout, NULL);
}

/* Gets the window size of the terminal in rows and cols. */
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

/* Append a gap buffer to the lines. */
void linesAppend(Lines *l, GapBuffer *buf) {
  // Grow lines capacity if needed
  if (l->size >= l->capacity) {
    l->capacity = l->capacity == 0 ? EDITOR_LINES_CAP : l->capacity * 2;
    l->bufs = (GapBuffer **) realloc(l->bufs, l->capacity * sizeof(*(l->bufs)));
    assert(l->bufs != NULL);
  }
  l->bufs[l->size++] = buf;
}

/* Insert a gap buffer in the lines at pos. */
void linesInsert(Lines *l, GapBuffer *buf, int pos) {
  // Grow lines capacity if needed
  if (l->size >= l->capacity) {
    l->capacity = l->capacity == 0 ? EDITOR_LINES_CAP : l->capacity * 2;
    l->bufs = (GapBuffer **) realloc(l->bufs, l->capacity * sizeof(*(l->bufs)));
    assert(l->bufs != NULL);
  }
  // Shifts gap buffer pointers after pos by 1
  memmove(&l->bufs[pos+1], &l->bufs[pos], (l->size - pos) * sizeof(*(l->bufs)));
  l->bufs[pos] = buf;
  l->size++;
}

/* Deletes and frees the gap buffer at pos in lines. */
void linesDelete(Lines *l, int pos) {
  gbFree(l->bufs[pos]);
  // Shifts gap buffer pointers after pos back by 1
  memmove(&l->bufs[pos], &l->bufs[pos+1], (l->size - pos - 1) * sizeof(*(l->bufs)));
  l->size--;
}

/* Returns the gap buffer at the given row. */
GapBuffer *getLine(Editor *e, int row) {
  if (row < e->lines.size)
    return e->lines.bufs[row];
  return NULL;
}

/* Initializes the editor state. The editor should be allocated with calloc. */
void initEditor(Editor *e) {
  linesAppend(&e->lines, gbCreate());
  if (getWindowSize(&e->height, &e->width) == -1) die("getWindowSize");
}

/* Helper min function. */
int min(int a, int b) {
  if (a < b) {
    return a;
  }
  return b;
}

/* Moves the cursor left by n. */
void cursorLeft(Editor *e, int n) {
  if (n <= 0 ) return;
  if (e->col - n >= 0) {
    moveCursorLeft(n);
    e->col -= n;
  }
}

/* Moves the cursor down by n. Scrolls if needed. */
void cursorDown(Editor *e, int n) {
  if (n <= 0 ) return;
  if (e->row - e->offset + n < min(e->height, e->lines.size - e->offset)) {
    e->row += n;
    // Stay on the text
    int len = gbLen(getLine(e, e->row));
    if (e->col > len) {
      e->col = len;
    }
    setCursorPos(e->row, e->col);
  } else {
    // TODO: scroll
  }
}

/* Moves the cursor up by n. Scrolls if needed. */
void cursorUp(Editor *e, int n) {
  if (n <= 0 ) return;
  if (e->row - e->offset - n >= 0) {
    e->row -= n;
    // Stay on the text
    int len = gbLen(getLine(e, e->row));
    if (e->col > len) {
      e->col = len;
    }
    setCursorPos(e->row, e->col);
  } else {
    // TODO: scroll
  }
}

/* Moves the cursor right by n. */
void cursorRight(Editor *e, int n) {
  if (n <= 0 ) return;
  if (e->col < gbLen(getLine(e, e->row))) {
    moveCursorRight(n);
    e->col += n;
  }
}

/* Renders the current line. */
void renderLine(Editor *e) {
  eraseLine();
  GapBuffer *gb = getLine(e, e->row);
  gbWrite(STDOUT_FILENO, gb, gbLen(gb));
  setCursorCol(e->col);
}

/* Render all the lines after startRow. */
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

/* Handle backspace. */
void backspace(Editor *e) {
  GapBuffer *gbCur = getLine(e, e->row);
  if (e->col == 0) {
    if (e->row == 0) return;
    // Backspace at start of line
    GapBuffer *gbPrev = getLine(e, e->row - 1);
    size_t prevLen = gbLen(gbPrev);
    // Append current line to the end of previous line
    gbConcat(gbPrev, gbCur);
    // Delete current line
    linesDelete(&e->lines, e->row);
    // Move cursor up and to the end of original text
    cursorUp(e, 1);
    cursorRight(e, prevLen);
    // Render new lines
    renderLinesAfter(e, e->row);
  } else if (e->col == gbLen(gbCur)) {
    // Backspace at end of line
    gbPopChar(gbCur);
    e->col--;
    renderLine(e);
  } else if (e->col < gbLen(gbCur)) {
    // Backspace in the middle of the line
    gbMoveGap(gbCur, e->col);
    gbDeleteChar(gbCur);
    e->col--;
    renderLine(e);
  }
}

/* Handle new line (enter). */
void newLine(Editor *e) {
  GapBuffer *gbCur = getLine(e, e->row);
  gbMoveGap(gbCur, e->col);
  GapBuffer *gbNew = gbCreate();
  // Split the current line at col, and put the second half in gbNew
  gbSplit(gbNew, gbCur);
  renderLine(e);

  if (e->row == e->lines.size) {
    // If its the last line, just append
    linesAppend(&e->lines, gbNew);
  } else {
    // Otherwise, insert the new gap buffer
    linesInsert(&e->lines, gbNew, e->row + 1);
  }
  cursorDown(e, 1);
  e->col = 0;
  renderLinesAfter(e, e->row);
}

/* Load file into editor buffer. */
void loadFile(Editor *e) {

}

/* Save editor buffer into file. */
void saveFile(Editor *e) {

}

/* Write a character to the terminal screen. */
void writeCh(Editor *e, char ch) {
  GapBuffer *gb = getLine(e, e->row);
  int lineLength = gbLen(gb);
  assert(e->col <= lineLength);

  if (e->col == lineLength) {
    // If at the end, just append
    gbPushChar(gb, ch);
  } else {
    // Otherwise, insert
    gbMoveGap(gb, e->col);
    gbInsertChar(gb, ch);
  }
  e->col++;
  renderLine(e);
}

/* Get the next character input. */
char getCh() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }
  return c;
}

/* Handle the next character input. */
bool processChar(Editor *e) {
  char c = getCh();
  if (e->mode == Normal) {
    // Deal with normal node keys.
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
    // Deal with insert mode keys.
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
          //printf("%d", c);
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
  if (e == NULL) die("Allocation failed\n");
  initEditor(e);

  bool quit = false;
  while (!quit) {
    quit = processChar(e);
    //debugEditor(e);
  };

  return 0;
}
