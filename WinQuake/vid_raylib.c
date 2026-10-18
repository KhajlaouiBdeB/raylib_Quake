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
// vid_raylib.c -- software renderer frame buffer shown in a raylib window

// The renderer draws 8-bit palette indices into vid.buffer as it always did.
// VID_Update turns them into RGBA through a 256-entry table and the bridge
// uploads that to a texture and draws it scaled to the window.

#include "quakedef.h"
#include "d_local.h"
#include "bridge_raylib.h"

#define	DEFAULT_WIDTH	640
#define	DEFAULT_HEIGHT	480

static byte		*vid_buffer;
static short	*zbuffer;
static byte		*surfcache;
static unsigned	*rgbabuffer;

// palette index to RGBA8 (R in the low byte, as raylib reads it)
static unsigned	rgbatable[256];

unsigned short	d_8to16table[256];
unsigned	d_8to24table[256];

cvar_t	vid_fullscreen = {"vid_fullscreen", "0"};	// not saved: -fullscreen or Alt+Enter
cvar_t	vid_vsync = {"vid_vsync", "1", true};
cvar_t	vid_integerscale = {"vid_integerscale", "0", true};
cvar_t	vid_filter = {"vid_filter", "0", true};		// 1 = linear

void	VID_SetPalette (unsigned char *palette)
{
	int		i;
	unsigned	r, g, b;
#ifdef __EMSCRIPTEN__
	// The browser build is shown brighter (QUAK-40): at gamma 1 the raw
	// palette reads very dark on today's sRGB screens, about half as bright
	// as GL ports such as netquake.io. This is a VID_DISPLAY_GAMMA (0.7)
	// power curve, like "gamma 0.7", put on top of the gamma cvar instead
	// of replacing it, because default.cfg sets "gamma 1.0" and every
	// config.cfg saves it. The Brightness slider is rescaled to match (see
	// view.h); saved values still apply. Black and white stay put. The
	// desktop build keeps id's look.
	static byte	bright[256];
	static qboolean	bright_built;

	if (!bright_built)
	{
		for (i = 0; i < 256; i++)
			bright[i] = (int)(255 * pow (i/255.0, VID_DISPLAY_GAMMA) + 0.5);
		bright_built = true;
	}
#endif

	for (i = 0; i < 256; i++)
	{
		r = palette[i*3];
		g = palette[i*3+1];
		b = palette[i*3+2];
#ifdef __EMSCRIPTEN__
		r = bright[r];
		g = bright[g];
		b = bright[b];
#endif
		rgbatable[i] = r | (g << 8) | (b << 16) | (0xffu << 24);
		d_8to24table[i] = r | (g << 8) | (b << 16);
	}
}

// Damage, pickup and underwater flashes: view.c has already blended the
// shift into the palette it passes, so this is just a new table.
void	VID_ShiftPalette (unsigned char *palette)
{
	VID_SetPalette (palette);
}

static int VID_ParmValue (char *parm, int defvalue)
{
	int		i;

	i = COM_CheckParm (parm);
	if (i && i < com_argc - 1)
		return Q_atoi (com_argv[i+1]);
	return defvalue;
}

