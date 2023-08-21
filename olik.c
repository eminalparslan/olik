#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <termios.h>
#include <sys/ioctl.h>

#include "list.h"
#include "gapbuffer.h"

#define CTRL_KEY(k) ((k) & 0x1f)

// Rougly based on the Kilo text editor
// https://viewsourcecode.org/snaptoken/kilo/

/*
  TODO:
   - USE PIECE TABLE
     - after certain period of inactivity, save to disk and reload for short change list
   - repeat changes
   - truncate visible text to screen width
   - undo/redo
   - change/delete word
   - searching
   - zz position screen
   - syntax highlighting
   - status bar
   - selecting, copying, pasting
   - replace/delete char
   - deal with tabs
   - skip list for lines
   - free lines after closing file
*/

// Stores the lines of the files as an array of gap buffers
typedef struct {
  GapBuffer **elems;
  size_t size;
  size_t capacity;
} Lines;

// Command pattern for undo/redo based on line changes:
// https://en.wikipedia.org/wiki/Undo#Undo_implementation

enum CommandType {
  LineChange,
  LineCreate,
  LineDestroy,
};

// Information on the change
typedef struct {
  enum CommandType type;
  GapBuffer *buf;
  int line;
} Command;

// List of commands
typedef struct {
  Command **elems;
  size_t size;
  size_t capacity;
} Commands;

enum EditorMode { Normal, Insert };

// Editor state and contents
typedef struct {
  Lines lines;          // Contains the gap buffers for each line
  int width, height;    // Width and height of the terminal window
  int row, col;         // Row and col in terminal window
  int offset;           // Offset of the window from start of file
  enum EditorMode mode; // Current mode of the editor
  bool fileOpen;        // Whether a file is open
  char *fileName;       // Name of the open file
  Commands commands;    // History (stack) of commands for undo/redo
  int cmdPos;           // Position of the current command
} Editor;

struct termios orig_termios;

// ANSI escape wrapper functions
// https://gist.github.com/fnky/458719343aabd01cfb17a3a4f7296797
void eraseScreen(void) { printf("\x1b[2J"); }
void eraseRestScreen(void) { printf("\x1B[0J"); }
void eraseLine(void) { printf("\x1B[2K\r"); }
void moveCursorHome(void) { printf("\x1b[H"); }
void moveCursorLeft(int n) { printf("\x1B[%dD", n); }
void moveCursorRight(int n) { printf("\x1B[%dC", n); }
void setCursorPos(int row, int col) { printf("\x1B[%d;%df", row+1, col+1); }
void setCursorCol(int col) { printf("\x1B[%dG", col+1); }
void hideCursor(void) { printf("\x1B[?25l"); }
void showCursor(void) { printf("\x1B[?25h"); }

/* Prints the editor content to stderr. */
void debugEditor(Editor *e) {
  fprintf(stderr, "struct Editor {\n");
  fprintf(stderr, "  struct Lines {\n");
  for (int i = 0; i < e->lines.size; i++) {
    fprintf(stderr, "    %d: ", i);
    gbPrint(e->lines.elems[i], stdout);
    fprintf(stderr, "\n");
  }
  fprintf(stderr, "  }\n");
  fprintf(stderr, "  width: %d, height: %d\n", e->width, e->height);
  fprintf(stderr, "  row: %d, col: %d, offset: %d\n", e->row, e->col, e->offset);
  fprintf(stderr, "  mode: %s\n", e->mode == Normal ? "Normal" : "Insert");
  fprintf(stderr, "}\n");
}

/* Clears the screen and moves the cursor home. */
void clearScreen(void) {
  eraseScreen();
  moveCursorHome();
}

/* Helper function for error checking. */
void die(const char *s) {
  clearScreen();
  perror(s);
  exit(1);
}

/* Disables raw mode for termios. Called at program exit. */
void disableRawMode(void) {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) die("tcsetattr");
  clearScreen();
}

/* Enables raw mode and saves original termios state. */
void enableRawMode(void) {
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
  // Turn off stdout buffer
  setvbuf(stdout, NULL, _IONBF, 0);
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
  listAppend(&e->lines, buf);
}

/* Insert a gap buffer in the lines at row. */
void linesInsert(Editor *e, GapBuffer *buf, int row) {
  row += e->offset;
  listInsert(&e->lines, buf, row);
}

/* Deletes and frees the gap buffer at pos in lines. */
void linesDelete(Editor *e, int row) {
  row += e->offset;
  gbFree(e->lines.elems[row]);
  listDelete(&e->lines, row);
}

Command *cmdCreate(enum CommandType type, int line, GapBuffer *gb) {
  Command *cmd = malloc(sizeof(Command));
  cmd->type = type;
  cmd->line = line;
  cmd->buf = gbCopy(gb);
  return cmd;
}

