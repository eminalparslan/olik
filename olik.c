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

// Initial increase in capacity for each line in editor
#define EDITOR_LINES_CAP 8

/*
  TODO:
   - more cursor navigation
   - optimize scrolling rendering
   - free lines after closing file
   - skip list for lines
   - hide the cursor when repainting
   - syntax highlighting
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
  Lines lines;          // Contains the gap buffers for each line
  int width, height;    // Width and height of the terminal window
  int row, col;         // Row and col in terminal window
  int offset;           // Offset of the window from start of file
  enum EditorMode mode; // Current mode of the editor
  char *filename;       // Name of the open file
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
    gbWrite(STDERR_FILENO, e->lines.bufs[i], gbLen(e->lines.bufs[i]));
    fprintf(stderr, "\n");
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
void linesAppend(Editor *e, GapBuffer *buf) {
  // Grow lines capacity if needed
  if (e->lines.size >= e->lines.capacity) {
    e->lines.capacity = e->lines.capacity == 0 ? EDITOR_LINES_CAP : e->lines.capacity * 2;
    e->lines.bufs = (GapBuffer **) realloc(e->lines.bufs, e->lines.capacity * sizeof(GapBuffer *));
    assert(e->lines.bufs != NULL);
  }
  e->lines.bufs[e->lines.size++] = buf;
}

/* Insert a gap buffer in the lines at row. */
void linesInsert(Editor *e, GapBuffer *buf, int row) {
  row += e->offset;
  // Grow lines capacity if needed
  if (e->lines.size >= e->lines.capacity) {
    e->lines.capacity = e->lines.capacity == 0 ? EDITOR_LINES_CAP : e->lines.capacity * 2;
    e->lines.bufs = (GapBuffer **) realloc(e->lines.bufs, e->lines.capacity * sizeof(GapBuffer *));
    assert(e->lines.bufs != NULL);
  }
  // Shifts gap buffer pointers after pos by 1
  memmove(&(e->lines.bufs[row+1]), &(e->lines.bufs[row]), (e->lines.size - row) * sizeof(GapBuffer *));
  e->lines.bufs[row] = buf;
  e->lines.size++;
}

/* Deletes and frees the gap buffer at pos in lines. */
void linesDelete(Editor *e, int row) {
  row += e->offset;
  gbFree(e->lines.bufs[row]);
  // Shifts gap buffer pointers after pos back by 1
  memmove(&e->lines.bufs[row], &e->lines.bufs[row+1], (e->lines.size - row - 1) * sizeof(GapBuffer *));
  e->lines.size--;
}

/* Returns the gap buffer at the given row. */
GapBuffer *getLine(Editor *e, int row) {
  if (row < e->lines.size)
    return e->lines.bufs[row + e->offset];
  return NULL;
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
  for (int row = startRow; row < e->height; row++) {
    setCursorPos(row, 0);
    GapBuffer *gb = getLine(e, row);
    if (gb) gbWrite(STDOUT_FILENO, gb, gbLen(gb));
  }
  setCursorPos(e->row, e->col);
}

void renderScreen(Editor *e) {
  renderLinesAfter(e, 0);
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
  if (e->row + e->offset + n >= e->lines.size) return;

  if (e->row + n < e->height) {
    e->row += n;
    // Stay on the text
    int len = gbLen(getLine(e, e->row));
    if (e->col > len) {
      e->col = len;
    }
    setCursorPos(e->row, e->col);
  } else {
    e->offset += n;
    // Stay on the text
    int len = gbLen(getLine(e, e->row));
    if (e->col > len) {
      e->col = len;
    }
    renderScreen(e);
  }
}

