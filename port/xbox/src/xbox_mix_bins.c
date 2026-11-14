/*
XBOX_MIX_BINS.C

The January SDK named a voice's mix bins with a mask, one bit per bin; the
later library takes a list of bins. For a voice of one channel the order of
the list doesn't matter. For a voice of more, the later library gives each
channel an equal run of the list, in order: the first half of a stereo
voice's bins are its left channel's and the second half its right's (the
library's voice code divides the count by the channels, and asserts that it
divides evenly). The January library played a stereo voice on one
hardware voice, with the mask's bins in its slots in bin order, and the game
pairs them, left and right, in that order, with paired volumes: Dolby
Digital's stereo voices are front left and right at full volume, then back
left and right and effect sends 0 and 1 at half (sound_dsound_xbox.c,
dsound_initialize_channel). So a mask's bins are dealt to the channels in
turn (the first to the left, the next to the right, and so on), and the list
is made of each channel's in turn. Taken in plain bin order instead, the
left channel had both fronts and the back left, and the right channel only
the back right and the sends.

The same words for debug.txt: the bins (FL, XTLK-BR, I3DL2, FX0 and so on,
the later SDK's numbering, port/include/xdk/xdk_dsound.h) and the
dashboard's speaker setting (XC_AUDIO_FLAGS: stereo 0, mono 1, surround 2 in
the low word; Dolby Digital 0x10000 and DTS 0x20000 in the high).
*/

#include <stdio.h>
#include <string.h>

#include "../include/xbox_mix_bins.h"

int snprintf(char *buffer, size_t size, const char *format, ...);

unsigned int xbox_mix_bins_from_mask(
	unsigned long mask,
	const long *volumes,
	unsigned int channels,
	unsigned long *bins,
	long *bin_volumes)
{
	unsigned long mask_bins[XBOX_MIX_BINS_MAXIMUM];
	long mask_volumes[XBOX_MIX_BINS_MAXIMUM];
	unsigned int count= 0;
	unsigned int channel, index, out;
	unsigned long bin;

	for (bin= 0; bin<XBOX_MIX_BINS_MAXIMUM; bin++)
	{
		if (mask & (1UL<<bin))
		{
			mask_bins[count]= bin;
			mask_volumes[count]= volumes ? volumes[count] : 0;
			count++;
		}
	}
	/* (one channel, or bins that don't share out evenly, which the library
	would refuse anyway: as they are) */
	if (channels<2 || count%channels)
	{
		channels= 1;
	}
	out= 0;
	for (channel= 0; channel<channels; channel++)
	{
		for (index= channel; index<count; index+= channels)
		{
			bins[out]= mask_bins[index];
			bin_volumes[out]= mask_volumes[index];
			out++;
		}
	}

	return count;
}

static const char *bin_name(
	unsigned long bin,
	char *buffer,
	unsigned long size)
{
	static const char *const names[]=
	{
		"FL", "FR", "C", "LFE", "BL", "BR",
		"XTLK-FL", "XTLK-FR", "XTLK-BL", "XTLK-BR", "I3DL2"
	};

	if (bin<sizeof(names)/sizeof(names[0]))
	{
		return names[bin];
	}
	if (bin<31)
	{
		snprintf(buffer, (size_t)size, "FX%lu", bin-11);
		return buffer;
	}

	return "SUBMIX";
}

void xbox_mix_bins_names(
	const unsigned long *bins,
	unsigned int count,
	char *text,
	unsigned long size)
{
	unsigned long length= 0;
	unsigned int index;

	if (!size)
	{
		return;
	}
	text[0]= '\0';
	for (index= 0; index<count && length+1<size; index++)
	{
		char buffer[8];
		const char *name= bin_name(bins[index], buffer, sizeof(buffer));
		int written= snprintf(text+length, (size_t)(size-length), "%s%s", index ? " " : "", name);

		if (written<0)
		{
			break;
		}
		length+= (unsigned long)written;
		if (length>=size)
		{
			length= size-1;
		}
	}
	text[length]= '\0';
}

void xbox_mix_bins_describe(
	const unsigned long *bins,
	unsigned int count,
	unsigned int channels,
	char *text,
	unsigned long size)
{
	char left[96], right[96];
	unsigned int half;

	if (!size)
	{
		return;
	}
	if (channels!=2 || count<2 || count%2)
	{
		xbox_mix_bins_names(bins, count, text, size);
		return;
	}
	half= count/2;
	xbox_mix_bins_names(bins, half, left, sizeof(left));
	xbox_mix_bins_names(bins+half, half, right, sizeof(right));
	snprintf(text, (size_t)size, "left %s, right %s", left, right);
}

void xbox_speaker_config_describe(
	unsigned long config,
	char *text,
	unsigned long size)
{
	const char *basic;

	switch (config & 0xFFFFUL)
	{
	case 0: basic= "stereo"; break;
	case 1: basic= "mono"; break;
	case 2: basic= "surround"; break;
	default: basic= "unknown"; break;
	}
	snprintf(text, (size_t)size, "%s%s%s", basic,
		(config & 0x10000UL) ? ", Dolby Digital" : "",
		(config & 0x20000UL) ? ", DTS" : "");
}
