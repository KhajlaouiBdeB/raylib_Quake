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
// sys_raylib.c -- system driver for the raylib build

// Files go through stdio and the clock is a monotonic one, so little here
// is tied to one OS; the POSIX parts (the clock, mkdir, reading server
// commands from stdin) are marked. The window, input and present calls are
// in the other *_raylib.c files.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <conio.h>
#else
#include <unistd.h>
#include <sys/select.h>
#include <dirent.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <limits.h>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "quakedef.h"

qboolean	isDedicated;

static qboolean	nostdout;

extern int	vcrFile;
extern int	recording;

/*
===============================================================================

FILE IO

===============================================================================
*/

#define	MAX_HANDLES		32
static FILE	*sys_handles[MAX_HANDLES];

static int findhandle (void)
{
	int		i;

	for (i = 1; i < MAX_HANDLES; i++)
		if (!sys_handles[i])
			return i;
	Sys_Error ("out of handles");
	return -1;
}

static int sys_filelength (FILE *f)
{
	long	pos;
	long	end;

	pos = ftell (f);
	fseek (f, 0, SEEK_END);
	end = ftell (f);
	fseek (f, pos, SEEK_SET);

	return (int)end;
}

#ifndef _WIN32
// Finds path ignoring the case of each component, for case-sensitive
// volumes (a case-sensitive APFS volume, an external disk) holding data
// named PAK0.PAK or ID1. Returns 1 and fills out if something matched.
static int FindCaseInsensitive (const char *path, char *out, int outsize)
{
	char			work[MAX_OSPATH];
	char			cur[MAX_OSPATH];
	char			*part, *next;
	DIR				*dir;
	struct dirent	*de;
	int				found;

	if (strlen (path) >= sizeof(work))
		return 0;
	strcpy (work, path);

	cur[0] = 0;
	if (work[0] == '/')
	{
		strcpy (cur, "/");
		part = work + 1;
	}
	else
		part = work;

	while (*part)
	{
		next = strchr (part, '/');
		if (next)
			*next++ = 0;
		if (*part)
		{
			dir = opendir (cur[0] ? cur : ".");
			if (!dir)
				return 0;
			found = 0;
			while ((de = readdir (dir)) != NULL)
			{
				if (!strcasecmp (de->d_name, part))
				{
					if (strlen (cur) + strlen (de->d_name) + 2 > sizeof(cur))
						break;
					if (cur[0] && cur[strlen (cur) - 1] != '/')
						strcat (cur, "/");
					strcat (cur, de->d_name);
					found = 1;
					break;
				}
			}
			closedir (dir);
			if (!found)
				return 0;
		}
		if (!next)
			break;
		part = next;
	}

	if (strlen (cur) >= (size_t)outsize)
		return 0;
	strcpy (out, cur);
	return 1;
}
#else
static int FindCaseInsensitive (const char *path, char *out, int outsize)
{
	(void)path; (void)out; (void)outsize;
	return 0;
}
#endif

static FILE *OpenReadFile (const char *path)
{
	FILE	*f;
	char	fixed[MAX_OSPATH];

	f = fopen (path, "rb");
	if (!f && errno == ENOENT && FindCaseInsensitive (path, fixed, sizeof(fixed)))
		f = fopen (fixed, "rb");
	return f;
}

int Sys_FileOpenRead (char *path, int *hndl)
{
	FILE	*f;
	int		i;

	i = findhandle ();

	f = OpenReadFile (path);
	if (!f)
	{
		*hndl = -1;
		return -1;
	}
	sys_handles[i] = f;
	*hndl = i;

	return sys_filelength (f);
}

int Sys_FileOpenWrite (char *path)
{
	FILE	*f;
	int		i;

	i = findhandle ();

	f = fopen (path, "wb");
	if (!f)
		Sys_Error ("Error opening %s: %s", path, strerror (errno));
	sys_handles[i] = f;

	return i;
}