void cmdPush(Editor *e, Command *cmd) {
  listAppend(&e->commands, cmd);
  e->cmdPos++;
}

/* Returns the gap buffer at the given row. */
GapBuffer *getRow(Editor *e, int row) {
  if (row < e->lines.size)
    return e->lines.elems[row + e->offset];
  return NULL;
}

/* Renders the current line. */
void renderLine(Editor *e) {
  eraseLine();
  GapBuffer *gb = getRow(e, e->row);
  gbPrint(gb, stdout);
  setCursorCol(e->col);
}

/* Render all the lines after startRow. */
void renderLinesAfter(Editor *e, int startRow) {
  hideCursor();
  setCursorPos(startRow, 0);
  eraseRestScreen();
  for (int row = startRow; row < e->height; row++) {
    setCursorPos(row, 0);
    GapBuffer *gb = getRow(e, row);
    if (gb) gbPrint(gb, stdout);
  }
  setCursorPos(e->row, e->col);
  showCursor();
}

void renderScreen(Editor *e) {
  renderLinesAfter(e, 0);
}

/* Get the next character input. */
char getCh(void) {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }
  return c;
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
    int len = gbLen(getRow(e, e->row));
    if (e->col > len) {
      e->col = len;
    }
    setCursorPos(e->row, e->col);
  } else {
    e->offset += n;
    // Stay on the text
    int len = gbLen(getRow(e, e->row));
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
    int len = gbLen(getRow(e, e->row));
    if (e->col > len) {
      e->col = len;
    }
    setCursorPos(e->row, e->col);
  } else {
    e->offset -= n;
    // Stay on the text
    int len = gbLen(getRow(e, e->row));
    if (e->col > len) {
      e->col = len;
    }
    renderScreen(e);
  }
}

/* Moves the cursor right by n. */
void cursorRight(Editor *e, int n) {
  if (n <= 0 ) return;
  if (e->col + n - 1 < gbLen(getRow(e, e->row))) {
    moveCursorRight(n);
    e->col += n;
  }
}

/* Moves the cursor to the end of the line. */
void cursorLineEnd(Editor *e) {
  size_t len = gbLen(getRow(e, e->row));
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
  GapBuffer *gb = getRow(e, e->row);
  size_t len = gbLen(gb);
  for (int i = 0; i < len; i++) {
    if (!isspace(gbGetChar(gb, i))) {
      e->col = i;
      setCursorCol(e->col);
      return;
    }
  }
}

/* Moves the cursor to the end of the line and goes into insert mode. */
void cursorLineEndInsert(Editor *e) {
  cursorLineEnd(e);
  e->mode = Insert;
}

/* Moves the cursor to the end of the line and goes into insert mode. */
void cursorLineStartInsert(Editor *e) {
  cursorLineStart(e);
  e->mode = Insert;
}

/* Moves the cursor forward by a word. */
void cursorWordForward(Editor *e) {
  GapBuffer *gb = getRow(e, e->row);
  size_t len = gbLen(gb);
  if (len == 0) return;
  for (int i = e->col + 1; i < len - 1; i++) {
    if (isspace(gbGetChar(gb, i)) && !isspace(gbGetChar(gb, i+1))) {
      e->col = i + 1;
      setCursorCol(e->col);
      return;
    }
  }
  cursorLineEnd(e);
}

/* Moves the cursor backward by a word. */
void cursorWordBackward(Editor *e) {
  GapBuffer *gb = getRow(e, e->row);
  size_t len = gbLen(gb);
  if (len == 0) return;
  if (len == e->col) e->col--;
  for (int i = e->col; i > 0; i--) {
    if (isspace(gbGetChar(gb, i)) && !isspace(gbGetChar(gb, i-1))) {
      e->col = i - 1;
      setCursorCol(e->col);
      return;
    }
  }
  cursorLineStart(e);
}

