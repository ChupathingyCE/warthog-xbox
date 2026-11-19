/*
XBOX_MIX_BINS.H

A January mix bin mask as the later DirectSound's list of bins, per channel,
and the words debug.txt says them in (port/xbox/src/xbox_mix_bins.c). No SDK
header: port/xbox/tests/run.sh builds it on the host.
*/

#ifndef XBOX_MIX_BINS_H
#define XBOX_MIX_BINS_H

#define XBOX_MIX_BINS_MAXIMUM 32

/* The bins of a January mask for a voice of that many channels, in the order
the later library takes them, and their volumes (the January volumes, given
in bin order, or 0 for full volume when there are none). Returns the count;
0 for an empty mask. */
unsigned int xbox_mix_bins_from_mask(
	unsigned long mask,
	const long *volumes,
	unsigned int channels,
	unsigned long *bins,
	long *bin_volumes);

/* "FL FR" for the bins of a list (from xbox_mix_bins_from_mask), or of one
channel's part of it; cut to the buffer. */
void xbox_mix_bins_names(
	const unsigned long *bins,
	unsigned int count,
	char *text,
	unsigned long size);

/* The voice's bins said per channel: "FL FR" for a mono voice, "left FL BL,
right FR BR" for a stereo one; cut to the buffer. */
void xbox_mix_bins_describe(
	const unsigned long *bins,
	unsigned int count,
	unsigned int channels,
	char *text,
	unsigned long size);

/* The dashboard's audio setting (XGetAudioFlags, which the library's
GetSpeakerConfig reports) in words: "stereo", "mono, Dolby Digital". */
void xbox_speaker_config_describe(
	unsigned long config,
	char *text,
	unsigned long size);

#endif
