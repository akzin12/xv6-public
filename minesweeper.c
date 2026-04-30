#include "types.h"
#include "user.h"
#include "mouse.h"
#include "fcntl.h"

#define ROWS     12
#define COLS     15
#define MINES    36       // 25% density
#define CELL     16       // grid size 14 - 16                  
#define GRID_X   76       // left margin of the grid                
#define GRID_Y   4        // top margin of the grid
#define SCR_W    320
#define SCR_H    200
#define SCR_SIZE (SCR_W*SCR_H)

// Left-panel buttons 
#define BTN_X    8
#define BTN_W    56
#define BTN_H    18
#define BTN_RST_Y  60
#define BTN_HNT_Y  85
#define BTN_EXT_Y 110

// VGA default-palette color indices
#define C_BLACK   0
#define C_BLUE    1
#define C_GREEN   2
#define C_CYAN    3
#define C_RED     4
#define C_BROWN   6
#define C_LGRAY   7
#define C_DGRAY   8
#define C_LBLUE   9
#define C_LRED   12
#define C_YELLOW 14
#define C_WHITE  15

// cell state
#define HIDDEN   0
#define REVEALED 1
#define FLAGGED  2

// global game state
static struct ulock game_lock;
static int dirty      = 0;
static int should_exit = 0;

static int  board[ROWS][COLS];
static int  cstate[ROWS][COLS];
static int  game_over   = 0;
static int  won         = 0;
static int  first_click = 1;
static int  flags_placed = 0;

static int  timer_seconds = 0;
static int  timer_running = 0;

static int cursor_x  = SCR_W / 2;
static int cursor_y  = SCR_H / 2;
static int prev_left = 0, prev_right = 0;

static int   display_fd = -1;
static char  screen[SCR_SIZE];

// lcg random
static unsigned int rseed;
static unsigned int rnext(void) {
  rseed = rseed * 1664525 + 1013904223;
  return rseed;
}

// 3x5 bitmap font 
// Indices 0-9 = digits 0-9
// Index 10=R (restart), 11=H (hint), 12=X (exit), 13=T (timer)
static const unsigned char font[14][5] = {
  {0x7,0x5,0x5,0x5,0x7}, // 0
  {0x2,0x6,0x2,0x2,0x7}, // 1
  {0x7,0x1,0x7,0x4,0x7}, // 2
  {0x7,0x1,0x7,0x1,0x7}, // 3
  {0x5,0x5,0x7,0x1,0x1}, // 4
  {0x7,0x4,0x7,0x1,0x7}, // 5
  {0x4,0x4,0x7,0x5,0x7}, // 6
  {0x7,0x1,0x1,0x2,0x2}, // 7
  {0x7,0x5,0x7,0x5,0x7}, // 8
  {0x7,0x5,0x7,0x1,0x7}, // 9
  {0x6,0x5,0x6,0x5,0x5}, // R
  {0x5,0x5,0x7,0x5,0x5}, // H
  {0x5,0x2,0x2,0x2,0x5}, // X
  {0x7,0x2,0x2,0x2,0x2}, // T
};

static const int num_color[9] = {
  C_LGRAY, C_BLUE, C_GREEN, C_LRED, C_LBLUE,
  C_RED, C_CYAN, C_BLACK, C_DGRAY
};

//  Drawing pixel at x,y with color c 
static void pset(int x, int y, int c) {
  if ((unsigned)x < SCR_W && (unsigned)y < SCR_H)
    screen[y*SCR_W + x] = (char)c;
}

// Fill a rectangle at (x,y) with width w and height h with color c
static void fillrect(int x, int y, int w, int h, int c) {
  for (int dy = 0; dy < h; dy++)
    for (int dx = 0; dx < w; dx++)
      pset(x+dx, y+dy, c);
}

// Draw a font glyph at (px,py) at 2x scale 3x5 px → 6x10 px 
// 1 pixel -> 2x2 block of pixels
static void draw_glyph(int idx, int px, int py, int col) {
  if (idx < 0 || idx >= 14) return;
  for (int row = 0; row < 5; row++)
    for (int bit = 0; bit < 3; bit++)
      if (font[idx][row] & (1 << (2-bit))) {
        pset(px + bit*2,     py + row*2,     col);
        pset(px + bit*2 + 1, py + row*2,     col);
        pset(px + bit*2,     py + row*2 + 1, col);
        pset(px + bit*2 + 1, py + row*2 + 1, col);
      }
}