/* Moves the cursor up by n. Scrolls if needed. */
void cursorUp(Editor *e, int n) {
  if (n <= 0 ) return;
  if (e->row + e->offset - n < 0) return;

  if (e->row - n >= 0) {
    e->row -= n;
    // Stay on the text
    int len = gbLen(getLine(e, e->row));
    if (e->col > len) {
      e->col = len;
    }
    setCursorPos(e->row, e->col);
  } else {
    e->offset -= n;
    // Stay on the text
    int len = gbLen(getLine(e, e->row));
    if (e->col > len) {
      e->col = len;
    }
    renderScreen(e);
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

/* Moves the cursor to the end of the line. */
void cursorLineEnd(Editor *e) {
  size_t len = gbLen(getLine(e, e->row));
  e->col = len;
  setCursorCol(e->col);
}

/* Moves the cursor to the start of the line. */
void cursorLineStart(Editor *e) {
  e->col = 0;
  setCursorCol(e->col);
}

/* Moves the cursor to the start of the text on the current line. */
void cursorTextStart(Editor *e) {
  GapBuffer *gb = getLine(e, e->row);
  size_t len = gbLen(gb);
  for (int i = 0; i < len; i++) {
    if (!isspace(gbGetChar(gb, i))) {
      e->col = i;
      setCursorCol(e->col);
      return;
    }
  }
}

/* Moves the cursor forward by a word. */
void cursorWordForward(Editor *e) {
  GapBuffer *gb = getLine(e, e->row);
  size_t len = gbLen(gb);
  for (int i = e->col; i < len - 1; i++) {
    if (isspace(gbGetChar(gb, i)) && isalnum(gbGetChar(gb, i+1))) {
      e->col = i + 1;
      setCursorCol(e->col);
      return;
    }
  }
  cursorLineEnd(e);
}

/* Moves the cursor backward by a word. */
void cursorWordBackward(Editor *e) {
  GapBuffer *gb = getLine(e, e->row);
  if (gbLen(gb) == e->col) e->col--;
  for (int i = e->col; i > 0; i--) {
    if (isspace(gbGetChar(gb, i)) && isalnum(gbGetChar(gb, i-1))) {
      e->col = i - 1;
      setCursorCol(e->col);
      return;
    }
  }
  cursorLineStart(e);
}

/* Moves the cursor to the next char c in the line. */
void cursorFindForward(Editor *e, char c) {
  GapBuffer *gb = getLine(e, e->row);
  size_t len = gbLen(gb);
  for (int i = e->col; i < len; i++) {
    if (gbGetChar(gb, i) == c) {
      e->col = i;
      setCursorCol(e->col);
      return;
    }
  }
}

/* Moves the cursor before the next char c in the line. */
void cursorFindToForward(Editor *e, char c) {
  GapBuffer *gb = getLine(e, e->row);
  size_t len = gbLen(gb);
  for (int i = e->col; i < len; i++) {
    if (gbGetChar(gb, i) == c) {
      e->col = i - 1;
      setCursorCol(e->col);
      return;
    }
  }
}

/* Moves the cursor backward to the next char c in the line. */
void cursorFindBackward(Editor *e, char c) {
  GapBuffer *gb = getLine(e, e->row);
  for (int i = e->col; i >= 0; i--) {
    if (gbGetChar(gb, i) == c) {
      e->col = i;
      setCursorCol(e->col);
      return;
    }
  }
}

/* Moves the cursor backward before the next char c in the line. */
void cursorFindToBackward(Editor *e, char c) {
  GapBuffer *gb = getLine(e, e->row);
  for (int i = e->col; i >= 0; i--) {
    if (gbGetChar(gb, i) == c) {
      e->col = i + 1;
      setCursorCol(e->col);
      return;
    }
  }
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
    linesDelete(e, e->row);
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

  if (e->row + e->offset == e->lines.size) {
    // If its the last line, just append
  linesAppend(e , gbNew);
  } else {
    // Otherwise, insert the new gap buffer
    linesInsert(e, gbNew, e->row + 1);
  }
  cursorDown(e, 1);
  e->col = 0;
  renderLinesAfter(e, e->row);
}

/* Load file into editor buffer. */
void loadFile(Editor *e) {
  FILE *fp;
  char *line = NULL;
  size_t len = 0;
  ssize_t read;

  fp = fopen(e->filename, "r");
  if (fp == NULL) die("fopen");

  while ((read = getline(&line, &len, fp)) != -1) {
    GapBuffer *gbNew = gbCreate();
    gbPushChars(gbNew, line, read - 1);
    linesAppend(e, gbNew);
  }

  fclose(fp);
  free(line);
  renderLinesAfter(e, 0);
}

/* Save editor buffer into file. */
void saveFile(Editor *e) {
  FILE *fp;
  fp = fopen(e->filename, "w");

  for (int i = 0; i < e->lines.size; i++) {
    gbfWrite(e->lines.bufs[i], fp);
  }

  fclose(fp);
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

/* Handle tab. */
void tab(Editor *e) {
  for (int i = 0; i < 2; i++) {
    writeCh(e, ' ');
  }
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
      case 'w':
        cursorWordForward(e);
        break;
      case 'b':
        cursorWordBackward(e);
        break;
      case '$':
        cursorLineEnd(e);
        break;
      case '^':
        cursorTextStart(e);
        break;
      case '0':
        cursorLineStart(e);
        break;
      case 'f':
        c = getCh();
        cursorFindForward(e, c);
        break;
      case 'F':
        c = getCh();
        cursorFindBackward(e, c);
        break;
      case 't':
        c = getCh();
        cursorFindToForward(e, c);
        break;
      case 'T':
        c = getCh();
        cursorFindToBackward(e, c);
        break;
      case ';':
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
          tab(e);
          break;
        default:
          //printf("%d", c);
          break;
      }
    }
  }
  return false;
}

/* Initializes the editor state. The editor should be allocated with calloc. */
void initEditor(Editor *e) {
  if (getWindowSize(&e->height, &e->width) == -1) die("getWindowSize");
}

void handleArgs(Editor *e, int argc, char *argv[]) {  
  if (argc == 1) {
    linesAppend(e, gbCreate());
  } else if (argc == 2) {
    e->filename = argv[1];
    if (strlen(e->filename) > 0) loadFile(e);
  }
}

int main(int argc, char *argv[]) {
  enableRawMode();
  clearScreen();

  Editor *e = (Editor *) calloc(1, sizeof(Editor));
  if (e == NULL) die("calloc");
  initEditor(e);
  handleArgs(e, argc, argv);

  bool quit = false;
  while (!quit) {
    quit = processChar(e);
    //debugEditor(e);
  };

  if (strlen(e->filename) > 0) saveFile(e);

  return 0;
}
