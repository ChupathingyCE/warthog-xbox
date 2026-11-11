/*
XBOX_SAVES.C

The saves of builds before 0.9.1, made under cxbe's default title ID
(include/xbox_saves.h), copied once to Warthog's own U:\. Only the game's
own save folders are taken (one holding blam.sav or blam.lst: profiles and
the rest of saved_game_files.c's), since other programs with the same
default ID saved beside them. A folder of the same name already in U:\ is
left alone, and nothing in the old folder is changed or deleted.

A folder is copied whole or not at all, even if the power goes in the
middle:
- T:\saves_copying.dat names the folder being copied, written before its
  first file and deleted after its last. Found at a start, it means a copy
  was cut off: that folder (which the copy made, so nothing of the
  player's) is removed and copied again.
- SaveMeta.xbx, what makes a folder a save to the dashboard and to the
  game's own list (XFindFirstSaveGame), is copied last, so a folder cut off
  before it is never taken for a save.
- A failed copy removes what it made, and the next start tries again.

When every folder has been copied, T:\saves_moved.dat says so and the copy
never runs again, so a save deleted later stays deleted - as long as the
title's T: data (E:\TDATA\43480001) is kept. Clearing it (the dashboard's
Memory screen) brings the old saves back once more.
*/

#include <xtl.h>
#include <stdio.h>
#include <string.h>

#include "../include/xbox_saves.h"

void xbox_boot_log(const char *format, ...);

#define MOVED_MARKER "T:\\saves_moved.dat"
#define COPYING_MARKER "T:\\saves_copying.dat"
#define SAVE_META "SaveMeta.xbx"

static int path_exists(const char *path)
{
	return GetFileAttributesA(path) != (DWORD)-1;
}

/* a folder copy_folder made, and its files, taken back (it was new:
CreateDirectory refuses one already there) */
static void remove_new_folder(const char *folder)
{
	char pattern[MAX_PATH], path[MAX_PATH];
	WIN32_FIND_DATAA found;
	HANDLE find;

	_snprintf(pattern, sizeof(pattern), "%s\\*", folder);
	pattern[sizeof(pattern) - 1] = 0;
	find = FindFirstFileA(pattern, &found);
	if (find != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;
			_snprintf(path, sizeof(path), "%s\\%s", folder, found.cFileName);
			path[sizeof(path) - 1] = 0;
			DeleteFileA(path);
		} while (FindNextFileA(find, &found));
		FindClose(find);
	}
	RemoveDirectoryA(folder);
}

static int copy_file(const char *from, const char *to, const char *name)
{
	char source[MAX_PATH], target[MAX_PATH];

	_snprintf(source, sizeof(source), "%s\\%s", from, name);
	source[sizeof(source) - 1] = 0;
	_snprintf(target, sizeof(target), "%s\\%s", to, name);
	target[sizeof(target) - 1] = 0;
	/* (never over a file already there) */
	return CopyFileA(source, target, TRUE) != 0;
}

/* one save folder's files (the folder is flat: blam.sav, blam.lst,
SaveImage.xbx, and SaveMeta.xbx, which goes last); 0, and nothing left in
U:\, if any failed or the old folder could not be read */
static int copy_folder(const char *from, const char *to)
{
	char pattern[MAX_PATH];
	WIN32_FIND_DATAA found;
	HANDLE find;
	int ok = 1, has_meta = 0;

	if (!CreateDirectoryA(to, NULL))
		return 0;
	_snprintf(pattern, sizeof(pattern), "%s\\*", from);
	pattern[sizeof(pattern) - 1] = 0;
	find = FindFirstFileA(pattern, &found);
	if (find == INVALID_HANDLE_VALUE)
	{
		remove_new_folder(to);
		return 0;
	}
	do
	{
		if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;
		if (!_stricmp(found.cFileName, SAVE_META))
		{
			has_meta = 1;
			continue;
		}
		if (!copy_file(from, to, found.cFileName))
			ok = 0;
	} while (FindNextFileA(find, &found));
	FindClose(find);
	if (ok && has_meta && !copy_file(from, to, SAVE_META))
		ok = 0;
	if (!ok)
		remove_new_folder(to);
	return ok;
}