// Draw a 2-digit decimal number (0-99), two glyphs side by side
static void draw_number2(int val, int px, int py, int col) {
  draw_glyph(val / 10, px,   py, col);
  draw_glyph(val % 10, px+7, py, col);
}

// Draw a 3-digit decimal number (0-999)
static void draw_number3(int val, int px, int py, int col) {
  if (val > 999) val = 999;
  draw_glyph(val / 100,       px,    py, col);
  draw_glyph((val / 10) % 10, px+7,  py, col);
  draw_glyph(val % 10,        px+14, py, col);
}

static void draw_cell(int r, int c) {
  int px = GRID_X + c*CELL;
  int py = GRID_Y + r*CELL;
  int s  = cstate[r][c];

  if (s == HIDDEN || s == FLAGGED) {
    fillrect(px,        py,        CELL, CELL, C_DGRAY);
    fillrect(px,        py,        CELL, 2,    C_LGRAY);
    fillrect(px,        py,        2,    CELL, C_LGRAY);
    fillrect(px,        py+CELL-2, CELL, 2,    C_BLACK);
    fillrect(px+CELL-2, py,        2,    CELL, C_BLACK);
    if (s == FLAGGED) {
      fillrect(px+7, py+2, 2, 10, C_BLACK); // flag pole
      fillrect(px+3, py+2, 4,  5, C_RED);   // flag
    }
  } else {
    int val = board[r][c];
    if (game_over && val == -1) {
      fillrect(px, py, CELL, CELL, C_RED);
      fillrect(px+4, py+4, 8, 8, C_BLACK);
    } else {
      fillrect(px, py, CELL, CELL, C_LGRAY);
      fillrect(px, py,    CELL, 1, C_DGRAY);
      fillrect(px, py,    1, CELL, C_DGRAY);
      if (val > 0)
        draw_glyph(val, px+5, py+3, num_color[val]);
    }
  }
}

// game logic
// count neighboring mines for cell (r,c)
static int neighbors(int r, int c) {
  int cnt = 0;
  for (int dr = -1; dr <= 1; dr++)
    for (int dc = -1; dc <= 1; dc++) {
      if (!dr && !dc) continue;
      int nr = r+dr, nc = c+dc;
      if (nr>=0 && nr<ROWS && nc>=0 && nc<COLS && board[nr][nc]==-1)
        cnt++;
    }
  return cnt;
}

// randomly place mines on the board
static void place_mines(int sr, int sc) {
  int placed = 0;
  while (placed < MINES) {
    int r = rnext() % ROWS; // random row
    int c = rnext() % COLS; // random col
    if (board[r][c] == -1) continue;
    // optional: to guarantee first-click safety
    // int dr = r-sr, dc = c-sc;
    // if (dr>=-1 && dr<=1 && dc>=-1 && dc<=1) continue;
    board[r][c] = -1;
    placed++;
  }
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      if (board[r][c] != -1)
        board[r][c] = neighbors(r, c); // count neighboring mines for non-mine cells
}

// reveal cell (r,c), if it's a zero, recursively reveal neighbors
static void reveal(int r, int c) {
  if (r<0||r>=ROWS||c<0||c>=COLS) return;
  if (cstate[r][c] != HIDDEN) return;
  cstate[r][c] = REVEALED;
  if (board[r][c] == 0)
    for (int dr=-1; dr<=1; dr++)
      for (int dc=-1; dc<=1; dc++)
        if (dr||dc) reveal(r+dr, c+dc);
}

// check if all non-mine cells are revealed
static void check_win(void) {
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      if (board[r][c] != -1 && cstate[r][c] != REVEALED)
        return;
  won = game_over = 1;
}

// reset game state to start a new game
static void init_game(void) {
  rseed = uptime();
  game_over = won = 0;
  first_click = 1;
  flags_placed = 0;
  timer_seconds = 0;
  timer_running = 0;
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) {
      board[r][c]  = 0;
      cstate[r][c] = HIDDEN;
    }
}

