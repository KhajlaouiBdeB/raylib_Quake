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
//
// In the browser the frame follows the canvas: the page sizes the canvas
// with CSS, and the frame is the canvas's size in device pixels, so it is
// shown 1:1. vid_maxpixels caps the pixels drawn; past it the frame is a
// whole fraction of the canvas (shown at a whole multiple) or, when that
// would lose too much, the largest frame of the canvas's shape that fits.

#include "quakedef.h"
#include "d_local.h"
#include "bridge_raylib.h"

#define	DEFAULT_WIDTH	640
#define	DEFAULT_HEIGHT	480
#define	MIN_WIDTH		320
#define	MIN_HEIGHT		200

static byte		*vid_buffer;
static short	*zbuffer;
static byte		*surfcache;
static unsigned	*rgbabuffer;

static qboolean	vid_follow;		// the frame follows the drawable (browser)
static int		vid_wholescale;	// the frame fills it at a whole multiple
static int		vid_pendwidth, vid_pendheight;
static double	vid_pendtime;

// palette index to RGBA8 (R in the low byte, as raylib reads it)
static unsigned	rgbatable[256];

unsigned short	d_8to16table[256];
unsigned	d_8to24table[256];

cvar_t	vid_fullscreen = {"vid_fullscreen", "0"};	// not saved: -fullscreen or Alt+Enter
cvar_t	vid_vsync = {"vid_vsync", "1", true};
cvar_t	vid_integerscale = {"vid_integerscale", "0", true};
cvar_t	vid_filter = {"vid_filter", "0", true};		// 1 = linear
// browser build: most pixels drawn per frame (1920x1080)
cvar_t	vid_maxpixels = {"vid_maxpixels", "2073600"};

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

// (Re)allocates the frame, z and surface cache buffers for a new size.
static void VID_SetSize (int width, int height)
{
	int		cachesize;

	// The surface cache holds pointers to the surfaces using it.
	if (surfcache)
		D_FlushCaches ();
	free (vid_buffer);
	free (zbuffer);
	free (surfcache);
	free (rgbabuffer);

	vid_buffer = malloc (width * height);
	zbuffer = malloc (width * height * sizeof(*zbuffer));
	cachesize = D_SurfaceCacheForRes (width, height);
	surfcache = malloc (cachesize);
	rgbabuffer = malloc (width * height * sizeof(*rgbabuffer));
	if (!vid_buffer || !zbuffer || !surfcache || !rgbabuffer)
		Sys_Error ("Not enough memory for a %dx%d frame buffer", width, height);
	memset (vid_buffer, 0, width * height);

	vid.width = vid.conwidth = width;
	vid.height = vid.conheight = height;
	// The frame is shown with square pixels at any size (RL_Present keeps
	// its aspect), so don't stretch the projection for a 4:3 monitor.
	// screen.c widens fov for wide frames (fov_adapt).
	vid.aspect = 1.0;
	vid.buffer = vid.conbuffer = vid_buffer;
	vid.rowbytes = vid.conrowbytes = width;

	d_pzbuffer = zbuffer;
	D_InitCaches (surfcache, cachesize);

	vid.recalc_refdef = 1;
	scr_fullupdate = 0;		// clear around the view, redraw the status bar
}

// The frame size for a drawable of dw x dh pixels: the drawable itself if
// it fits vid_maxpixels and MAXWIDTH x MAXHEIGHT, else a whole fraction of
// it, or the largest frame of its shape that fits when the whole fraction
// would keep less than 3/4 of that.
static void VID_PickSize (int dw, int dh, int *width, int *height,
	int *wholescale)
{
	double	budget, fit;
	int		k, w, h;

	budget = vid_maxpixels.value;
	if (budget < MIN_WIDTH * MIN_HEIGHT)
		budget = MIN_WIDTH * MIN_HEIGHT;

	for (k = 1 ; ; k++)
	{
		w = dw / k;
		h = dh / k;
		if ((double)w * h <= budget && w <= MAXWIDTH && h <= MAXHEIGHT)
			break;
	}
	*wholescale = 1;
	if (k > 1)
	{
		fit = sqrt (budget / ((double)dw * dh));
		if (fit > (double)MAXWIDTH / dw)
			fit = (double)MAXWIDTH / dw;
		if (fit > (double)MAXHEIGHT / dh)
			fit = (double)MAXHEIGHT / dh;
		if ((double)w * h * 4 < (double)dw * dh * fit * fit * 3)
		{
			w = (int)(dw * fit);
			h = (int)(dh * fit);
			*wholescale = 0;
		}
	}

	w &= ~7;	// the span drawers like multiples of 8
	if (w < MIN_WIDTH)
		w = MIN_WIDTH;
	if (h < MIN_HEIGHT)
		h = MIN_HEIGHT;
	*width = w;
	*height = h;
}