static void write_marker(const char *path, const char *text)
{
	FILE *file = fopen(path, "wb");

	if (file)
	{
		fputs(text, file);
		fclose(file);
	}
}

/* a copy cut off at an earlier start (COPYING_MARKER): its folder removed,
so the copy below makes it again */
static void undo_cut_off_copy(void)
{
	char name[MAX_PATH], folder[MAX_PATH];
	FILE *file = fopen(COPYING_MARKER, "rb");
	size_t length;

	if (!file)
		return;
	length = fread(name, 1, sizeof(name) - 1, file);
	fclose(file);
	name[length] = 0;
	/* (a folder name of U:\'s, nothing else: no separators) */
	if (length && !strchr(name, '\\') && !strchr(name, '/') && !strchr(name, ':') && strcmp(name, ".") &&
		strcmp(name, ".."))
	{
		_snprintf(folder, sizeof(folder), "U:\\%s", name);
		folder[sizeof(folder) - 1] = 0;
		if (path_exists(folder))
		{
			remove_new_folder(folder);
			xbox_boot_log("saves: the copy of %s was cut off at an earlier start; copying it again", name);
		}
	}
	DeleteFileA(COPYING_MARKER);
}

void xbox_saves_move_old(void)
{
	char drive = 0;
	char root[8], pattern[16], from[MAX_PATH], to[MAX_PATH], check[MAX_PATH], line[64];
	WIN32_FIND_DATAA found;
	HANDLE find;
	DWORD result;
	int copied = 0, kept = 0, failed = 0;

	if (path_exists(MOVED_MARKER))
		return;
	undo_cut_off_copy();
	result = XMountAlternateTitleA("U:\\", XBOX_OLD_TITLE_ID, &drive);
	if (result != ERROR_SUCCESS || !drive)
	{
		/* (no old folder, or the XBE has no alternate ID: nothing to do,
		and asked again next start) */
		xbox_boot_log("saves: the old title ID's saves are not there to copy (%lu)", (unsigned long)result);
		return;
	}
	_snprintf(root, sizeof(root), "%c:", drive);
	_snprintf(pattern, sizeof(pattern), "%c:\\*", drive);
	find = FindFirstFileA(pattern, &found);
	if (find != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				continue;
			_snprintf(from, sizeof(from), "%s\\%s", root, found.cFileName);
			from[sizeof(from) - 1] = 0;
			/* (the game's own folders only) */
			_snprintf(check, sizeof(check), "%s\\blam.sav", from);
			check[sizeof(check) - 1] = 0;
			if (!path_exists(check))
			{
				_snprintf(check, sizeof(check), "%s\\blam.lst", from);
				check[sizeof(check) - 1] = 0;
				if (!path_exists(check))
					continue;
			}
			_snprintf(to, sizeof(to), "U:\\%s", found.cFileName);
			to[sizeof(to) - 1] = 0;
			if (path_exists(to))
			{
				kept++;
				xbox_boot_log("saves: %s is already in U:\\, the old copy left as it is", found.cFileName);
				continue;
			}
			write_marker(COPYING_MARKER, found.cFileName);
			if (copy_folder(from, to))
			{
				copied++;
				xbox_boot_log("saves: copied %s from the old title ID's saves", found.cFileName);
			}
			else
			{
				failed++;
				xbox_boot_log("saves: copying %s from the old title ID's saves FAILED (%lu)", found.cFileName,
					(unsigned long)GetLastError());
			}
			DeleteFileA(COPYING_MARKER);
		} while (FindNextFileA(find, &found));
		FindClose(find);
	}
	XUnmountAlternateTitleA(drive);
	xbox_boot_log("saves: %d copied from E:\\UDATA\\ffff0002, %d already here, %d failed", copied, kept, failed);
	/* (a failure is tried again at the next start) */
	if (failed)
		return;
	_snprintf(line, sizeof(line), "%d copied, %d already here\n", copied, kept);
	line[sizeof(line) - 1] = 0;
	write_marker(MOVED_MARKER, line);
}
