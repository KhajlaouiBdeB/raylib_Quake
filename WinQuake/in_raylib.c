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
// in_raylib.c -- keyboard and mouse through the raylib bridge

// The bridge reports keys as ASCII or RLK_* codes; this file turns them into
// Quake's K_* key numbers. Key_Event does the rest, as with the old X11
// driver: the console gets its characters (shifted through keys.c's own
// table) and its auto-repeat from the key events.

#include "quakedef.h"
#include "bridge_raylib.h"

cvar_t	_windowed_mouse = {"_windowed_mouse", "1", true};
cvar_t	m_filter = {"m_filter", "0", true};

static qboolean	mouse_avail;
static float	mouse_x, mouse_y;
static float	old_mouse_x, old_mouse_y;

static const int	specialkeys[RLK_NUMCODES - RLK_FIRST_SPECIAL] =
{
	[RLK_UP - RLK_FIRST_SPECIAL] = K_UPARROW,
	[RLK_DOWN - RLK_FIRST_SPECIAL] = K_DOWNARROW,
	[RLK_LEFT - RLK_FIRST_SPECIAL] = K_LEFTARROW,
	[RLK_RIGHT - RLK_FIRST_SPECIAL] = K_RIGHTARROW,
	[RLK_ALT - RLK_FIRST_SPECIAL] = K_ALT,
	[RLK_CTRL - RLK_FIRST_SPECIAL] = K_CTRL,
	[RLK_SHIFT - RLK_FIRST_SPECIAL] = K_SHIFT,
	[RLK_F1 - RLK_FIRST_SPECIAL] = K_F1,
	[RLK_F2 - RLK_FIRST_SPECIAL] = K_F2,
	[RLK_F3 - RLK_FIRST_SPECIAL] = K_F3,
	[RLK_F4 - RLK_FIRST_SPECIAL] = K_F4,
	[RLK_F5 - RLK_FIRST_SPECIAL] = K_F5,
	[RLK_F6 - RLK_FIRST_SPECIAL] = K_F6,
	[RLK_F7 - RLK_FIRST_SPECIAL] = K_F7,
	[RLK_F8 - RLK_FIRST_SPECIAL] = K_F8,
	[RLK_F9 - RLK_FIRST_SPECIAL] = K_F9,
	[RLK_F10 - RLK_FIRST_SPECIAL] = K_F10,
	[RLK_F11 - RLK_FIRST_SPECIAL] = K_F11,
	[RLK_F12 - RLK_FIRST_SPECIAL] = K_F12,
	[RLK_INS - RLK_FIRST_SPECIAL] = K_INS,
	[RLK_DEL - RLK_FIRST_SPECIAL] = K_DEL,
	[RLK_PGDN - RLK_FIRST_SPECIAL] = K_PGDN,
	[RLK_PGUP - RLK_FIRST_SPECIAL] = K_PGUP,
	[RLK_HOME - RLK_FIRST_SPECIAL] = K_HOME,
	[RLK_END - RLK_FIRST_SPECIAL] = K_END,
	[RLK_PAUSE - RLK_FIRST_SPECIAL] = K_PAUSE,
	[RLK_MOUSE1 - RLK_FIRST_SPECIAL] = K_MOUSE1,
	[RLK_MOUSE2 - RLK_FIRST_SPECIAL] = K_MOUSE2,
	[RLK_MOUSE3 - RLK_FIRST_SPECIAL] = K_MOUSE3,
	[RLK_MWHEELUP - RLK_FIRST_SPECIAL] = K_MWHEELUP,
	[RLK_MWHEELDOWN - RLK_FIRST_SPECIAL] = K_MWHEELDOWN,
};

// The Quake key number for a bridge code, 0 if there is none.
static int IN_TranslateKey (int code)
{
	// Tab, Enter, Escape, Space, Backspace and the printable characters
	// have the same numbers on both sides.
	if (code > 0 && code < RLK_FIRST_SPECIAL)
		return code;
	if (code < RLK_NUMCODES)
		return specialkeys[code - RLK_FIRST_SPECIAL];
	return 0;
}

/*
================
Sys_SendKeyEvents

Called at the start of every host frame.
================
*/
void Sys_SendKeyEvents (void)
{
	rl_keyevent_t	ev;
	int				key;

	if (!RL_WindowOpen ())
		return;

	// The pointer belongs to the player only while playing; the console,
	// the menus and the demo loop leave it free.
	RL_SetMouseGrab (mouse_avail && _windowed_mouse.value
		&& key_dest == key_game && cls.state == ca_connected
		&& !cls.demoplayback);

	RL_PumpEvents ();

	while (RL_GetKeyEvent (&ev))
	{
		key = IN_TranslateKey (ev.key);
#ifdef IN_RAYLIB_KEYLOG
		// -DIN_RAYLIB_KEYLOG: log every key event, to check the
		// downs and ups are balanced
		Sys_Printf ("keylog: key %d %s\n", key, ev.down ? "down" : "up");
#endif
		if (key)
			Key_Event (key, ev.down);
	}

	// The window's close button quits without the "are you sure" menu.
	if (RL_QuitRequested ())
	{
		CL_Disconnect ();
		Host_ShutdownServer (false);
		Sys_Quit ();
	}
}

void IN_Init (void)
{
	Cvar_RegisterVariable (&_windowed_mouse);
	Cvar_RegisterVariable (&m_filter);

	mouse_avail = !COM_CheckParm ("-nomouse");
	mouse_x = mouse_y = 0;
	old_mouse_x = old_mouse_y = 0;
}

void IN_Shutdown (void)
{
	mouse_avail = false;
	RL_SetMouseGrab (0);
}

// Mouse buttons and the wheel arrive as key events in Sys_SendKeyEvents.
void IN_Commands (void)
{
}

void IN_Move (usercmd_t *cmd)
{
	float	dx, dy;

	if (!mouse_avail)
		return;

	// Pixels of relative (raw, unaccelerated) motion since the last call,
	// the same units the X11 driver used.
	RL_GetMouseMotion (&dx, &dy);
#ifdef IN_RAYLIB_KEYLOG
	if (dx || dy)
		Sys_Printf ("keylog: mouse %g %g\n", dx, dy);
#endif
	mouse_x = dx;
	mouse_y = dy;

	if (m_filter.value)
	{
		mouse_x = (mouse_x + old_mouse_x) * 0.5;
		mouse_y = (mouse_y + old_mouse_y) * 0.5;
	}
	old_mouse_x = dx;
	old_mouse_y = dy;

	mouse_x *= sensitivity.value;
	mouse_y *= sensitivity.value;

	if ( (in_strafe.state & 1) || (lookstrafe.value && (in_mlook.state & 1) ))
		cmd->sidemove += m_side.value * mouse_x;
	else
		cl.viewangles[YAW] -= m_yaw.value * mouse_x;

	if (in_mlook.state & 1)
		V_StopPitchDrift ();

	if ( (in_mlook.state & 1) && !(in_strafe.state & 1))
	{
		cl.viewangles[PITCH] += m_pitch.value * mouse_y;
		if (cl.viewangles[PITCH] > 80)
			cl.viewangles[PITCH] = 80;
		if (cl.viewangles[PITCH] < -70)
			cl.viewangles[PITCH] = -70;
	}
	else
	{
		if ((in_strafe.state & 1) && noclip_anglehack)
			cmd->upmove -= m_forward.value * mouse_y;
		else
			cmd->forwardmove -= m_forward.value * mouse_y;
	}
}
