/*
 * work_buffer.c - Populate the VC8000D H.264 decoder work buffer.
 *
 * Work-buffer memory layout (from the open VC8000D SDK h264hwd_asic.c):
 *   offset 0    : CABAC init table  - 920 u32 = 3680 B (ASIC_CABAC_INIT_BUFFER_SIZE)
 *   offset 3680 : POC table         - 34 i32 = 136 B (ASIC_POC_BUFFER_SIZE)
 *                   [0..31] = 16 DPB frames x {top,bottom} POC (i/2, i&1)
 *                   [32]    = current POC (or pic_order_cnt[0])
 *                   [33]    = 0 (or pic_order_cnt[1]) when MBAFF
 *   offset 3816 : scaling lists     - 56 u32 = 224 B (ASIC_SCALING_LIST_SIZE)
 *                   6 x 4x4 lists (4 u32 each) + 2 x 8x8 lists (16 u32 each)
 *
 * These mirror H264PrepareCabacInitTables(), h264PreparePOC() and
 * h264PrepareScaleList() in the SDK.
 */

#include <stdint.h>
#include <string.h>

#include "h264_decode_params.h"
#include "work_buffer.h"
#include "h264_cabac_init.h"

#define CABAC_OFFSET 3680u
#define POC_OFFSET   3680u
#define POC_BYTES    136u
#define SCALE_OFFSET 3816u

static inline int32_t min_i32(int32_t a, int32_t b) { return a < b ? a : b; }

void h264_fill_work_buffer(void *work, const struct h264_decode_params *prm)
{
	uint8_t *base = (uint8_t *)work;
	int32_t *poc;
	uint32_t *scale;
	unsigned int i, j;
	int32_t curr_poc;

	if (!work || !prm)
		return;

	/* --- CABAC init table at offset 0 --- */
	memcpy(base, h264_cabac_init_values, sizeof(h264_cabac_init_values));

	/* --- POC table at offset 3680 --- */
	poc = (int32_t *)(base + POC_OFFSET);
	for (i = 0; i < 16; i++) {
		poc[2 * i]     = prm->dpb[i].poc_top;
		poc[2 * i + 1] = prm->dpb[i].poc_bottom;
	}

	if (prm->field_pic_flag)
		curr_poc = prm->curr_poc_top;  /* bottom_field_flag selects top/bottom */
	else
		curr_poc = min_i32(prm->curr_poc_top, prm->curr_poc_bottom);

	if (prm->field_pic_flag || !prm->mb_adaptive_frame_field_flag) {
		poc[32] = curr_poc;
		poc[33] = 0;
	} else {
		poc[32] = prm->curr_poc_top;
		poc[33] = prm->curr_poc_bottom;
	}

	/* --- Scaling lists at offset 3816 (only if present) --- */
	if (prm->scaling_list_present) {
		scale = (uint32_t *)(base + SCALE_OFFSET);
		/* 6 x 4x4 lists: pack 16 bytes each into 4 big-endian u32 */
		for (i = 0; i < 6; i++) {
			const uint8_t *l = prm->scaling_list4x4[i];
			for (j = 0; j < 4; j++)
				*scale++ = ((uint32_t)l[4*j]     << 24) |
				           ((uint32_t)l[4*j+1] << 16) |
				           ((uint32_t)l[4*j+2] << 8)  |
				           ((uint32_t)l[4*j+3]);
		}
		/* 2 x 8x8 lists: pack 64 bytes each into 16 big-endian u32 */
		for (i = 0; i < 2; i++) {
			const uint8_t *l = prm->scaling_list8x8[i];
			for (j = 0; j < 16; j++)
				*scale++ = ((uint32_t)l[4*j]     << 24) |
				           ((uint32_t)l[4*j+1] << 16) |
				           ((uint32_t)l[4*j+2] << 8)  |
				           ((uint32_t)l[4*j+3]);
		}
	}

	(void)POC_BYTES;
}
