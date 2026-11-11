/*
XBOX_SAVES.C

The saves of builds before 0.9.1, made under cxbe's default title ID
(include/xbox_saves.h), copied once to Warthog's own U:\. Only the game's
own save folders are taken (one holding blam.sav or blam.lst: profiles and
the rest of saved_game_files.c's), since other programs with the same
default ID saved beside them. A folder of the same name already in U:\ is
left alone, and nothing in the old folder is changed or deleted. When the
copy has run, T:\saves_moved.dat says so, and it never runs again: a save
deleted later stays deleted.
*/

#include <xtl.h>
#include <stdio.h>
#include <string.h>

#include "../include/xbox_saves.h"

void xbox_boot_log(const char *format, ...);

#define MOVED_MARKER "T:\\saves_moved.dat"

static int path_exists(const char *path)
{
	return GetFileAttributesA(path) != (DWORD)-1;
}

/* a folder copy_folder made, and its files, taken back after a failed copy
(it was new: CreateDirectory refuses one already there) */
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

/* one save folder's files (the folder is flat: blam.sav, blam.lst,
SaveMeta.xbx, SaveImage.xbx); 0 if any failed */
static int copy_folder(const char *from, const char *to)
{
	char pattern[MAX_PATH], source[MAX_PATH], target[MAX_PATH];
	WIN32_FIND_DATAA found;
	HANDLE find;
	int ok = 1;

	if (!CreateDirectoryA(to, NULL))
		return 0;
	_snprintf(pattern, sizeof(pattern), "%s\\*", from);
	pattern[sizeof(pattern) - 1] = 0;
	find = FindFirstFileA(pattern, &found);
	if (find == INVALID_HANDLE_VALUE)
		return 1;
	do
	{
		if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;
		_snprintf(source, sizeof(source), "%s\\%s", from, found.cFileName);
		source[sizeof(source) - 1] = 0;
		_snprintf(target, sizeof(target), "%s\\%s", to, found.cFileName);
		target[sizeof(target) - 1] = 0;
		/* (never over a file already there) */
		if (!CopyFileA(source, target, TRUE))
			ok = 0;
	} while (FindNextFileA(find, &found));
	FindClose(find);
	/* (all of it or none: a half copy would count as already here next time) */
	if (!ok)
		remove_new_folder(to);
	return ok;
}

void xbox_saves_move_old(void)
{
	char drive = 0;
	char root[8], pattern[16], from[MAX_PATH], to[MAX_PATH], check[MAX_PATH];
	WIN32_FIND_DATAA found;
	HANDLE find;
	DWORD result;
	int copied = 0, kept = 0, failed = 0;
	FILE *marker;

	if (path_exists(MOVED_MARKER))
		return;
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
		} while (FindNextFileA(find, &found));
		FindClose(find);
	}
	XUnmountAlternateTitleA(drive);
	xbox_boot_log("saves: %d copied from E:\\UDATA\\ffff0002, %d already here, %d failed", copied, kept, failed);
	/* (a failure is tried again at the next start) */
	if (failed)
		return;
	marker = fopen(MOVED_MARKER, "wb");
	if (marker)
	{
		fprintf(marker, "%d copied, %d already here\n", copied, kept);
		fclose(marker);
	}
}