void Sys_FileClose (int handle)
{
	if (handle < 1 || handle >= MAX_HANDLES || !sys_handles[handle])
		return;
	fclose (sys_handles[handle]);
	sys_handles[handle] = NULL;
}

void Sys_FileSeek (int handle, int position)
{
	fseek (sys_handles[handle], position, SEEK_SET);
}

int Sys_FileRead (int handle, void *dest, int count)
{
	return (int)fread (dest, 1, count, sys_handles[handle]);
}

int Sys_FileWrite (int handle, void *data, int count)
{
	return (int)fwrite (data, 1, count, sys_handles[handle]);
}

int Sys_FileTime (char *path)
{
	struct stat	buf;
	char		fixed[MAX_OSPATH];

	if (stat (path, &buf) == -1)
	{
		if (errno != ENOENT || !FindCaseInsensitive (path, fixed, sizeof(fixed))
			|| stat (fixed, &buf) == -1)
			return -1;
	}

	return (int)buf.st_mtime;
}

void Sys_mkdir (char *path)
{
#ifdef _WIN32
	_mkdir (path);
#else
	mkdir (path, 0777);		// POSIX
#endif
}

/*
===============================================================================

SYSTEM IO

===============================================================================
*/

void Sys_MakeCodeWriteable (unsigned long startaddr, unsigned long length)
{
// only the x86 assembly modifies its own code, and it isn't built
}

void Sys_DebugLog (char *file, char *fmt, ...)
{
	va_list		argptr;
	FILE		*f;

	f = fopen (file, "a");
	if (!f)
		return;
	va_start (argptr, fmt);
	vfprintf (f, fmt, argptr);
	va_end (argptr);
	fclose (f);
}

void Sys_Error (char *error, ...)
{
	va_list		argptr;
	char		string[1024];
	static qboolean	inerror;

	va_start (argptr, error);
	vsnprintf (string, sizeof(string), error, argptr);
	va_end (argptr);
	fprintf (stderr, "Error: %s\n", string);

	// an error while shutting down only gets printed
	if (!inerror)
	{
		inerror = true;
		Host_Shutdown ();	// closes the window too
	}
#ifdef __EMSCRIPTEN__
	// The page shows the message and offers a reload.
	emscripten_cancel_main_loop ();
	EM_ASM ({ if (Module.quakeError) Module.quakeError (UTF8ToString ($0)); },
		string);
	emscripten_force_exit (1);
#endif
	exit (1);
}

void Sys_Printf (char *fmt, ...)
{
	va_list		argptr;
	char		text[2048];
	unsigned char	*p;

	if (nostdout)
		return;

	va_start (argptr, fmt);
	vsnprintf (text, sizeof(text), fmt, argptr);
	va_end (argptr);

	// Quake's high-bit "red" characters print as plain ones
	for (p = (unsigned char *)text; *p; p++)
	{
		*p &= 0x7f;
		if (*p < 32 && *p != 10 && *p != 13 && *p != 9)
			printf ("[%02x]", *p);
		else
			putc (*p, stdout);
	}
}

void Sys_Quit (void)
{
	Host_Shutdown ();
	fflush (stdout);
#ifdef __EMSCRIPTEN__
	// config.cfg was just written; the page saves it and offers a reload.
	emscripten_cancel_main_loop ();
	EM_ASM ({ if (Module.quakeQuit) Module.quakeQuit (); });
	emscripten_force_exit (0);
#endif
	exit (0);
}

double Sys_FloatTime (void)
{
#ifdef _WIN32
	static LARGE_INTEGER	freq;
	static LARGE_INTEGER	start;
	LARGE_INTEGER			now;

	if (!freq.QuadPart)
	{
		QueryPerformanceFrequency (&freq);
		QueryPerformanceCounter (&start);
	}
	QueryPerformanceCounter (&now);
	return (double)(now.QuadPart - start.QuadPart) / (double)freq.QuadPart;
#else
	struct timespec	ts;
	static time_t	secbase;

	// POSIX monotonic clock: the wall clock can jump
	clock_gettime (CLOCK_MONOTONIC, &ts);
	if (!secbase)
		secbase = ts.tv_sec;

	return (ts.tv_sec - secbase) + ts.tv_nsec / 1000000000.0;
#endif
}

