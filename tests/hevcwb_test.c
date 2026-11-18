/*
 * hevcwb_test.c - verify the VC8000D HEVC scaling-list work-buffer layout:
 * 8 DC bytes @0, 8 pad, 6x4x4 @16, 6x8x8 @112, 6x16x16 @496, 2x32x32 @880.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "hevc_decode_params.h"
#include "hevc_work_buffer.h"

int main(void)
{
	uint8_t buf[HEVC_WB_SCALE_BYTES];
	struct hevc_decode_params prm;
	int i, ok = 1;

	memset(&prm, 0, sizeof(prm));
	for (i = 0; i < 6; i++) {
		prm.scaling_list_dc16[i] = (uint8_t)(0x10 + i);
		prm.scaling_list_dc32[i % 2] = (uint8_t)(0x20 + i % 2);
		prm.scaling_list4x4[i][0] = (uint8_t)(0x30 + i);
		prm.scaling_list8x8[i][0] = (uint8_t)(0x40 + i);
		prm.scaling_list16x16[i][0] = (uint8_t)(0x50 + i);
	}

	hevc_fill_scaling_list(buf, &prm);

	/* DC values: 6x 16x16 then 2x 32x32 */
	for (i = 0; i < 6; i++)
		if (buf[i] != (uint8_t)(0x10 + i)) { fprintf(stderr, "FAIL dc16[%d]\n", i); ok = 0; }
	for (i = 0; i < 2; i++)
		if (buf[6 + i] != (uint8_t)(0x20 + i)) { fprintf(stderr, "FAIL dc32[%d]\n", i); ok = 0; }

	/* padding bytes 8..15 must be zero */
	for (i = 8; i < 16; i++)
		if (buf[i] != 0) { fprintf(stderr, "FAIL pad[%d]\n", i); ok = 0; }

	/* 4x4 lists start at byte 16: column-by-column, list[i][4*k+j].
	 * For k=0,j=0 -> list[i][0], so first byte of each 16-byte block is 0x30+i. */
	for (i = 0; i < 6; i++)
		if (buf[16 + 16 * i] != (uint8_t)(0x30 + i)) { fprintf(stderr, "FAIL 4x4[%d]\n", i); ok = 0; }

	/* 8x8 lists start at byte 112 (16 + 6*16). First byte of each 64-byte
	 * block is list[i][0] = 0x40+i (k=0,j=0). */
	for (i = 0; i < 6; i++)
		if (buf[112 + 64 * i] != (uint8_t)(0x40 + i)) { fprintf(stderr, "FAIL 8x8[%d]\n", i); ok = 0; }

	/* 16x16 lists start at byte 496 (112 + 6*64) */
	for (i = 0; i < 6; i++)
		if (buf[496 + 64 * i] != (uint8_t)(0x50 + i)) { fprintf(stderr, "FAIL 16x16[%d]\n", i); ok = 0; }

	if (!ok) return 1;
	printf("hevcwb_test PASSED (DC@0, 4x4@16, 8x8@112, 16x16@496, 32x32@880)\n");
	return 0;
}
