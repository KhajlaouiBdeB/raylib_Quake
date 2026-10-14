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
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/select.h>

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

static int filelength (FILE *f)
{
	long	pos;
	long	end;

	pos = ftell (f);
	fseek (f, 0, SEEK_END);
	end = ftell (f);
	fseek (f, pos, SEEK_SET);

	return (int)end;
}

int Sys_FileOpenRead (char *path, int *hndl)
{
	FILE	*f;
	int		i;

	i = findhandle ();

	f = fopen (path, "rb");
	if (!f)
	{
		*hndl = -1;
		return -1;
	}
	sys_handles[i] = f;
	*hndl = i;

	return filelength (f);
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

	if (stat (path, &buf) == -1)
		return -1;

	return (int)buf.st_mtime;
}

void Sys_mkdir (char *path)
{
	mkdir (path, 0777);		// POSIX
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
	exit (0);
}

double Sys_FloatTime (void)
{
	struct timespec	ts;
	static time_t	secbase;

	// POSIX monotonic clock: the wall clock can jump
	clock_gettime (CLOCK_MONOTONIC, &ts);
	if (!secbase)
		secbase = ts.tv_sec;

	return (ts.tv_sec - secbase) + ts.tv_nsec / 1000000000.0;
}

// Commands typed into the terminal of a dedicated server.
char *Sys_ConsoleInput (void)
{
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
}

void Sys_Sleep (void)
{
	struct timespec	ts = { 0, 1000000 };	// 1 ms

	nanosleep (&ts, NULL);
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

int main (int argc, char **argv)
{
	quakeparms_t	parms;
	double			time, oldtime, newtime;
	int				oldframecount;
	int				j;

	signal (SIGFPE, SIG_IGN);

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
	parms.basedir = ".";
// caching is disabled by default, use -cachedir to enable

	isDedicated = COM_CheckParm ("-dedicated") != 0;
	nostdout = COM_CheckParm ("-nostdout") != 0;

	if (isDedicated)
		fcntl (0, F_SETFL, fcntl (0, F_GETFL, 0) | O_NONBLOCK);	// POSIX

	printf ("raylib Quake -- Version %0.3f\n", VERSION);

	Host_Init (&parms);

	Sys_Init ();

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
