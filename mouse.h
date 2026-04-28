#pragma once

struct mouse_event {
  int x, y;
  int buttons;  // bit 0 = left, bit 1 = right
};

void mouseinit(void);
void mouseintr(void);
int mouseread(struct mouse_event*);
