/*
XBOX_DSOUND.C

The game's DirectSound calls, as the January 2002 SDK declared them, on a
later SDK's DirectSound library.

The January SDK named a voice's mix bins with a mask, one bit per bin
(DSBUFFERDESC and DSSTREAMDESC's dwMixBinMask, SetMixBins(mask),
SetMixBinVolumes(mask, volumes), SetMixBinHeadroom(mask, headroom)); later
SDKs take a DSMIXBINS list of bin and volume pairs. Bit N of a January mask
is bin N of the later list (port/include/xdk/xdk_dsound.h). The game's calls
to these are renamed to the halo_xbox_ functions here by
port/xbox/include/halo_xbox_prefix.h; everything else goes to the library
directly, its declarations being the same in both SDKs.

A voice of more than one channel takes its list in per-channel runs in the
later library (the first half of a stereo voice's bins are its left
channel's), where the January one paired them in bin order; the voices'
channels are kept here so their masks are dealt out per channel
(port/xbox/src/xbox_mix_bins.c). Each new layout a 2D voice gets is said once
in debug.txt, and the speaker setting and the game's layouts for it at the
start (sound: speakers ...), so a report's log shows where 2D sounds went.

DirectSoundStopStream and DirectSoundGetStreamVoiceStatus came from a
stream helper linked with the January game (dsstrmh), not the library.

This file is compiled with the later SDK's headers, not the game's.
*/

#include <xtl.h>

#include "../include/xbox_mix_bins.h"

void platform_log(const char *format, ...);

/* ---------- the January SDK's structures */

struct january_buffer_description
{
	DWORD size;
	DWORD flags;
	DWORD buffer_bytes;
	LPWAVEFORMATEX format;
	DWORD mix_bin_mask;
	DWORD input_mix_bin_mask;
};

struct january_stream_description
{
	DWORD flags;
	DWORD maximum_attached_packets;
	LPWAVEFORMATEX format;
	LPFNXMEDIAOBJECTCALLBACK callback;
	LPVOID context;
	DWORD mix_bin_mask;
};

/* ---------- mix bins */

#define MAXIMUM_MIX_BINS XBOX_MIX_BINS_MAXIMUM

struct mix_bins
{
	DSMIXBINS bins;
	DSMIXBINVOLUMEPAIR pairs[MAXIMUM_MIX_BINS];
};

/* The bins of a January mask for a voice of that many channels, with the
volumes given in bin order (or full volume). */
static LPCDSMIXBINS mix_bins_from_mask(
	struct mix_bins *out,
	DWORD mask,
	const LONG *volumes,
	unsigned int channels)
{
	unsigned long bins[MAXIMUM_MIX_BINS];
	long bin_volumes[MAXIMUM_MIX_BINS];
	unsigned int count, index;

	count= xbox_mix_bins_from_mask(mask, volumes, channels, bins, bin_volumes);
	out->bins.dwMixBinCount= count;
	out->bins.lpMixBinVolumePairs= out->pairs;
	for (index= 0; index<count; index++)
	{
		out->pairs[index].dwMixBin= bins[index];
		out->pairs[index].lVolume= volumes ? bin_volumes[index] : DSBVOLUME_MAX;
	}

	return &out->bins;
}

/* ---------- the voices' channels */

/* (the game makes about 90 voices, once, when its sound starts) */
#define MAXIMUM_VOICES 160

static struct
{
	const void *voice;
	unsigned int channels;
} voices[MAXIMUM_VOICES];
static unsigned int next_voice;

static void voice_remember(
	const void *voice,
	const WAVEFORMATEX *format)
{
	unsigned int index;
	unsigned int channels= format ? format->nChannels : 1;

	if (!voice)
	{
		return;
	}
	for (index= 0; index<MAXIMUM_VOICES; index++)
	{
		if (voices[index].voice==voice || !voices[index].voice)
		{
			break;
		}
	}
	if (index==MAXIMUM_VOICES)
	{
		/* (the oldest, the game's voices having been released and made again) */
		index= next_voice;
		next_voice= (next_voice+1)%MAXIMUM_VOICES;
	}
	voices[index].voice= voice;
	voices[index].channels= channels;
}

