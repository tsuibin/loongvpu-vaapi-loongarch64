/*
 * workbuf_test.c - verify VC8000D H.264 work-buffer layout and content:
 * CABAC init @0, POC table @3680, scaling lists @3816.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "h264_decode_params.h"
#include "work_buffer.h"

/* Work-buffer u32 words are stored as native-endian uint32. The SDK packs
 * each word value explicitly (scaling lists big-endian within the word),
 * then writes the u32 to memory in native order. */
static uint32_t wb32(const uint8_t *p) {
	uint32_t v;
	memcpy(&v, p, sizeof(v));
	return v;
}

int main(void)
{
	uint8_t work[H264_WB_MIN_SIZE];
	struct h264_decode_params prm;
	uint8_t *p;
	int32_t *poc;
	int i;

	memset(work, 0, sizeof(work));
	memset(&prm, 0, sizeof(prm));

	/* DPB POC pairs */
	for (i = 0; i < 16; i++) {
		prm.dpb[i].poc_top    = 2 * i;
		prm.dpb[i].poc_bottom = 2 * i + 1;
	}
	prm.curr_poc_top    = 100;
	prm.curr_poc_bottom = 102;
	prm.field_pic_flag = 0;
	prm.mb_adaptive_frame_field_flag = 0;

	/* Scaling lists: 4x4 list 0 = 0,1,2,3..., 8x8 list 0 = 0xff */
	prm.scaling_list_present = 1;
	for (i = 0; i < 16; i++) prm.scaling_list4x4[0][i] = (uint8_t)i;
	for (i = 0; i < 64; i++) prm.scaling_list8x8[0][i] = 0xff;

	h264_fill_work_buffer(work, &prm);

	/* CABAC: copied as a native-endian u32 array (SDK uses plain memcpy).
	 * First entry from the standard table is 0x14f10236. */
	{
		uint32_t cabac0;
		memcpy(&cabac0, work, sizeof(cabac0));
		if (cabac0 != 0x14f10236u) {
			fprintf(stderr, "FAIL: CABAC init first value = %08x\n", cabac0);
			return 1;
		}
	}

	/* POC table: 16 DPB pairs then current POC */
	poc = (int32_t *)(work + H264_WB_POC_OFFSET);
	if (poc[0] != 0 || poc[1] != 1 || poc[30] != 30 || poc[31] != 31) {
		fprintf(stderr, "FAIL: POC DPB pairs\n");
		return 1;
	}
	if (poc[32] != 100 || poc[33] != 0) {
		fprintf(stderr, "FAIL: POC current = %d,%d\n", poc[32], poc[33]);
		return 1;
	}

	/* Scaling list: first 4x4 list first u32 = 0x00010203 */
	p = work + H264_WB_SCALE_OFFSET;
	if (wb32(p) != 0x00010203u) {
		fprintf(stderr, "FAIL: scaling 4x4 first u32 = %08x\n", wb32(p));
		return 1;
	}
	/* 8x8 list 0 starts after 6*4=24 u32; first u32 all 0xff */
	p = work + H264_WB_SCALE_OFFSET + 24 * 4;
	if (wb32(p) != 0xffffffffu) {
		fprintf(stderr, "FAIL: scaling 8x8 first u32 = %08x\n", wb32(p));
		return 1;
	}

	printf("workbuf_test PASSED (CABAC @0, POC @%u, scaling @%u)\n",
	       H264_WB_POC_OFFSET, H264_WB_SCALE_OFFSET);
	return 0;
}