// reveal a random safe hidden cell
static void do_hint(void) {
  if (game_over || first_click) return;
  int candidates[ROWS*COLS][2];
  int n = 0;
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      if (cstate[r][c] == HIDDEN && board[r][c] != -1) {
        candidates[n][0] = r;
        candidates[n][1] = c;
        n++;
      }
  if (n == 0) return;
  int pick = rnext() % n;
  reveal(candidates[pick][0], candidates[pick][1]);
  check_win();
}

// check if (x,y) is inside a button at vertical position by
static int in_btn(int x, int y, int by) {
  return x >= BTN_X && x < BTN_X + BTN_W &&
         y >= by    && y < by + BTN_H;
}

// handle a mouse event: update cursor position, check button clicks, reveal cells, etc.
static void handle_click(struct mouse_event ev) {
  cursor_x += ev.x;
  cursor_y -= ev.y;
  if (cursor_x < 0)       cursor_x = 0;
  if (cursor_x >= SCR_W)  cursor_x = SCR_W-1;
  if (cursor_y < 0)       cursor_y = 0;
  if (cursor_y >= SCR_H)  cursor_y = SCR_H-1;

  int left  = (ev.buttons & 0x1) != 0;
  int right = (ev.buttons & 0x2) != 0;
  int mid   = (ev.buttons & 0x4) != 0;

  // middle click to reset game 
  if (mid) { init_game(); prev_left = prev_right = 0; return; }

  int lclick = left  && !prev_left;
  int rclick = right && !prev_right;
  prev_left  = left;
  prev_right = right;

  if (!lclick && !rclick) return;

  int cx = cursor_x, cy = cursor_y;

  // Button panel (always active regardless of game_over)
  if (lclick) {
    if (in_btn(cx, cy, BTN_RST_Y)) { init_game(); return; }
    if (in_btn(cx, cy, BTN_HNT_Y)) { do_hint();   return; }
    if (in_btn(cx, cy, BTN_EXT_Y)) { should_exit = 1; return; }
  }

  if (game_over) return;

  // Grid
  int c = (cx - GRID_X) / CELL;
  int r = (cy - GRID_Y) / CELL;
  if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return;

  if (rclick) {
    if (cstate[r][c] == HIDDEN)       { cstate[r][c] = FLAGGED; flags_placed++; }
    else if (cstate[r][c] == FLAGGED) { cstate[r][c] = HIDDEN;  flags_placed--; }
    return;
  }

  if (cstate[r][c] == FLAGGED || cstate[r][c] == REVEALED) return;

  if (first_click) {
    place_mines(r, c);
    first_click = 0;
    timer_running = 1;
  }

  if (board[r][c] == -1) {
    cstate[r][c] = REVEALED;
    game_over = 1;
    timer_running = 0;
    for (int rr=0; rr<ROWS; rr++)
      for (int cc=0; cc<COLS; cc++)
        if (board[rr][cc]==-1 && cstate[rr][cc]!=FLAGGED)
          cstate[rr][cc] = REVEALED;
  } else {
    reveal(r, c);
    check_win();
    if (won) timer_running = 0;
  }
}

