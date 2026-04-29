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
#include "vga.h"

#define CRTPORT 0x3d4

static char * endofbuf = (char *)P2V(0xA0000); // start of VGA buffer
static char savedbuf[80*25*2]; // buffer to save text mode content 
static int pos = 0; // cursor position in text mode
static char * textbuffer = (char *)P2V(0xB8000); // start of text mode buffer

// save text buffer before mode switching
static void savebuf() {
  for(int i=0; i<80*25*2; i++) {
    savedbuf[i] = textbuffer[i]; 
  }
  // save cursor position before mode switching
  outb(CRTPORT, 14);
  pos = inb(CRTPORT+1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT+1); 
}

// restore text buffer after mode switching
static void restorebuf() {
  for(int i=0; i<80*25*2; i++) {
    textbuffer[i] = savedbuf[i]; 
  }
  // restore cursor position after mode switching
  outb(CRTPORT, 14);
  outb(CRTPORT+1, pos>>8);
  outb(CRTPORT, 15);
  outb(CRTPORT+1, pos);
}

// initialize display in the devsw table
void displayinit(void)
{
  devsw[DISPLAY].ioctl = displayioctl;
  devsw[DISPLAY].write = displaywrite;
}

// handle ioctl requests for display device
int displayioctl(struct file *f, int param, int value)
{  
  if (param == 1) { // set VGA palette color
    if (value == 3) {
        vgaMode3();
        restorebuf(); // restore the saved screen content
    }
    else if (value == 0x13) {
        savebuf(); // save the screen content 
        vgaMode13();
    } 
    return 0;
  } else if (param == 2) { // set VGA palette color
    int palette_index = (value >> 24) & 0x3f;
    int r = (value >> 16) & 0x3f;
    int g = (value >> 8) & 0x3f;
    int b = value & 0x3f;
    vgaSetPalette(palette_index, r, g, b);
    return 0;
  }
  return -1; // return -1 for unknown ioctl mode change requests
}

// write to VGA buffer for display device
int displaywrite(struct inode *ip, uint off, char *buf, int n)
{
  for(int i=0; i<n; i++) {
    if (endofbuf >= (char *)P2V(0xA0000) + 64000)
      endofbuf = (char *)P2V(0xA0000);
    *endofbuf++ = buf[i];
  }
  return n; // return num bytes written
}