/* Moves the cursor to the next char c in the line. */
void cursorFindForward(Editor *e) {
  char c = getCh();
  GapBuffer *gb = getRow(e, e->row);
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
void cursorFindToForward(Editor *e) {
  char c = getCh();
  GapBuffer *gb = getRow(e, e->row);
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
void cursorFindBackward(Editor *e) {
  char c = getCh();
  GapBuffer *gb = getRow(e, e->row);
  for (int i = e->col; i >= 0; i--) {
    if (gbGetChar(gb, i) == c) {
      e->col = i;
      setCursorCol(e->col);
      return;
    }
  }
}

/* Moves the cursor backward before the next char c in the line. */
void cursorFindToBackward(Editor *e) {
  char c = getCh();
  GapBuffer *gb = getRow(e, e->row);
  for (int i = e->col; i >= 0; i--) {
    if (gbGetChar(gb, i) == c) {
      e->col = i + 1;
      setCursorCol(e->col);
      return;
    }
  }
}

void cursorHome(Editor *e) {
  e->row = 0;
  e->col = 0;
  moveCursorHome();
}

/* Scrolls the screen half a page down. */
void scrollHalfPageDown(Editor *e) {
  e->offset += e->height / 2;
  if (e->offset + e->row > e->lines.size) {
    e->offset = e->lines.size - 1;
    cursorHome(e);
  }
  renderScreen(e);
}

/* Scrolls the screen half a page up. */
void scrollHalfPageUp(Editor *e) {
  e->offset -= e->height / 2;
  if (e->offset < 0) e->offset = 0;
  renderScreen(e);
}

/* Scrolls the screen a page down. */
void scrollPageDown(Editor *e) {
  e->offset += e->height;
  if (e->offset > e->lines.size) {
    e->offset = e->lines.size - 1;
    cursorHome(e);
  }
  renderScreen(e);
}

/* Scrolls the screen a page up. */
void scrollPageUp(Editor *e) {
  e->offset -= e->height;
  if (e->offset < 0) e->offset = 0;
  renderScreen(e);
}

/* Scroll the screen a line down. */
void scrollLineDown(Editor *e) {
  e->offset += 1;
  if (e->offset > e->lines.size) {
    e->offset = e->lines.size - 1;
    cursorHome(e);
  }
  renderScreen(e);
}

/* Scroll the screen a line up. */
void scrollLineUp(Editor *e) {
  e->offset -= 1;
  if (e->offset < 0) e->offset = 0;
  renderScreen(e);
}

/* Handle backspace. */
void backspace(Editor *e) {
  GapBuffer *gbCur = getRow(e, e->row);
  if (e->col == 0) {
    if (e->row == 0) return;
    // Backspace at start of line
    GapBuffer *gbPrev = getRow(e, e->row - 1);
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
  GapBuffer *gbCur = getRow(e, e->row);
  gbMoveGap(gbCur, e->col);
  GapBuffer *gbNew = gbCreate();
  // Split the current line at col, and put the second half in gbNew
  gbSplit(gbNew, gbCur);
  renderLine(e);

  if (e->row + e->offset == e->lines.size) {
    // If its the last line, just append
    listAppend(&e->lines, gbNew);
  } else {
    // Otherwise, insert the new gap buffer
    linesInsert(e, gbNew, e->row + 1);
  }
  cursorDown(e, 1);
  e->col = 0;
  renderLinesAfter(e, e->row);
}

/* Creates a new line on the next line. */
void newLineNext(Editor *e) {
  GapBuffer *gbNew = gbCreate();
  if (e->row + e->offset == e->lines.size) {
    // If its the last line, just append
    listAppend(&e->lines, gbNew);
  } else {
    // Otherwise, insert the new gap buffer
    listInsert(&e->lines, gbNew, e->row + e->offset + 1);
  }
  cursorDown(e, 1);
  e->col = 0;
  renderLinesAfter(e, e->row);
  e->mode = Insert;
}

/* Creates a new line on the current line. */
void newLineCurrent(Editor *e) {
  GapBuffer *gbNew = gbCreate();
  listInsert(&e->lines, gbNew, e->row + e->offset);
  e->col = 0;
  renderLinesAfter(e, e->row);
  e->mode = Insert;
}

/* Load file into editor buffer. */
void loadFile(Editor *e) {
  FILE *fp;
  char *line = NULL;
  size_t len = 0;
  ssize_t read;

  fp = fopen(e->fileName, "r");
  if (fp == NULL) die("fopen");

  while ((read = getline(&line, &len, fp)) != -1) {
    GapBuffer *gbNew = gbCreate();
    gbPushChars(gbNew, line, read - 1);
    listAppend(&e->lines, gbNew);
  }

  fclose(fp);
  free(line);
  renderLinesAfter(e, 0);
}

/* Save editor buffer into file. */
void saveFile(Editor *e) {
  FILE *fp;
  fp = fopen(e->fileName, "w");

  for (int i = 0; i < e->lines.size; i++) {
    gbPrint(e->lines.elems[i], fp);
    fprintf(fp, "\n");
  }

  fclose(fp);
}

/* Write a character to the terminal screen. */
void writeCh(Editor *e, char ch) {
  GapBuffer *gb = getRow(e, e->row);
  int lineLength = gbLen(gb);
  assert(e->col <= lineLength);

  // TODO: free these lines
  cmdPush(e, cmdCreate(LineChange, e->row + e->offset, gb));

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

/* Handle tab. */
void tab(Editor *e) {
  for (int i = 0; i < 2; i++) {
    writeCh(e, ' ');
  }
}

/* Deletes the line. */
void deleteLine(Editor *e) {
  linesDelete(e, e->row);
  renderLinesAfter(e, e->row);
  if (e->row + e->offset == e->lines.size) cursorUp(e, 1);
  e->col = 0;
}

/* Delete handler. */
void delete(Editor *e) {
  char c = getCh();
  if (c == 'd') deleteLine(e);
}

/* Clears the line and goes into insert mode. */
void changeLine(Editor *e) {
  deleteLine(e);
  newLineCurrent(e);
  e->mode = Insert;
}

/* Change handler. */
void change(Editor *e) {
  char c = getCh();
  if (c == 'c') changeLine(e);
}

void deleteRestLine(Editor *e) {
  GapBuffer *gb = getRow(e, e->row);
  gbMoveGap(gb, e->col);
  gbClearTail(gb);
  renderLine(e);
}

void changeRestLine(Editor *e) {
  deleteRestLine(e);
  e->mode = Insert;
}

void undo(Editor *e) {
  // recursive call
  if (e->cmdPos <= 0) return;
  Command *cmd = e->commands.elems[--e->cmdPos];

  // Move screen so that line is at middle
  e->offset = cmd->line - e->height / 2;
  if (e->offset < 0) e->offset = 0;

  switch (cmd->type) {
    case LineChange:
      e->lines.elems[cmd->line] = cmd->buf;
      break;
    case LineCreate:
      break;
    case LineDestroy:
      break;
    default: return;
  }

  renderScreen(e);
  size_t cols = gbLen(getRow(e, e->row));
  if (e->col > cols) {
    e->col = cols;
    setCursorCol(e->col);
  }
}

/* Handle the next character input. */
bool processChar(Editor *e, char c) {
  if (e->mode == Normal) {
    // Deal with normal mode keys
    switch (c) {
      case 'q':
        if (getCh() == 'q') return true;
      case 's':
        if (e->fileOpen) saveFile(e); break;
      case 'i':
        e->mode = Insert; break;
      case 'I':
        cursorLineStartInsert(e); break;
      case 'h':
        cursorLeft(e, 1); break;
      case 'j':
        cursorDown(e, 1); break;
      case 'k':
        cursorUp(e, 1); break;
      case 'l':
        cursorRight(e, 1); break;
      case 'w':
        cursorWordForward(e); break;
      case 'b':
        cursorWordBackward(e); break;
      case '$':
        cursorLineEnd(e); break;
      case '^':
        cursorTextStart(e); break;
      case '0':
        cursorLineStart(e); break;
      case 'f':
        cursorFindForward(e); break;
      case 'F':
        cursorFindBackward(e); break;
      case 't':
        cursorFindToForward(e); break;
      case 'T':
        cursorFindToBackward(e); break;
      case ';':
        // TODO: repeat last find char
        break;
      case 'u':
        undo(e); break;
      case 'o':
        newLineNext(e); break;
      case 'O':
        newLineCurrent(e); break;
      case 'a':
        cursorRight(e, 1);
        e->mode = Insert;
        break;
      case 'A':
        cursorLineEndInsert(e); break;
      case 'd':
        delete(e); break;
      case 'c':
        change(e); break;
      case 'D':
        deleteRestLine(e); break;
      case 'C':
        changeRestLine(e); break;
      case CTRL_KEY('d'):
        scrollHalfPageDown(e); break;
      case CTRL_KEY('u'):
        scrollHalfPageUp(e); break;
      case CTRL_KEY('f'):
        scrollPageDown(e); break;
      case CTRL_KEY('b'):
        scrollPageUp(e); break;
      case CTRL_KEY('e'):
        scrollLineDown(e); break;
      case CTRL_KEY('y'):
        scrollLineUp(e); break;
      default: break;
    }
  } else if (e->mode == Insert) {
    // Deal with insert mode keys
    if (isprint(c)) {
      writeCh(e, c);
    } else {
      switch (c) {
        case 27: // Esc
          e->mode = Normal; break;
        case 127: // Backspace
          backspace(e); break;
        case 13: // Enter
          newLine(e); break;
        case 9: // Tab
          tab(e); break;
        default:
          fprintf(stderr, "%d\n", c); break;
      }
    }
  }
  return false;
}

/* Initializes the editor state. The editor should be allocated with calloc. */
void initEditor(Editor *e) {
  if (getWindowSize(&e->height, &e->width) == -1) die("getWindowSize");
}

int main(int argc, char *argv[]) {
  enableRawMode();
  clearScreen();

  Editor *e = (Editor *) calloc(1, sizeof(Editor));
  if (e == NULL) die("calloc");
  initEditor(e);

  if (argc == 1) {
    listAppend(&e->lines, gbCreate());
    e->fileOpen = false;
  } else if (argc == 2) {
    e->fileName = argv[1];
    e->fileOpen = true;
    loadFile(e);
  }

  bool quit = false;
  while (!quit) {
    quit = processChar(e, getCh());
    //debugEditor(e);
  };

  return 0;
}
