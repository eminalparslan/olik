#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include "gapbuffer.h"

#define CTRL_KEY(k) ((k) & 0x1f)

#define EDITOR_LINES_CAP 8

enum EditorMode { Normal, Insert };

struct Lines {
  struct GapBuffer **items;
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

struct termios initial_termios;

void clearScreen() {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
}

void die(const char *s) {
  clearScreen();
  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &initial_termios) == -1) die("tcsetattr");
}

void enableRawMode() {
  if (tcgetattr(STDIN_FILENO, &initial_termios) == -1) die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw_termios = initial_termios;
  raw_termios.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
  raw_termios.c_oflag &= ~(OPOST);
  raw_termios.c_cflag |= (CS8);
  raw_termios.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  raw_termios.c_cc[VMIN] = 0;
  raw_termios.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_termios) == -1) die("tcsetattr");
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
    l->items = (struct GapBuffer *) realloc(l->items, l->capacity * sizeof(*(l->items)));
    assert(l->items != NULL);
  }
  l->items[l->size++] = buf;
}

void initEditor(struct Editor *e) {
  gbInit(&e->lines);
  e->lines.capacity = 0;
  e->lines.size = 0;
  e->lines.items = NULL;

  if (getWindowSize(&e->height, &e->width) == -1) die("getWindowSize");

  e->row = 0;
  e->col = 0;
  e->offset = 0;
  e->mode = Normal;
  e->filename = "";
}

int min(int a, int b) {
  if (a < b) {
    return a;
  }
  return b;
}

void cursorLeft(struct Editor *e, int n) {
  if (e->col > 0) {
    write(STDOUT_FILENO, "\x1B[1D", 4);
    e->col--;
  }
}

void cursorDown(struct Editor *e, int n) {
  if (e->row - e->offset + n < min(e->height, e->lines.len - e->offset)) {
    e->row++;
    if (e->col > )
  }
}

char getCh() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }
  return c;
}

void processChar(struct Editor *e) {
  char c = getCh();
  if (e->mode == Normal) {
    switch (c) {
      case 'q':
        exit(0);
    }
  } else if (e->mode == Insert) {

  }
}

int main() {
  enableRawMode();

  struct Editor e;
  initEditor(&e);

  while (1) {
    processChar(&e);
  };

  return 0;
}
