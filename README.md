# Minesweeper on xv6

**CS461 - Operating Systems | Spring 2026 | University of Illinois Chicago**

A fully playable Minesweeper game built from scratch on top of xv6-64, featuring real-time VGA graphics, mouse input, kernel-level threads, and synchronization primitives implemented inside the OS.

---

## Demo

![Minesweeper Demo](https://github.com/user-attachments/assets/1cdf239d-d6e9-4ade-9e1d-58ebdbbb735c)

---

## Features

- **VGA 320×200 graphics** — direct framebuffer rendering with the 256-color default palette
- **Mouse support** — custom PS/2 mouse driver; move, left-click to reveal, right-click to flag
- **Multithreaded** — dedicated timer thread and input thread running concurrently with the main render loop
- **User-level locks (`ulock`)** — spinlock-based mutual exclusion to protect shared game state
- **12×15 grid, 36 mines** — ~25% mine density with safe-first-click guarantee
- **Hint system** — reveals a safe cell when you're stuck
- **Live timer** — elapsed seconds displayed in the HUD, driven by the timer thread
- **Face status indicator** — updates on win/loss/in-progress
- **Restart & Exit buttons** — rendered in the left panel, fully clickable

---

## Implementation Overview

| Component | File(s) | Description |
|-----------|---------|-------------|
| Game logic & renderer | `minesweeper.c` | Board state, VGA drawing, event handling |
| VGA display driver | `display.c`, `vga.h` | Mode-switching, framebuffer write syscall |
| Mouse driver | `mouse.c`, `mouse.h` | PS/2 packet parsing, `mouseread` syscall |
| Threading library | `thread.c` | `thread_create` / `thread_join` via `clone` syscall |
| User-level locks | `ulock` (in `user.h`) | Spinlock primitives for thread synchronization |

### Threading Model

The game spawns two threads on startup:

```
main()
 ├── thread_create(timer_thread)   — increments timer every second
 └── thread_create(input_thread)   — polls mouse events, updates cursor & clicks
```

All shared state (`board`, `cstate`, `timer_seconds`, `game_over`, etc.) is protected by a `ulock` acquired before reads/writes and released immediately after.

### VGA Rendering

The display driver switches the VGA adapter from text mode (80×25) into Mode 13h (320×200, 1 byte/pixel) on open and restores text mode on close. The game writes a 64 KB framebuffer via a single `write(display_fd, screen, SCR_SIZE)` call each frame, keeping rendering atomic from the driver's perspective.

---

## Building and Running

### Prerequisites

- `x86_64-elf` cross-compiler (GCC)
- QEMU with x86 support (`qemu-system-i386` or `qemu-system-x86_64`)

### Build

```bash
make
```

### Run in QEMU

```bash
make qemu
```

Once xv6 boots to the shell, launch the game:

```
$ minesweeper
```

### Controls

| Action | Input |
|--------|-------|
| Move cursor | Mouse |
| Reveal cell | Left-click |
| Toggle flag | Right-click |
| Hint | Click **H** button |
| Restart | Click **R** button |
| Exit | Click **X** button |

---

## Project Structure (new files)

```
minesweeper.c   — game application
display.c       — kernel VGA display driver
mouse.c         — kernel PS/2 mouse driver
mouse.h         — mouse_event struct & syscall declarations
thread.c        — user-level threading library
```

---

## Authors

- Built for **CS461 Operating Systems, Spring 2026** at **UIC**

---

## Base System

xv6-64 is a 64-bit port of MIT's xv6 by Anthony Shelton and Jakob Eriksson, used in UIC's OS curriculum.  
Original xv6: Copyright 2006–2017 Frans Kaashoek, Robert Morris, Russ Cox, Anthony Shelton, and Jakob Eriksson.
