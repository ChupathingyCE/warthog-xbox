/*
MIX_BINS_TEST.C

The January masks as the later library's lists (port/xbox/src/
xbox_mix_bins.c) on the host: a mono voice's bins in bin order, a stereo
voice's dealt left and right in turn, with their volumes, and the words
debug.txt says them and the speaker setting in. port/xbox/tests/run.sh.
*/

#include <stdio.h>
#include <string.h>

#include "../include/xbox_mix_bins.h"

static int failures;

#define CHECK(condition) do { if (!(condition)) { failures++; fprintf(stderr, "mix_bins_test: line %d: %s\n", __LINE__, #condition); } } while (0)

/* the game's masks (port/include/xdk/xdk_dsound.h) */
#define FL 0x1UL
#define FR 0x2UL
#define C 0x4UL
#define BL 0x10UL
#define BR 0x20UL
#define XTLK_BL 0x100UL
#define XTLK_BR 0x200UL
#define I3DL2 0x400UL
#define FX0 0x800UL
#define FX1 0x1000UL

int main(void)
{
	unsigned long bins[XBOX_MIX_BINS_MAXIMUM];
	long volumes[XBOX_MIX_BINS_MAXIMUM];
	char text[160];
	unsigned int count;

	/* a mono 2D voice: front left and right at -6 dB */
	{
		static const long given[2] = { -602, -602 };

		count = xbox_mix_bins_from_mask(FL|FR, given, 1, bins, volumes);
		CHECK(count == 2 && bins[0] == 0 && bins[1] == 1 && volumes[0] == -602 && volumes[1] == -602);
		xbox_mix_bins_describe(bins, count, 1, text, sizeof(text));
		CHECK(!strcmp(text, "FL FR"));
	}
	/* a stereo voice of two bins: left front left, right front right */
	count = xbox_mix_bins_from_mask(FL|FR, NULL, 2, bins, volumes);
	CHECK(count == 2 && bins[0] == 0 && bins[1] == 1 && volumes[0] == 0 && volumes[1] == 0);
	xbox_mix_bins_describe(bins, count, 2, text, sizeof(text));
	CHECK(!strcmp(text, "left FL, right FR"));
	/* Dolby Digital's stereo voice: the pairs dealt out, each with its volume */
	{
		static const long given[6] = { 0, -1, -602, -603, -604, -605 };

		count = xbox_mix_bins_from_mask(FL|FR|BL|BR|FX0|FX1, given, 2, bins, volumes);
		CHECK(count == 6);
		CHECK(bins[0] == 0 && bins[1] == 4 && bins[2] == 11);
		CHECK(bins[3] == 1 && bins[4] == 5 && bins[5] == 12);
		CHECK(volumes[0] == 0 && volumes[1] == -602 && volumes[2] == -604);
		CHECK(volumes[3] == -1 && volumes[4] == -603 && volumes[5] == -605);
		xbox_mix_bins_describe(bins, count, 2, text, sizeof(text));
		CHECK(!strcmp(text, "left FL BL FX0, right FR BR FX1"));
	}
	/* the mono voice of Dolby Digital, and the rear speakers' silent one */
	count = xbox_mix_bins_from_mask(FL|FR|C, NULL, 1, bins, volumes);
	xbox_mix_bins_describe(bins, count, 1, text, sizeof(text));
	CHECK(count == 3 && !strcmp(text, "FL FR C"));
	count = xbox_mix_bins_from_mask(XTLK_BL|XTLK_BR|I3DL2|FX0|FX1, NULL, 1, bins, volumes);
	xbox_mix_bins_describe(bins, count, 1, text, sizeof(text));
	CHECK(count == 5 && !strcmp(text, "XTLK-BL XTLK-BR I3DL2 FX0 FX1"));
	/* bins that don't share out evenly stay in bin order */
	count = xbox_mix_bins_from_mask(FL|FR|C, NULL, 2, bins, volumes);
	CHECK(count == 3 && bins[0] == 0 && bins[1] == 1 && bins[2] == 2);
	/* nothing, every bin, and a cut buffer */
	CHECK(xbox_mix_bins_from_mask(0, NULL, 2, bins, volumes) == 0);
	count = xbox_mix_bins_from_mask(0xFFFFFFFFUL, NULL, 2, bins, volumes);
	CHECK(count == 32 && bins[0] == 0 && bins[15] == 30 && bins[16] == 1 && bins[31] == 31);
	xbox_mix_bins_names(bins + 13, 3, text, sizeof(text));
	CHECK(!strcmp(text, "FX15 FX17 FX19"));
	xbox_mix_bins_names(bins, count, text, 8);
	CHECK(strlen(text) == 7);
	/* the dashboard's setting */
	xbox_speaker_config_describe(0x00000000UL, text, sizeof(text));
	CHECK(!strcmp(text, "stereo"));
	xbox_speaker_config_describe(0x00000001UL, text, sizeof(text));
	CHECK(!strcmp(text, "mono"));
	xbox_speaker_config_describe(0x00010002UL, text, sizeof(text));
	CHECK(!strcmp(text, "surround, Dolby Digital"));
	xbox_speaker_config_describe(0x00030000UL, text, sizeof(text));
	CHECK(!strcmp(text, "stereo, Dolby Digital, DTS"));

	if (failures)
	{
		fprintf(stderr, "mix_bins_test: %d failed\n", failures);
		return 1;
	}
	printf("mix_bins_test: passed\n");
	return 0;
}
