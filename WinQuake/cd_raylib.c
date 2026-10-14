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
// cd_raylib.c -- CD audio emulation with music/trackNN.ogg|wav playback via raylib

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "raylib.h"

typedef struct cvar_s
{
	char	*name;
	char	*string;
	int		archive;
	char	*server;
	float	value;
	struct cvar_s *next;
} cvar_t;

extern void Con_Printf (const char *fmt, ...);
extern int COM_CheckParm (const char *parm);
extern void Cvar_RegisterVariable (cvar_t *variable);
extern float Cvar_VariableValue (char *var_name);
extern char *Cvar_VariableString (char *var_name);

static cvar_t cd_music_cvar = {"cd_music", "1", 1, NULL, 1.0f, NULL};

int CDAudio_Init (void);
void CDAudio_Play (unsigned char track, int looping);
void CDAudio_Stop (void);
void CDAudio_Pause (void);
void CDAudio_Resume (void);
void CDAudio_Update (void);
void CDAudio_Shutdown (void);

static int		cd_inited = 0;
static int		cd_enabled = 1;
static Music	current_music;
static int		music_loaded = 0;
static int		music_playing = 0;
static int		current_track = -1;
static float	last_volume = -1.0f;

extern char com_gamedir[];

static const char *exts[] = { "ogg", "wav", "mp3", "flac", NULL };

static int FindTrackFile (int track, char *out_path, size_t max_len)
{
	char buf[1024];

	for (int i = 0; exts[i] != NULL; i++)
	{
		// Check com_gamedir/music/trackNN.ext
		if (com_gamedir[0])
		{
			snprintf (buf, sizeof(buf), "%s/music/track%02d.%s", com_gamedir, track, exts[i]);
			if (access (buf, R_OK) == 0)
			{
				snprintf (out_path, max_len, "%s", buf);
				return 1;
			}
			snprintf (buf, sizeof(buf), "%s/music/track%d.%s", com_gamedir, track, exts[i]);
			if (access (buf, R_OK) == 0)
			{
				snprintf (out_path, max_len, "%s", buf);
				return 1;
			}
		}

		// Check music/trackNN.ext and id1/music/trackNN.ext
		snprintf (buf, sizeof(buf), "music/track%02d.%s", track, exts[i]);
		if (access (buf, R_OK) == 0)
		{
			snprintf (out_path, max_len, "%s", buf);
			return 1;
		}
		snprintf (buf, sizeof(buf), "id1/music/track%02d.%s", track, exts[i]);
		if (access (buf, R_OK) == 0)
		{
			snprintf (out_path, max_len, "%s", buf);
			return 1;
		}
	}
	return 0;
}

typedef void (*xcommand_t)(void);
extern void Cmd_AddCommand (char *cmd_name, xcommand_t function);
extern int Cmd_Argc (void);
extern char *Cmd_Argv (int arg);

static void CD_f (void)
{
	char *command;

	if (Cmd_Argc () < 2)
	{
		Con_Printf ("cd [play <track> | loop <track> | stop | pause | resume | info | on | off]\n");
		return;
	}

	command = Cmd_Argv (1);

	if (strcasecmp (command, "on") == 0)
	{
		cd_enabled = 1;
		return;
	}
	if (strcasecmp (command, "off") == 0)
	{
		CDAudio_Stop ();
		cd_enabled = 0;
		return;
	}
	if (strcasecmp (command, "play") == 0)
	{
		CDAudio_Play ((unsigned char)atoi (Cmd_Argv (2)), 0);
		return;
	}
	if (strcasecmp (command, "loop") == 0)
	{
		CDAudio_Play ((unsigned char)atoi (Cmd_Argv (2)), 1);
		return;
	}
	if (strcasecmp (command, "stop") == 0)
	{
		CDAudio_Stop ();
		return;
	}
	if (strcasecmp (command, "pause") == 0)
	{
		CDAudio_Pause ();
		return;
	}
	if (strcasecmp (command, "resume") == 0)
	{
		CDAudio_Resume ();
		return;
	}
	if (strcasecmp (command, "info") == 0)
	{
		if (music_playing)
			Con_Printf ("Playing track %d\n", current_track);
		else
			Con_Printf ("No CD music track playing\n");
		return;
	}
}

int CDAudio_Init (void)
{
	if (COM_CheckParm ("-nocd"))
	{
		cd_enabled = 0;
		return -1;
	}

	Cvar_RegisterVariable (&cd_music_cvar);
	Cmd_AddCommand ("cd", CD_f);

	if (!IsAudioDeviceReady ())
	{
		SetTraceLogLevel (LOG_WARNING);
		InitAudioDevice ();
	}

	cd_inited = 1;
	cd_enabled = 1;
	music_loaded = 0;
	music_playing = 0;
	current_track = -1;
	return 0;
}

void CDAudio_Play (unsigned char track, int looping)
{
	char track_path[1024];

	if (!cd_inited || !cd_enabled)
		return;

	if (Cvar_VariableValue ("cd_music") == 0.0f)
		return;

	if (track < 2)
		return; // track 1 is data on original Quake CD

	if (music_loaded && current_track == (int)track && music_playing)
		return; // already playing this track

	CDAudio_Stop ();

	if (!FindTrackFile ((int)track, track_path, sizeof(track_path)))
	{
		// cd_null behaviour by default if file is not present
		return;
	}

	current_music = LoadMusicStream (track_path);
	if (!IsMusicValid (current_music))
	{
		Con_Printf ("CDAudio_Play: failed to load %s\n", track_path);
		return;
	}

	current_music.looping = (bool)looping;
	music_loaded = 1;
	current_track = (int)track;

	float vol = Cvar_VariableValue ("bgmvolume");
	if (vol < 0.0f) vol = 0.0f;
	if (vol > 1.0f) vol = 1.0f;
	SetMusicVolume (current_music, vol);
	last_volume = vol;

	PlayMusicStream (current_music);
	music_playing = 1;
	Con_Printf ("CDAudio_Play: playing track %d (%s)\n", (int)track, track_path);
}

void CDAudio_Stop (void)
{
	if (!cd_inited || !music_loaded)
		return;

	if (music_playing)
		StopMusicStream (current_music);

	UnloadMusicStream (current_music);
	music_loaded = 0;
	music_playing = 0;
	current_track = -1;
}

void CDAudio_Pause (void)
{
	if (!cd_inited || !music_loaded || !music_playing)
		return;

	PauseMusicStream (current_music);
}

void CDAudio_Resume (void)
{
	if (!cd_inited || !music_loaded || !music_playing)
		return;

	ResumeMusicStream (current_music);
}

void CDAudio_Update (void)
{
	if (!cd_inited || !music_loaded || !music_playing)
		return;

	UpdateMusicStream (current_music);

	float vol = Cvar_VariableValue ("bgmvolume");
	if (vol != last_volume)
	{
		if (vol < 0.0f) vol = 0.0f;
		if (vol > 1.0f) vol = 1.0f;
		SetMusicVolume (current_music, vol);
		last_volume = vol;
	}
}

void CDAudio_Shutdown (void)
{
	if (!cd_inited)
		return;

	CDAudio_Stop ();
	cd_inited = 0;
}
