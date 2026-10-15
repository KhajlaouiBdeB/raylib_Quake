/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// bridge_raylib.h -- plain C interface to the raylib window, input and clock

// raylib.h and the Quake headers can't be included in the same file: raylib
// has its own bool (stdbool.h breaks qboolean), KEY_*, Vector3 and Color.
// bridge_raylib.c is the only file that includes raylib.h, and it includes no
// Quake header. The engine side (vid_raylib.c, in_raylib.c, sys_raylib.c)
// only sees this header, which uses plain C types.

#ifndef BRIDGE_RAYLIB_H
#define BRIDGE_RAYLIB_H

//
// window and video
//

// Opens a resizable window for a width x height RGBA framebuffer.
// Returns 0 if there is no window (no display, no OpenGL).
int RL_OpenWindow (const char *title, int width, int height,
	int winwidth, int winheight, int fullscreen, int vsync);
void RL_CloseWindow (void);
int RL_WindowOpen (void);

// Uploads a width x height RGBA8 frame and draws it, scaled to the window
// with the aspect ratio kept. integer: scale by whole multiples only.
// linear: bilinear filtering instead of nearest.
void RL_Present (const unsigned *rgba, int integer, int linear);

int RL_IsFullscreen (void);
void RL_SetFullscreen (int fullscreen);
void RL_SetVsync (int vsync);

// True once the user asked to close the window (close button, WM).
int RL_QuitRequested (void);

//
// input
//

// Key codes reported by the bridge. Letters, digits, space and punctuation
// are their lowercase ASCII characters; everything else is one of these.
// Left and right modifiers, and the keypad keys with a main keyboard
// equivalent, are merged into one code.
enum
{
	RLK_TAB = 9,
	RLK_ENTER = 13,
	RLK_ESCAPE = 27,
	RLK_SPACE = 32,
	RLK_BACKSPACE = 127,

	RLK_FIRST_SPECIAL = 256,
	RLK_UP = RLK_FIRST_SPECIAL,
	RLK_DOWN,
	RLK_LEFT,
	RLK_RIGHT,
	RLK_ALT,
	RLK_CTRL,
	RLK_SHIFT,
	RLK_F1, RLK_F2, RLK_F3, RLK_F4, RLK_F5, RLK_F6,
	RLK_F7, RLK_F8, RLK_F9, RLK_F10, RLK_F11, RLK_F12,
	RLK_INS,
	RLK_DEL,
	RLK_PGDN,
	RLK_PGUP,
	RLK_HOME,
	RLK_END,
	RLK_PAUSE,
	RLK_MOUSE1,
	RLK_MOUSE2,
	RLK_MOUSE3,
	RLK_MWHEELUP,
	RLK_MWHEELDOWN,

	RLK_NUMCODES
};

typedef struct
{
	int		key;	// RLK_* or ASCII
	int		down;	// 1 press (or auto-repeat), 0 release
} rl_keyevent_t;

// Polls the OS and queues key events. Keys pressed and released between
// two polls still give a down and an up. Alt+Enter toggles fullscreen and
// is not reported (nor is the release of its Enter).
void RL_PumpEvents (void);

// Returns 1 and fills ev while queued events remain.
int RL_GetKeyEvent (rl_keyevent_t *ev);

// Asks for the pointer to be captured (hidden, relative motion). The bridge
// only captures it while the window has focus, and captures it again when
// focus comes back.
void RL_SetMouseGrab (int grab);

// Mouse motion in pixels while captured, summed since the last call.
void RL_GetMouseMotion (float *dx, float *dy);

//
// touch controls (browser build)
//

// The page (web/shell.html) draws the controls and tracks the fingers; it
// calls RL_WebTouch with the buttons held, the stick and the look drag.
// These are its button bits. The menu keys come out of RL_GetKeyEvent as
// key events; the play buttons are read with RL_GetTouchButtons, so they
// work whatever the key bindings are.
enum
{
	RLT_FIRE = 0x1,
	RLT_JUMP = 0x2,
	RLT_NEXTWEAPON = 0x4,

	RLT_ESCAPE = 0x100,
	RLT_ENTER = 0x200,
	RLT_UP = 0x400,
	RLT_DOWN = 0x800,
	RLT_LEFT = 0x1000,
	RLT_RIGHT = 0x2000,
	RLT_YES = 0x4000,
	RLT_NO = 0x8000,
	RLT_CONSOLE = 0x10000
};

// Which controls the page shows: the menu keys, the play controls, or the
// menu keys and the phone's keyboard for the console.
enum
{
	RL_TOUCH_MENU,
	RL_TOUCH_PLAY,
	RL_TOUCH_CONSOLE
};

// True when the page has touch controls on. Always 0 on the desktop.
int RL_TouchActive (void);

// Tells the page which layout to show (sent only when it changes).
void RL_SetTouchMode (int mode);

// The play buttons held now, and those that went down since the last
// call (a tap shorter than a frame is still a press).
int RL_GetTouchButtons (int *pressed);

// The stick, -1..1 on each axis (y is down), and the look drag in CSS
// pixels since the last call.
void RL_GetTouchMove (float *movex, float *movey, float *lookx, float *looky);

#endif