// Commands typed into the terminal of a dedicated server.
char *Sys_ConsoleInput (void)
{
#ifdef _WIN32
	static char	text[256];
	static int	len;
	int			c;

	if (cls.state != ca_dedicated)
		return NULL;

	if (!_kbhit ())
		return NULL;

	while (_kbhit ())
	{
		c = _getche ();
		if (c == '\r' || c == '\n')
		{
			text[len] = 0;
			len = 0;
			putchar ('\n');
			return text;
		}
		if (c == '\b')
		{
			if (len > 0)
			{
				len--;
				putchar (' ');
				putchar ('\b');
			}
		}
		else if (len < (int)sizeof(text) - 1)
		{
			text[len++] = (char)c;
		}
	}
	return NULL;
#else
	static char	text[256];
	int			len;
	fd_set		fdset;
	struct timeval	timeout;

	if (cls.state != ca_dedicated)
		return NULL;

	// POSIX
	FD_ZERO (&fdset);
	FD_SET (0, &fdset);
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	if (select (1, &fdset, NULL, NULL, &timeout) == -1 || !FD_ISSET (0, &fdset))
		return NULL;

	len = read (0, text, sizeof(text) - 1);
	if (len < 1)
		return NULL;
	text[len-1] = 0;	// rip off the \n and terminate

	return text;
#endif
}

void Sys_Sleep (void)
{
#ifdef _WIN32
	Sleep (1);
#else
	struct timespec	ts = { 0, 1000000 };	// 1 ms

	nanosleep (&ts, NULL);
#endif
}

void Sys_Init (void)
{
}

void Sys_HighFPPrecision (void)
{
}

void Sys_LowFPPrecision (void)
{
}

//=============================================================================

#ifdef __APPLE__
static int HasGameData (const char *dir)
{
	char	path[MAX_OSPATH];
	struct stat	st;

	snprintf (path, sizeof(path), "%s/id1", dir);
	return stat (path, &st) == 0 && S_ISDIR (st.st_mode);
}

// Where the game data is when -basedir isn't given: the current directory
// (terminal use), next to the executable or inside an app bundle's
// Resources (double click, where the current directory is "/"), then
// ~/Library/Application Support/raylib_Quake. The last is also where an
// empty id1 is made on a first run, so saves and config.cfg have a home.
static const char *DefaultBaseDir (void)
{
	static char	dir[MAX_OSPATH];
	char		exe[PATH_MAX];
	uint32_t	size = sizeof(exe);
	char		*slash;
	const char	*home;

	if (HasGameData ("."))
		return ".";

	if (_NSGetExecutablePath (exe, &size) == 0 && (slash = strrchr (exe, '/')))
	{
		*slash = 0;
		if (HasGameData (exe))
		{
			snprintf (dir, sizeof(dir), "%s", exe);
			return dir;
		}
		snprintf (dir, sizeof(dir), "%s/../Resources", exe);
		if (HasGameData (dir))
			return dir;
	}

	home = getenv ("HOME");
	if (home)
	{
		snprintf (dir, sizeof(dir), "%s/Library", home);
		mkdir (dir, 0777);
		snprintf (dir, sizeof(dir), "%s/Library/Application Support", home);
		mkdir (dir, 0777);
		snprintf (dir, sizeof(dir), "%s/Library/Application Support/raylib_Quake", home);
		mkdir (dir, 0777);
		strcat (dir, "/id1");
		mkdir (dir, 0777);
		dir[strlen (dir) - 4] = 0;
		return dir;
	}

	return ".";
}
#endif

