#include "types.h"
#include "user.h"
#include "mouse.h"

#define VGA_BASE    0xB8000
#define VGA_COLS    80
#define VGA_ROWS    25
#define VGA_COLOR   0x0F      // white on black
#define CURSOR_CHAR '@'

// Clamp helper
int clamp(int val, int min, int max) {
  if (val < min) return min;
  if (val > max) return max;
  return val;
}

// Write a character directly to VGA memory via /dev/mem or MMIO
// In xv6 userspace we use the screen syscall instead
void clear_screen(void) {
  int i;
  for (i = 0; i < VGA_ROWS; i++) {
    int j;
    for (j = 0; j < VGA_COLS; j++) {
      printf(1, " ");
    }
    printf(1, "\n");
  }
}

void move_cursor(int old_x, int old_y, int new_x, int new_y) {
  // Move to old position and erase
  printf(1, "\033[%d;%dH ", old_y + 1, old_x + 1);
  // Move to new position and draw cursor
  printf(1, "\033[%d;%dH@", new_y + 1, new_x + 1);
}

void draw_border(void) {
  int i;
  // Top border
  printf(1, "\033[1;1H+");
  for (i = 1; i < VGA_COLS - 1; i++) printf(1, "-");
  printf(1, "+");

  // Bottom border
  printf(1, "\033[%d;1H+", VGA_ROWS - 1);
  for (i = 1; i < VGA_COLS - 1; i++) printf(1, "-");
  printf(1, "+");

  // Side borders
  for (i = 2; i < VGA_ROWS - 1; i++) {
    printf(1, "\033[%d;1H|", i);
    printf(1, "\033[%d;%dH|", i, VGA_COLS);
  }
}

void draw_status(int x, int y, int buttons) {
  printf(1, "\033[%d;1H", VGA_ROWS);   // move to last row
  printf(1, "pos:(%2d,%2d) L:%d R:%d M:%d  (press middle click to exit)",
         x, y,
         (buttons & 0x1) ? 1 : 0,
         (buttons & 0x2) ? 1 : 0,
         (buttons & 0x4) ? 1 : 0);
}

int main(void) {
  struct mouse_event ev;
  int cur_x = VGA_COLS / 2;
  int cur_y = VGA_ROWS / 2;
  int old_x = cur_x;
  int old_y = cur_y;

  // Hide terminal cursor and clear screen
  printf(1, "\033[2J");        // clear screen
  printf(1, "\033[?25l");      // hide terminal cursor

  draw_border();
  draw_status(cur_x, cur_y, 0);

  // Draw initial cursor
  printf(1, "\033[%d;%dH@", cur_y + 1, cur_x + 1);

  while (1) {
    if (mouseread(&ev) < 0)
      break;

    // Middle click exits
    if (ev.buttons & 0x4)
      break;

    old_x = cur_x;
    old_y = cur_y;

    // Scale down movement so cursor doesn't fly across screen
    cur_x = clamp(cur_x + ev.x / 2, 1, VGA_COLS - 2);
    cur_y = clamp(cur_y + ev.y / 2, 1, VGA_ROWS - 3);

    // Only redraw if position changed
    if (cur_x != old_x || cur_y != old_y || ev.buttons) {
      move_cursor(old_x, old_y, cur_x, cur_y);
      draw_status(cur_x, cur_y, ev.buttons);
    }
  }

  // Cleanup — restore screen
  printf(1, "\033[2J");
  printf(1, "\033[1;1H");
  printf(1, "\033[?25h");      // restore terminal cursor
  printf(1, "Mouse test complete.\n");
  exit();
}