static int neutral_face[20][20] = {
  {4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,1},
  {4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {4,0,0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1},
  {4,0,0,0,1,1,0,0,0,0,0,0,0,0,1,1,0,0,0,1},
  {4,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1},
  {4,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,1},
  {4,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1},
  {4,0,1,0,0,0,0,0,1,0,0,1,0,0,0,0,0,1,0,1},
  {4,0,1,0,0,0,0,0,1,0,0,1,0,0,0,0,0,1,0,1},
  {4,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1},
  {4,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1},
  {4,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1},
  {4,0,1,0,0,0,1,1,1,1,1,1,1,1,0,0,0,1,0,1},
  {4,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1},
  {4,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,1},
  {4,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1},
  {4,0,0,0,1,1,0,0,0,0,0,0,0,0,1,1,0,0,0,1},
  {4,0,0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1},
  {4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

static int smiley_face[20][20] = {
  {4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,1},
  {4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {4,0,0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1},
  {4,0,0,0,1,1,3,3,3,3,3,3,3,3,1,1,0,0,0,1},
  {4,0,0,1,1,3,3,3,3,3,3,3,3,3,3,1,1,0,0,1},
  {4,0,1,1,3,3,3,3,3,3,3,3,3,3,3,3,1,1,0,1},
  {4,0,1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,1,0,1},
  {4,0,1,3,3,3,3,3,1,3,3,1,3,3,3,3,3,1,0,1},
  {4,0,1,3,3,3,3,3,1,3,3,1,3,3,3,3,3,1,0,1},
  {4,0,1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,1,0,1},
  {4,0,1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,1,0,1},
  {4,0,1,3,3,1,3,3,3,3,3,3,3,3,1,3,3,1,0,1},
  {4,0,1,3,3,3,1,1,1,1,1,1,1,1,3,3,3,1,0,1},
  {4,0,1,3,3,3,3,3,3,3,3,3,3,3,3,3,3,1,0,1},
  {4,0,1,1,3,3,3,3,3,3,3,3,3,3,3,3,1,1,0,1},
  {4,0,0,1,1,3,3,3,3,3,3,3,3,3,3,1,1,0,0,1},
  {4,0,0,0,1,1,3,3,3,3,3,3,3,3,1,1,0,0,0,1},
  {4,0,0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1},
  {4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

static int sad_face[20][20] = {
  {4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,1},
  {4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {4,0,0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1},
  {4,0,0,0,1,1,2,2,2,2,2,2,2,2,1,1,0,0,0,1},
  {4,0,0,1,1,2,2,2,2,2,2,2,2,2,2,1,1,0,0,1},
  {4,0,1,1,2,2,2,2,2,2,2,2,2,2,2,2,1,1,0,1},
  {4,0,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,0,1},
  {4,0,1,2,2,2,2,2,1,2,2,1,2,2,2,2,2,1,0,1},
  {4,0,1,2,2,2,2,2,1,2,2,1,2,2,2,2,2,1,0,1},
  {4,0,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,0,1},
  {4,0,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,0,1},
  {4,0,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,0,1},
  {4,0,1,2,2,2,1,1,1,1,1,1,1,1,2,2,2,1,0,1},
  {4,0,1,2,2,1,2,2,2,2,2,2,2,2,1,2,2,1,0,1},
  {4,0,1,1,2,2,2,2,2,2,2,2,2,2,2,2,1,1,0,1},
  {4,0,0,1,1,2,2,2,2,2,2,2,2,2,2,1,1,0,0,1},
  {4,0,0,0,1,1,2,2,2,2,2,2,2,2,1,1,0,0,0,1},
  {4,0,0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1},
  {4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

static int face_pallette[5] = {C_LGRAY, C_BLACK, C_RED, C_YELLOW, C_WHITE}; 

static void draw_face_helper(int face[20][20]) {
  int cx = BTN_X + BTN_W / 2;
  int cy = 200 - (200 - BTN_EXT_Y - 18) / 2;
  for (int r = 0; r < 20; r++)
    for (int c = 0; c < 20; c++)
      pset(cx+c-8, cy+r-8, face_pallette[face[r][c]]);

}

// Smiley (won) / frown (lost) / neutral face in the left panel
static void draw_face() {
// neutral, sad, smiley
  if (game_over & !won) draw_face_helper(sad_face);
  else if (game_over & won) draw_face_helper(smiley_face);
  else draw_face_helper(neutral_face);
}

static int arrow[12][8] = {
    {1,1,1,0,0,0,0,0},
    {1,2,1,1,0,0,0,0},
    {1,2,2,1,1,0,0,0},
    {1,2,2,2,1,1,0,0},
    {1,2,2,2,2,1,1,0},
    {1,2,2,2,2,2,1,1},
    {1,2,2,2,2,2,2,1},
    {1,2,2,2,2,1,1,1},
    {1,2,2,1,2,1,1,0},
    {1,2,1,1,1,2,1,1},
    {1,1,0,0,1,1,2,1},
    {0,0,0,0,0,1,1,1}
};

// arrow cursor
static void draw_cursor(int cx, int cy) {
  for (int r = 0; r < 12; r++)
    for (int c = 0; c < 8; c++) {
      int col = arrow[r][c];
      if (col)
        pset(cx+c, cy+r, col == 1 ? C_WHITE : C_BLACK);
    }
}

// Draw a button with 3D border and a centered glyph
static void draw_btn(int by, int bg, int fg, int glyph_idx) {
  fillrect(BTN_X, by, BTN_W, BTN_H, bg);
  fillrect(BTN_X,          by,          BTN_W, 1,    C_WHITE);
  fillrect(BTN_X,          by,          1,    BTN_H, C_WHITE);
  fillrect(BTN_X,          by+BTN_H-1,  BTN_W, 1,    C_BLACK);
  fillrect(BTN_X+BTN_W-1,  by,          1,    BTN_H, C_BLACK);
  // center the 6x10 glyph inside the button
  int gx = BTN_X + (BTN_W - 6) / 2;
  int gy = by    + (BTN_H - 10) / 2;
  draw_glyph(glyph_idx, gx, gy, fg);
}

static void render_board(void) {
  fillrect(0, 0, SCR_W, SCR_H, C_BLACK);

  // Left panel background
  fillrect(0, 0, GRID_X-4, SCR_H, C_DGRAY);

  // Mine counter: small black square + remaining count
  int remaining = MINES - flags_placed;
  if (remaining < 0) remaining = 0;
  fillrect(BTN_X, 13, 8, 8, C_BLACK);             // mine icon
  draw_number2(remaining, BTN_X+11, 12, C_WHITE);  // two-digit count

  // Timer: T glyph + three-digit seconds
  draw_glyph(13, BTN_X, 32, C_WHITE);              // T
  draw_number3(timer_seconds, BTN_X+9, 32, C_YELLOW);

  // Face (between timer and restart button)
  draw_face();

  // Buttons
  draw_btn(BTN_RST_Y, C_YELLOW, C_BLACK, 10); // R
  draw_btn(BTN_HNT_Y, C_CYAN,   C_BLACK, 11); // H
  draw_btn(BTN_EXT_Y, C_RED,    C_WHITE, 12); // X

  // Grid border (color reflects game state)
  int bx = GRID_X-2, by = GRID_Y-2;
  int bw = COLS*CELL+4, bh = ROWS*CELL+4;
  int bcol = won ? C_GREEN : (game_over ? C_RED : C_DGRAY);
  fillrect(bx, by, bw, bh, bcol);

  // Cells
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      draw_cell(r, c);

  // Arrow cursor
  draw_cursor(cursor_x, cursor_y);

  write(display_fd, screen, SCR_SIZE);
}

// timer thread
void timer_thread(void *arg) {
  uint last_tick = uptime();
  while (!should_exit) {
    if (uptime() - last_tick >= 100) { // update every 100 ticks (~1 second)
      last_tick = uptime();
      ulock_acquire(&game_lock);
      if (timer_running && !game_over) {
        timer_seconds++;
        dirty = 1;
      }
      ulock_release(&game_lock);
    }
  }
  exit();
}

// input thread
void input_thread(void *arg) {
  struct mouse_event ev;
  while (!should_exit) {
    mouseread(&ev);
    ulock_acquire(&game_lock);
    handle_click(ev);
    dirty = 1;
    ulock_release(&game_lock);
  }
  exit();
}

int main(void) {
  display_fd = open("display", O_RDWR);
  if (display_fd < 0) {
    printf(1, "minesweeper: cannot open display\n");
    exit();
  }

  ioctl(display_fd, 1, 0x13);  // switch to VGA mode 13h

  init_game();
  dirty = 1;

  thread_create(timer_thread, 0);
  thread_create(input_thread, 0);

  // main thread: render loop and exit handling
  while (!should_exit) {
    ulock_acquire(&game_lock);
    if (dirty) {
      render_board();
      dirty = 0;
    }
    ulock_release(&game_lock);
  }

  ioctl(display_fd, 1, 3);  // switch back to text mode
  thread_join(); // wait for other threads to finish (optional, since we're exiting)
  thread_join();
  exit();
}