void	VID_Init (unsigned char *palette)
{
	int		width, height;
	int		winwidth, winheight;
	int		dw, dh;
	int		scale;
	int		fullscreen;

	Cvar_RegisterVariable (&vid_fullscreen);
	Cvar_RegisterVariable (&vid_vsync);
	Cvar_RegisterVariable (&vid_integerscale);
	Cvar_RegisterVariable (&vid_filter);
	Cvar_RegisterVariable (&vid_maxpixels);

	// -width/-height set the size the renderer draws at; the window can
	// then be resized freely and the frame is scaled to fit it. In the
	// browser, without them, the frame follows the canvas.
	vid_follow = !COM_CheckParm ("-width") && !COM_CheckParm ("-height")
		&& RL_GetDrawableSize (&dw, &dh);
	if (vid_follow)
		VID_PickSize (dw, dh, &width, &height, &vid_wholescale);
	else
	{
		width = VID_ParmValue ("-width", DEFAULT_WIDTH);
		height = VID_ParmValue ("-height", DEFAULT_HEIGHT);
	}
	if (width < MIN_WIDTH)
		width = MIN_WIDTH;
	if (height < MIN_HEIGHT)
		height = MIN_HEIGHT;
	if (width > MAXWIDTH)
		width = MAXWIDTH;
	if (height > MAXHEIGHT)
		height = MAXHEIGHT;
	width &= ~7;	// the span drawers like multiples of 8

	// -scale sets the starting window size in multiples of the frame size
	scale = VID_ParmValue ("-scale", width <= 400 ? 2 : 1);
	if (scale < 1)
		scale = 1;
	winwidth = VID_ParmValue ("-winwidth", vid_follow ? dw : width * scale);
	winheight = VID_ParmValue ("-winheight", vid_follow ? dh : height * scale);

	fullscreen = COM_CheckParm ("-fullscreen") != 0;
	if (COM_CheckParm ("-window"))
		fullscreen = 0;

	vid.maxwarpwidth = WARP_WIDTH;
	vid.maxwarpheight = WARP_HEIGHT;
	vid.numpages = 1;
	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));
	vid.direct = NULL;

	VID_SetSize (width, height);

	VID_SetPalette (palette);

	if (!RL_OpenWindow ("Quake", width, height, winwidth, winheight,
		fullscreen, (int)vid_vsync.value))
		Sys_Error ("Couldn't open a window (is there a display?)");

	// the command line wins over config.cfg, which is read later
	if (fullscreen)
		Cvar_SetValue ("vid_fullscreen", 1);

	Con_SafePrintf ("Video mode %dx%d, window %dx%d\n", width, height,
		winwidth, winheight);
}

// Follows the drawable (the browser's canvas) once a new size has held for
// a moment, so dragging a window edge does not reallocate every frame.
void	VID_CheckResize (void)
{
	int		dw, dh, w, h, wholescale;
	double	now;

	if (!vid_follow || !RL_GetDrawableSize (&dw, &dh))
		return;
	VID_PickSize (dw, dh, &w, &h, &wholescale);
	if (w == vid.width && h == vid.height)
	{
		vid_wholescale = wholescale;
		vid_pendwidth = vid_pendheight = 0;
		return;
	}

	now = Sys_FloatTime ();
	if (w != vid_pendwidth || h != vid_pendheight)
	{
		vid_pendwidth = w;
		vid_pendheight = h;
		vid_pendtime = now;
		return;
	}
	if (now - vid_pendtime < 0.25)
		return;

	vid_pendwidth = vid_pendheight = 0;
	vid_wholescale = wholescale;
	VID_SetSize (w, h);
	RL_SetFrameSize (w, h);
	Con_Printf ("Video mode %dx%d, canvas %dx%d\n", w, h, dw, dh);
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

	RL_Present (rgbabuffer, (int)vid_integerscale.value
		|| (vid_follow && vid_wholescale), (int)vid_filter.value);
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