static unsigned int voice_channels(
	const void *voice)
{
	unsigned int index;

	for (index= 0; index<MAXIMUM_VOICES && voices[index].voice; index++)
	{
		if (voices[index].voice==voice)
		{
			return voices[index].channels;
		}
	}

	return 1;
}

/* Each layout a voice is given, said once: its channels, its bins, and
whether it's a 3D voice (whose bins the library's HRTF sets) or 2D. */
#define MAXIMUM_LAYOUTS_SAID 16

static void layout_say(
	const char *what,
	unsigned int channels,
	LPCDSMIXBINS bins)
{
	static struct
	{
		const char *what;
		unsigned int channels;
		DWORD mask;
	} said[MAXIMUM_LAYOUTS_SAID];
	static unsigned int said_count;
	unsigned long list[MAXIMUM_MIX_BINS];
	char text[160];
	DWORD mask= 0;
	unsigned int index, count= bins ? bins->dwMixBinCount : 0;

	for (index= 0; index<count && index<MAXIMUM_MIX_BINS; index++)
	{
		list[index]= bins->lpMixBinVolumePairs[index].dwMixBin;
		mask|= 1UL<<list[index];
	}
	for (index= 0; index<said_count; index++)
	{
		if (said[index].what==what && said[index].channels==channels && said[index].mask==mask)
		{
			return;
		}
	}
	if (said_count==MAXIMUM_LAYOUTS_SAID)
	{
		return;
	}
	said[said_count].what= what;
	said[said_count].channels= channels;
	said[said_count].mask= mask;
	said_count++;
	if (bins)
	{
		xbox_mix_bins_describe(list, count, channels, text, sizeof(text));
	}
	platform_log("sound: a %s %s voice plays to %s", channels==2 ? "stereo" : channels==1 ? "mono" : "multichannel",
		what, bins ? text : channels==2 ? "left FL, right FR (the library's own)" : "FL FR (the library's own)");
}

/* ---------- buffers and streams */

static void buffer_description_from_january(
	DSBUFFERDESC *description,
	struct mix_bins *bins,
	const struct january_buffer_description *january)
{
	unsigned int channels= january->format ? january->format->nChannels : 1;

	ZeroMemory(description, sizeof(*description));
	description->dwSize= sizeof(*description);
	description->dwFlags= january->flags;
	description->dwBufferBytes= january->buffer_bytes;
	description->lpwfxFormat= january->format;
	description->lpMixBins= january->mix_bin_mask ?
		mix_bins_from_mask(bins, january->mix_bin_mask, NULL, channels) : NULL;
	description->dwInputMixBin= 0;
	if (january->input_mix_bin_mask)
	{
		DWORD bin;

		for (bin= 0; !(january->input_mix_bin_mask & (1UL<<bin)); bin++);
		description->dwInputMixBin= bin;
	}
}

static void buffer_layout_say(
	const DSBUFFERDESC *description)
{
	if (description->lpMixBins && !(description->dwFlags & DSBCAPS_CTRL3D))
	{
		layout_say("2D buffer", description->lpwfxFormat ? description->lpwfxFormat->nChannels : 1,
			description->lpMixBins);
	}
}

HRESULT WINAPI halo_xbox_DirectSoundCreateBuffer(
	const struct january_buffer_description *january,
	LPDIRECTSOUNDBUFFER *buffer)
{
	DSBUFFERDESC description;
	struct mix_bins bins;
	HRESULT result;

	buffer_description_from_january(&description, &bins, january);
	result= DirectSoundCreateBuffer(&description, buffer);
	if (SUCCEEDED(result))
	{
		voice_remember(*buffer, january->format);
		buffer_layout_say(&description);
	}

	return result;
}

