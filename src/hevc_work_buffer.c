/*
 * hevc_work_buffer.c - Fill the VC8000D HEVC scaling-list work buffer.
 *
 * Packing mirrors hevc_asic.c (HevcRunAsic) in the open VC8000D SDK:
 *   byte 0..7   : 8 DC values = 6x 16x16 DC then 2x 32x32 DC
 *                 (SDK stores them in scaling_list[0][0][16..23])
 *   byte 8..15  : padding to a 128-bit boundary
 *   byte 16..111: 6x 4x4 lists (16 bytes each), column-by-column:
 *                   for k in 0..3: for j in 0..3: list[4*k + j]
 *   byte 112..495: 6x 8x8 lists (64 bytes), column-by-column:
 *                   for k in 0..7: for j in 0..7: list[8*k + j]
 *   byte 496..879: 6x 16x16 lists (64 bytes), same column order
 *   byte 880..1007: 2x 32x32 lists (64 bytes), same column order
 * Total 1008 bytes; buffer is 1024 (ASIC_SCALING_LIST_SIZE = 16*64).
 */

#include <stdint.h>
#include <string.h>

#include "hevc_decode_params.h"
#include "hevc_work_buffer.h"

void hevc_fill_scaling_list(uint8_t *dst, const struct hevc_decode_params *prm)
{
	int i, j, k;
	uint8_t *p = dst;

	if (!dst || !prm)
		return;

	memset(dst, 0, HEVC_WB_SCALE_BYTES);

	/* DC coefficients: 6x 16x16 then 2x 32x32 */
	for (i = 0; i < 6; i++) *p++ = prm->scaling_list_dc16[i];
	for (i = 0; i < 2; i++) *p++ = prm->scaling_list_dc32[i];
	p += 8; /* pad to 128-bit boundary (offset 16) */

	/* 4x4 lists, 6 of them, column-by-column */
	for (i = 0; i < 6; i++)
		for (j = 0; j < 4; j++)
			for (k = 0; k < 4; k++)
				*p++ = prm->scaling_list4x4[i][4 * k + j];

	/* 8x8 lists, 6 of them */
	for (i = 0; i < 6; i++)
		for (j = 0; j < 8; j++)
			for (k = 0; k < 8; k++)
				*p++ = prm->scaling_list8x8[i][8 * k + j];

	/* 16x16 lists, 6 of them */
	for (i = 0; i < 6; i++)
		for (j = 0; j < 8; j++)
			for (k = 0; k < 8; k++)
				*p++ = prm->scaling_list16x16[i][8 * k + j];

	/* 32x32 lists, 2 of them */
	for (i = 0; i < 2; i++)
		for (j = 0; j < 8; j++)
			for (k = 0; k < 8; k++)
				*p++ = prm->scaling_list32x32[i][8 * k + j];
}
