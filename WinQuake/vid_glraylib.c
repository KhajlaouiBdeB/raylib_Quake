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
// vid_glraylib.c -- GLQuake's video driver on a raylib window

// The window, input and clock are the software build's (bridge_raylib.c,
// in_raylib.c, sys_raylib.c); GLQuake draws through gl_raylib.c.
//
// GLQuake renders at the window's real size. The 2D layer (console, menus,
// status bar) is laid out in a virtual vid.width x vid.height screen that
// the hardware scales: its width is -conwidth (640 by default) and its
// height follows the window's shape, so pixels stay square in 2D and 3D.

#include "quakedef.h"		// with glquake.h, for GLQUAKE
#include "bridge_raylib.h"

#define	DEFAULT_WIDTH	640
#define	DEFAULT_HEIGHT	480

unsigned short	d_8to16table[256];
unsigned		d_8to24table[256];
unsigned char	d_15to8table[65536];	// only for 8-bit texture extensions

int		texture_mode = GL_LINEAR;
int		texture_extension_number = 1;

float	gldepthmin, gldepthmax;

// Alternating depth ranges to save a depth clear was a 1997 fill-rate
// trick; a clear is free now.
cvar_t	gl_ztrick = {"gl_ztrick", "0"};

const char *gl_vendor;
const char *gl_renderer;
const char *gl_version;
const char *gl_extensions;

qboolean	isPermedia = false;
qboolean	gl_mtexable = false;	// no GL_SGIS_multitexture here

cvar_t	vid_fullscreen = {"vid_fullscreen", "0"};	// not saved: -fullscreen or Alt+Enter
cvar_t	vid_vsync = {"vid_vsync", "1", true};

static int		scr_width, scr_height;		// the window, in pixels
static int		fixedconheight;				// -conheight
static float	vid_gamma = 1.0;

void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height)
{
}

void D_EndDirectRect (int x, int y, int width, int height)
{
}

void VID_SetPalette (unsigned char *palette)
{
	byte		*pal;
	unsigned	r, g, b;
	int			i;

	pal = palette;
	for (i = 0; i < 256; i++, pal += 3)
	{
		r = pal[0];
		g = pal[1];
		b = pal[2];
		d_8to24table[i] = (255u << 24) | r | (g << 8) | (b << 16);
	}
	d_8to24table[255] &= 0xffffff;	// 255 is transparent
}

// GLQuake does flashes with a blended polygon (gl_polyblend), not with
// the palette.
void VID_ShiftPalette (unsigned char *palette)
{
}

qboolean VID_Is8bit (void)
{
	return false;
}

// Brightens the palette the textures are made from, as GLQuake did on
// every card but a 3Dfx: -gamma, 0.7 by default. 1 keeps the software
// renderer's colours.
static void Check_Gamma (unsigned char *pal)
{
	float	f, inf;
	unsigned char	palette[768];
	int		i;

	if ((i = COM_CheckParm ("-gamma")) != 0 && i < com_argc - 1)
		vid_gamma = Q_atof (com_argv[i+1]);
	else
		vid_gamma = 0.7;
	if (vid_gamma <= 0)
		vid_gamma = 1;

	for (i = 0; i < 768; i++)
	{
		f = pow ((pal[i] + 1) / 256.0, vid_gamma);
		inf = f * 255 + 0.5;
		if (inf < 0)
			inf = 0;
		if (inf > 255)
			inf = 255;
		palette[i] = inf;
	}

	memcpy (pal, palette, sizeof(palette));
}

void GL_Init (void)
{
	gl_vendor = (const char *)glGetString (GL_VENDOR);
	Con_Printf ("GL_VENDOR: %s\n", gl_vendor);
	gl_renderer = (const char *)glGetString (GL_RENDERER);
	Con_Printf ("GL_RENDERER: %s\n", gl_renderer);
	gl_version = (const char *)glGetString (GL_VERSION);
	Con_Printf ("GL_VERSION: %s\n", gl_version);
	gl_extensions = (const char *)glGetString (GL_EXTENSIONS);

	glClearColor (1, 0, 0, 0);
	glCullFace (GL_FRONT);
	glEnable (GL_TEXTURE_2D);

	glEnable (GL_ALPHA_TEST);
	glAlphaFunc (GL_GREATER, 0.666);

	glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	glShadeModel (GL_FLAT);

	glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}