HRESULT WINAPI halo_xbox_IDirectSound_CreateSoundBuffer(
	LPDIRECTSOUND direct_sound,
	const struct january_buffer_description *january,
	LPDIRECTSOUNDBUFFER *buffer,
	LPUNKNOWN outer)
{
	DSBUFFERDESC description;
	struct mix_bins bins;
	HRESULT result;

	buffer_description_from_january(&description, &bins, january);
	result= IDirectSound_CreateSoundBuffer(direct_sound, &description, buffer, outer);
	if (SUCCEEDED(result))
	{
		voice_remember(*buffer, january->format);
		buffer_layout_say(&description);
	}

	return result;
}

HRESULT WINAPI halo_xbox_IDirectSound_CreateSoundStream(
	LPDIRECTSOUND direct_sound,
	const struct january_stream_description *january,
	LPDIRECTSOUNDSTREAM *stream,
	LPUNKNOWN outer)
{
	DSSTREAMDESC description;
	struct mix_bins bins;
	unsigned int channels= january->format ? january->format->nChannels : 1;
	HRESULT result;

	ZeroMemory(&description, sizeof(description));
	description.dwFlags= january->flags;
	description.dwMaxAttachedPackets= january->maximum_attached_packets;
	description.lpwfxFormat= january->format;
	description.lpfnCallback= january->callback;
	description.lpvContext= january->context;
	description.lpMixBins= january->mix_bin_mask ?
		mix_bins_from_mask(&bins, january->mix_bin_mask, NULL, channels) : NULL;
	result= IDirectSound_CreateSoundStream(direct_sound, &description, stream, outer);
	if (SUCCEEDED(result))
	{
		voice_remember(*stream, january->format);
		/* (a 2D voice the game gives no bins keeps the library's: front left
		and right; a 3D one's are its HRTF's) */
		if (!description.lpMixBins && !(january->flags & DSSTREAMCAPS_CTRL3D))
		{
			layout_say("2D", channels, NULL);
		}
	}

	return result;
}

HRESULT WINAPI halo_xbox_IDirectSoundStream_SetMixBins(
	LPDIRECTSOUNDSTREAM stream,
	DWORD mask)
{
	struct mix_bins bins;
	unsigned int channels= voice_channels(stream);
	LPCDSMIXBINS list= mix_bins_from_mask(&bins, mask, NULL, channels);

	layout_say("2D", channels, list);
	return IDirectSoundStream_SetMixBins(stream, list);
}

HRESULT WINAPI halo_xbox_IDirectSoundStream_SetMixBinVolumes(
	LPDIRECTSOUNDSTREAM stream,
	DWORD mask,
	const LONG *volumes)
{
	struct mix_bins bins;

	return IDirectSoundStream_SetMixBinVolumes(stream, mix_bins_from_mask(&bins, mask, volumes,
		voice_channels(stream)));
}

HRESULT WINAPI halo_xbox_IDirectSound_SetMixBinHeadroom(
	LPDIRECTSOUND direct_sound,
	DWORD mask,
	DWORD headroom)
{
	HRESULT result= DS_OK;
	DWORD bin;

	for (bin= 0; bin<MAXIMUM_MIX_BINS && SUCCEEDED(result); bin++)
	{
		if (mask & (1UL<<bin))
		{
			result= IDirectSound_SetMixBinHeadroom(direct_sound, bin, headroom);
		}
	}

	return result;
}

/* The game carries the January SDK's effects image (reverb and crosstalk
DSP code). The image is not only reverb: a 3D voice's HRTF writes the
crosstalk mix bins, which only the image's crosstalk effect plays on to the
speakers, so with no image every 3D sound (weapons, grenades, footsteps) is
silent and only 2D sounds are heard (the devkit's capture, 2026-10-21). The
game's own image is downloaded, at the game's effect indices; whether this
library takes it is logged. The game only checks the result, so a refusal
is still answered DS_OK and the game runs on without the image (an error
would stop its sound altogether); but since 3D sound is then silent, the
refusal is said plainly in debug.txt and in a box at the main menu. */
void xbox_boot_warning(const wchar_t *text);

