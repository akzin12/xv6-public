#include <stdarg.h>

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "mouse.h"

#define MOUSE_BUF 128

struct {
  struct spinlock lock;
  struct mouse_event buf[MOUSE_BUF];
  int head;
  int tail;
} mousedev;

void mouseinit(void) {
  initlock(&mousedev.lock, "mouse");

  // Helper: wait until PS/2 controller input buffer is empty
  // (must do this before every command or controller ignores it)
  void ps2wait_write(void);

  // Wait for input buffer empty (bit 1 of status = 0)
  while (inb(0x64) & 0x2);
  outb(0x64, 0xA8);          // enable auxiliary mouse port

  while (inb(0x64) & 0x2);
  outb(0x64, 0x20);          // read current controller config byte
  while (!(inb(0x64) & 0x1));
  uchar config = inb(0x60);
  config |= 0x02;            // enable IRQ12 (bit 1)
  config &= ~0x20;           // enable mouse clock (clear bit 5)

  while (inb(0x64) & 0x2);
  outb(0x64, 0x60);          // write config byte back
  while (inb(0x64) & 0x2);
  outb(0x60, config);

  // Send enable data reporting command to mouse itself
  while (inb(0x64) & 0x2);
  outb(0x64, 0xD4);          // next byte goes to mouse
  while (inb(0x64) & 0x2);
  outb(0x60, 0xF4);          // mouse command: enable data reporting
  while (!(inb(0x64) & 0x1));
  uchar ack = inb(0x60);     // should be 0xFA

  ioapicenable(IRQ_MOUSE, 0);
}

void mouseintr(void) {
  static uchar packet[3]; // mouse packets: 3 bytes
  static int byte_count = 0;
  struct mouse_event event;

  uchar status = inb(0x64);
  if (!(status & 0x1) || !(status & 0x20)) // no data or not from mouse
    return;

  packet[byte_count] = inb(0x60);

  if (byte_count == 0 && !(packet[0] & 0x08))
    return;

  byte_count++;

  if (byte_count < 3)
    return;

  byte_count = 0;

  if (packet[0] & 0xC0) // x y overflow, ignore packet
    return;

  event.buttons = packet[0] & 0x07; // bits 0-2: left, right, middle buttons
  event.x =  (char)packet[1];    // (char) cast handles sign extension automatically
  event.y =  (char)packet[2];    // negate inline, no separate line needed

  acquire(&mousedev.lock);
  mousedev.buf[mousedev.tail] = event;
  mousedev.tail = (mousedev.tail + 1) % MOUSE_BUF;
  if (mousedev.tail == mousedev.head)
    mousedev.head = (mousedev.head + 1) % MOUSE_BUF;
  wakeup(&mousedev);
  release(&mousedev.lock);
}

int mouseread(struct mouse_event *ev) {
  acquire(&mousedev.lock);
  while (mousedev.head == mousedev.tail) { // buffer empty
    sleep(&mousedev, &mousedev.lock);
  }
  *ev = mousedev.buf[mousedev.head];
  mousedev.head = (mousedev.head + 1) % MOUSE_BUF; //circular buffer
  release(&mousedev.lock);
  return 0;
}