#ifdef __EMSCRIPTEN__
static double	frame_oldtime;

// One browser frame (requestAnimationFrame, so at the display's rate). The
// native loop below waits for the next host frame; here the browser does
// the waiting, and Host_Frame runs once per call. Host_Frame holds the game
// at 72 frames a second and returns at once in between, as before.
static void Sys_WebFrame (void)
{
	double	newtime, time;

	newtime = Sys_FloatTime ();
	time = newtime - frame_oldtime;
	if (time > sys_ticrate.value*2)
		frame_oldtime = newtime;
	else
		frame_oldtime += time;

	Host_Frame (time);

	// The page saves config.cfg and saved games to IndexedDB when they
	// change; tell it at most once a second that it may look.
	{
		static double	lastsync;

		if (newtime - lastsync > 1.0)
		{
			lastsync = newtime;
			EM_ASM ({ if (Module.quakeSyncFiles) Module.quakeSyncFiles (); });
		}
	}
}
#endif

int main (int argc, char **argv)
{
	quakeparms_t	parms;
	double			time, oldtime, newtime;
	int				oldframecount;
	int				j;

	signal (SIGFPE, SIG_IGN);
#ifdef SIGPIPE
	signal (SIGPIPE, SIG_IGN);	// a closed pipe on stdout or a socket
#endif

	memset (&parms, 0, sizeof(parms));

	COM_InitArgv (argc, argv);
	parms.argc = com_argc;
	parms.argv = com_argv;

	parms.memsize = 16*1024*1024;
	j = COM_CheckParm ("-mem");
	if (j && j < com_argc - 1)
		parms.memsize = (int)(Q_atof (com_argv[j+1]) * 1024 * 1024);
	parms.membase = malloc (parms.memsize);
	if (!parms.membase)
		Sys_Error ("Not enough memory free; check disk space\n");

	// -basedir is read by COM_InitFilesystem
#ifdef __APPLE__
	parms.basedir = (char *)DefaultBaseDir ();
#elif defined(__EMSCRIPTEN__)
	parms.basedir = "/quake";	// the page preloads /quake/id1/pak0.pak
#else
	parms.basedir = ".";
#endif
// caching is disabled by default, use -cachedir to enable

	isDedicated = COM_CheckParm ("-dedicated") != 0;
	nostdout = COM_CheckParm ("-nostdout") != 0;

#ifndef _WIN32
	if (isDedicated)
		fcntl (0, F_SETFL, fcntl (0, F_GETFL, 0) | O_NONBLOCK);	// POSIX
#endif

	printf ("raylib Quake -- Version %0.3f\n", VERSION);

	Host_Init (&parms);

	Sys_Init ();

#ifdef __EMSCRIPTEN__
	// Never returns: the browser calls Sys_WebFrame on every display frame.
	frame_oldtime = Sys_FloatTime () - 0.1;
	emscripten_set_main_loop (Sys_WebFrame, 0, 1);
#endif

	oldtime = Sys_FloatTime () - 0.1;
	while (1)
	{
	// find time spent rendering last frame
		newtime = Sys_FloatTime ();
		time = newtime - oldtime;

		if (cls.state == ca_dedicated)
		{	// play vcrfiles at max speed
			if (time < sys_ticrate.value && (vcrFile == -1 || recording))
			{
				Sys_Sleep ();
				continue;		// not time to run a server only tic yet
			}
			time = sys_ticrate.value;
		}

		if (time > sys_ticrate.value*2)
			oldtime = newtime;
		else
			oldtime += time;

		oldframecount = host_framecount;
		Host_Frame (time);

		// Host_Frame runs at most 72 frames a second and returns at once
		// in between; don't spin the CPU waiting. With vsync on, the
		// display already holds each frame back in EndDrawing.
		if (host_framecount == oldframecount && !cls.timedemo)
			Sys_Sleep ();
	}

	return 0;
}