/* The speaker setting and the game's mix bins for it, once, as the game's
sound starts (just before it downloads the effects image): what the
dashboard says (XGetAudioFlags), what the library reports to the game, and
where the game then sends 2D sound (sound_dsound_xbox.c,
dsound_initialize_channel: it looks only at Dolby Digital; mono and
surround are the library's to mix down or encode). */
static void speakers_say(
	LPDIRECTSOUND direct_sound)
{
	DWORD dashboard= XGetAudioFlags();
	DWORD reported= dashboard;
	char dashboard_text[48], reported_text[48];

	if (FAILED(IDirectSound_GetSpeakerConfig(direct_sound, &reported)))
	{
		reported= dashboard;
	}
	xbox_speaker_config_describe(dashboard, dashboard_text, sizeof(dashboard_text));
	xbox_speaker_config_describe(reported, reported_text, sizeof(reported_text));
	platform_log("sound: speakers %s (the dashboard's %08lx; DirectSound reports %s, %08lx)", dashboard_text,
		(unsigned long)dashboard, reported_text, (unsigned long)reported);
	if (reported & DSSPEAKER_ENABLE_AC3)
	{
		platform_log("sound: 2D mono to FL FR C at -6 dB; 2D stereo left to FL BL FX0, right to FR BR FX1 "
			"(fronts full, the rest -6 dB); 3D to XTLK-FL XTLK-BL XTLK-FR XTLK-BR I3DL2");
	}
	else
	{
		platform_log("sound: 2D mono to FL FR at -6 dB; 2D stereo left to FL, right to FR; "
			"3D to XTLK-FL XTLK-BL XTLK-FR XTLK-BR I3DL2");
	}
}

HRESULT WINAPI halo_xbox_IDirectSound_DownloadEffectsImage(
	LPDIRECTSOUND direct_sound,
	LPCVOID image,
	DWORD image_size,
	LPCDSEFFECTIMAGELOC location,
	LPDSEFFECTIMAGEDESC *description)
{
	HRESULT result;

	if (description)
	{
		*description= NULL;
	}
	speakers_say(direct_sound);
	result= IDirectSound_DownloadEffectsImage(direct_sound, image, image_size, (LPDSEFFECTIMAGELOC)location,
		description);
	platform_log("sound: the game's effects image (%lu bytes, reverb %lu, crosstalk %lu) %s (%08lx)",
		(unsigned long)image_size, location ? location->dwI3DL2ReverbIndex : 0UL,
		location ? location->dwCrosstalkIndex : 0UL, SUCCEEDED(result) ? "downloaded" : "refused",
		(unsigned long)result);
	if (FAILED(result))
	{
		platform_log("sound: WARNING: without the effects image, 3D sounds (weapons, grenades, footsteps) are "
			"silent; only 2D sounds play. Please send this debug.txt.");
		xbox_boot_warning(L"Sound: this console refused the game's sound effects, so weapons and footsteps "
			L"may be silent.\r\nPlease send D:\\debug.txt.");
	}

	return SUCCEEDED(result) ? result : DS_OK;
}

/* ---------- the stream helper */

void WINAPI DirectSoundStopStream(
	LPDIRECTSOUNDSTREAM stream)
{
	IDirectSoundStream_FlushEx(stream, 0, DSSTREAMFLUSHEX_ASYNC);
}

DWORD WINAPI DirectSoundGetStreamVoiceStatus(
	LPDIRECTSOUNDSTREAM stream)
{
	DWORD status= 0;

	if (FAILED(IDirectSoundStream_GetStatus(stream, &status)))
	{
		return 0;
	}

	return (status & DSSTREAMSTATUS_PLAYING) ? 1 : 0;
}