void	VID_Init (unsigned char *palette)
{
	int		width, height;
	int		winwidth, winheight;
	int		scale;
	int		cachesize;
	int		fullscreen;

	Cvar_RegisterVariable (&vid_fullscreen);
	Cvar_RegisterVariable (&vid_vsync);
	Cvar_RegisterVariable (&vid_integerscale);
	Cvar_RegisterVariable (&vid_filter);

	// -width/-height set the size the renderer draws at; the window can
	// then be resized freely and the frame is scaled to fit it.
	width = VID_ParmValue ("-width", DEFAULT_WIDTH);
	height = VID_ParmValue ("-height", DEFAULT_HEIGHT);
	if (width < 320)
		width = 320;
	if (height < 200)
		height = 200;
	if (width > MAXWIDTH)
		width = MAXWIDTH;
	if (height > MAXHEIGHT)
		height = MAXHEIGHT;
	width &= ~7;	// the span drawers like multiples of 8

	// -scale sets the starting window size in multiples of the frame size
	scale = VID_ParmValue ("-scale", width <= 400 ? 2 : 1);
	if (scale < 1)
		scale = 1;
	winwidth = VID_ParmValue ("-winwidth", width * scale);
	winheight = VID_ParmValue ("-winheight", height * scale);

	fullscreen = COM_CheckParm ("-fullscreen") != 0;
	if (COM_CheckParm ("-window"))
		fullscreen = 0;

	vid.maxwarpwidth = WARP_WIDTH;
	vid.maxwarpheight = WARP_HEIGHT;
	vid.width = vid.conwidth = width;
	vid.height = vid.conheight = height;
	// Square pixels on the screen; the renderer's aspect is relative to
	// 320x240.
	vid.aspect = ((float)vid.height / (float)vid.width) * (320.0 / 240.0);
	vid.numpages = 1;
	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));

	vid_buffer = malloc (width * height);
	zbuffer = malloc (width * height * sizeof(*zbuffer));
	cachesize = D_SurfaceCacheForRes (width, height);
	surfcache = malloc (cachesize);
	rgbabuffer = malloc (width * height * sizeof(*rgbabuffer));
	if (!vid_buffer || !zbuffer || !surfcache || !rgbabuffer)
		Sys_Error ("Not enough memory for a %dx%d frame buffer", width, height);
	memset (vid_buffer, 0, width * height);

	vid.buffer = vid.conbuffer = vid_buffer;
	vid.rowbytes = vid.conrowbytes = width;
	vid.direct = NULL;

	d_pzbuffer = zbuffer;
	D_InitCaches (surfcache, cachesize);

	VID_SetPalette (palette);

	if (!RL_OpenWindow ("Quake", width, height, winwidth, winheight,
		fullscreen, (int)vid_vsync.value))
		Sys_Error ("Couldn't open a window (is there a display?)");

	// the command line wins over config.cfg, which is read later
	if (fullscreen)
		Cvar_SetValue ("vid_fullscreen", 1);

	vid.recalc_refdef = 1;
	Con_SafePrintf ("Video mode %dx%d, window %dx%d\n", width, height,
		winwidth, winheight);
}

void	VID_Shutdown (void)
{
	RL_CloseWindow ();
}

// Keeps the window in step with the cvars, which config.cfg, the console
// and Alt+Enter (in the bridge) can all change.
static void VID_CheckCvars (void)
{
	static int	oldfullscreen = -1;
	int			fullscreen;

	fullscreen = RL_IsFullscreen ();
	if (fullscreen != oldfullscreen)
	{
		// changed by Alt+Enter
		if (oldfullscreen != -1 && fullscreen != (int)vid_fullscreen.value)
			Cvar_SetValue ("vid_fullscreen", fullscreen);
		oldfullscreen = fullscreen;
	}
	if ((int)vid_fullscreen.value != fullscreen)
	{
		RL_SetFullscreen ((int)vid_fullscreen.value);
		oldfullscreen = RL_IsFullscreen ();
	}

	// A timedemo measures the renderer, so it is never held to the display.
	RL_SetVsync (vid_vsync.value && !cls.timedemo);
}

void	VID_Update (vrect_t *rects)
{
	byte		*src;
	unsigned	*dst;
	int			n;

	VID_CheckCvars ();

	// The whole frame goes up every time: the buffer keeps what was drawn
	// before, so the dirty rectangles add nothing on a GPU.
	src = vid.buffer;
	dst = rgbabuffer;
	for (n = vid.width * vid.height; n > 0; n--)
		*dst++ = rgbatable[*src++];

	RL_Present (rgbabuffer, (int)vid_integerscale.value,
		(int)vid_filter.value);
}

/*
================
D_BeginDirectRect
================
*/
void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height)
{
// the "accessing disk" icon is drawn straight to the screen on DOS; the
// next frame shows the screen here, so it isn't drawn
}


/*
================
D_EndDirectRect
================
*/
void D_EndDirectRect (int x, int y, int width, int height)
{
}