// Keeps the 2D screen's height in step with the window's shape.
static void VID_SetConsoleSize (int width, int height)
{
	int		conheight;

	if (fixedconheight)
		return;
	conheight = (int)((float)vid.conwidth * height / width + 0.5f);
	if (conheight < 200)
		conheight = 200;
	if (conheight == (int)vid.conheight)
		return;
	vid.height = vid.conheight = conheight;
	vid.recalc_refdef = 1;
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

void GL_BeginRendering (int *x, int *y, int *width, int *height)
{
	int		w, h;

	VID_CheckCvars ();

	RL_BeginGLFrame (&w, &h);
	if (w < 1)
		w = 1;
	if (h < 1)
		h = 1;
	if (w != scr_width || h != scr_height)
	{
		scr_width = w;
		scr_height = h;
		VID_SetConsoleSize (w, h);
	}
	RGL_BeginFrame (w, h);

	*x = *y = 0;
	*width = scr_width;
	*height = scr_height;
}

void GL_EndRendering (void)
{
	RGL_EndFrame ();
	RL_EndGLFrame ();	// also polls input, see RL_PumpEvents
}

static int VID_ParmValue (char *parm, int defvalue)
{
	int		i;

	i = COM_CheckParm (parm);
	if (i && i < com_argc - 1)
		return Q_atoi (com_argv[i+1]);
	return defvalue;
}

void VID_Init (unsigned char *palette)
{
	int		width, height;
	int		fullscreen;
	char	gldir[MAX_OSPATH];

	Cvar_RegisterVariable (&gl_ztrick);
	Cvar_RegisterVariable (&vid_fullscreen);
	Cvar_RegisterVariable (&vid_vsync);

	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));

	// -width/-height: the window; it can be resized afterwards
	width = VID_ParmValue ("-width", DEFAULT_WIDTH);
	height = VID_ParmValue ("-height", DEFAULT_HEIGHT);
	if (width < 320)
		width = 320;
	if (height < 200)
		height = 200;

	fullscreen = COM_CheckParm ("-fullscreen") != 0;
	if (COM_CheckParm ("-window"))
		fullscreen = 0;

	vid.conwidth = VID_ParmValue ("-conwidth", 640);
	vid.conwidth &= ~7;
	if (vid.conwidth < 320)
		vid.conwidth = 320;
	if (vid.conwidth > width)
		vid.conwidth = width & ~7;
	vid.width = vid.conwidth;
	vid.conheight = 0;
	VID_SetConsoleSize (width, height);
	if (COM_CheckParm ("-conheight"))
	{
		vid.height = vid.conheight = VID_ParmValue ("-conheight", vid.conheight);
		fixedconheight = 1;
	}

	vid.aspect = ((float)vid.height / (float)vid.width) * (320.0 / 240.0);
	vid.numpages = 2;

	vid.buffer = vid.conbuffer = vid.direct = NULL;
	vid.rowbytes = vid.conrowbytes = 0;

	if (!RL_OpenWindow ("GLQuake", width, height, width, height,
		fullscreen, (int)vid_vsync.value))
		Sys_Error ("Couldn't open a window (is there a display?)");
	if (!RGL_Init ())
		Sys_Error ("Couldn't set up OpenGL (needs OpenGL 3.3 or OpenGL ES 3)");

	if (fullscreen)
		Cvar_SetValue ("vid_fullscreen", 1);

	GL_Init ();

	Q_snprintf (gldir, sizeof(gldir), "%s/glquake", com_gamedir);
	Sys_mkdir (gldir);

	Check_Gamma (palette);
	VID_SetPalette (palette);

	vid.recalc_refdef = 1;
	Con_SafePrintf ("Video mode %dx%d (%dx%d 2D), gamma %g\n", width, height,
		vid.conwidth, vid.conheight, vid_gamma);
}

void VID_Shutdown (void)
{
	if (!RL_WindowOpen ())
		return;
	RGL_Shutdown ();
	RL_CloseWindow ();
}
