/*
 * cmd_test.c - unit tests for VCMD command builders and H.264 register
 * programming (no hardware needed).
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "loongvpu_abi.h"
#include "vcmd.h"
#include "h264_regs.h"
#include "h264_decode_params.h"

static int test_opcodes(void)
{
	uint32_t buf[64], *p = buf;

	vcmd_cmd_wreg(&p, 0x0c, 0xdeadbeef);
	if (buf[0] != (OPCODE_WREG | (1u << 16) | 0x0c) || buf[1] != 0xdeadbeef)
		return 1;

	p = buf; vcmd_cmd_end(&p);
	if (buf[0] != OPCODE_END)
		return 1;

	p = buf; vcmd_cmd_jmp(&p, 0x1234);
	if (buf[0] != (OPCODE_JMP | JMP_IE_1) || buf[3] != 0x1234)
		return 1;
	return 0;
}

static int test_field_masking(void)
{
	unsigned int shadow[H264_REG_SHADOW_WORDS] = {0};
	/* 9-bit field at lsb 23: write 0x1ff -> bits 23..31 set */
	h264_set_field(shadow, F_PIC_MB_WIDTH, 0x1ff);
	if (shadow[4] != (0x1ffu << 23))
		return 1;
	/* Overwrite with smaller value keeps other fields in same reg intact */
	h264_set_field(shadow, F_REF_FRAMES, 3); /* swreg 4, lsb 0, 5 bits */
	if ((shadow[4] & 0x1f) != 3)
		return 1;
	if ((shadow[4] >> 23) != 0x1ff)
		return 1;
	return 0;
}

static int test_h264_stream(void)
{
	uint32_t cmd[128];
	struct h264_decode_params prm;
	int words, i, found_out_base = 0, found_dec_e = 0;

	memset(&prm, 0, sizeof(prm));
	prm.pic_width_in_mbs_minus1 = 119;  /* 1920/16 - 1 */
	prm.pic_height_in_mbs_minus1 = 67;  /* 1088/16 - 1 */
	prm.num_ref_frames = 4;
	prm.pic_init_qp_minus26 = 0;
	prm.slice_data_size = 4096;
	prm.slice_data_bit_offset = 16;
	prm.log2_max_frame_num_minus4 = 0;   /* -> FRAMENUM_LEN = 4 */
	prm.frame_num = 7;
	prm.entropy_coding_mode_flag = 1;
	prm.direct_8x8_inference_flag = 1;
	prm.output_mmu_addr = 0x00000000deadbeefULL;

	words = vcmd_h264_build_decode_cmd(cmd, 128, &prm);
	if (words < 10)
		return 1;

	/* command stream must be WREG pairs terminated by END */
	for (i = 0; i < words; ) {
		uint32_t op = cmd[i] & 0xf8000000u;
		if (op == OPCODE_END) { i++; break; }
		if (op != OPCODE_WREG) return 1;
		i += 2; /* WREG opcode word + value word */
	}
	if (i != words)
		return 1;

	/* verify key register values are present as (offset,value) pairs */
	for (i = 0; i + 1 < words; i += 2) {
		uint32_t off = cmd[i] & 0xffffu;       /* byte offset */
		uint32_t val = cmd[i + 1];
		if (off == (uint32_t)h264_fields[F_PIC_MB_WIDTH].swreg * 4) {
			/* width=120<<23 | height=68<<11 | refs=4 */
			if (((val >> 23) & 0x1ff) != 120) return 1;
			if (((val >> 11) & 0xff) != 68) return 1;
			if ((val & 0x1f) != 4) return 1;
		}
		if (off == (uint32_t)h264_fields[F_DEC_OUT_BASE_LSB].swreg * 4) {
			if (val != 0xdeadbeefu) return 1;
			found_out_base = 1;
		}
		if (off == (uint32_t)h264_fields[F_DEC_E].swreg * 4) {
			/* swreg1 holds DEC_E(bit0) + DEC_IRQ_DIS(bit4) = 0x11 */
			if ((val & 0x11) != 0x11) return 1;
			found_dec_e = 1;
		}
	}
	if (!found_out_base || !found_dec_e)
		return 1;
	return 0;
}

int main(void)
{
	if (test_opcodes())   { fprintf(stderr, "FAIL: opcodes\n"); return 1; }
	if (test_field_masking()) { fprintf(stderr, "FAIL: field masking\n"); return 1; }
	if (test_h264_stream()) { fprintf(stderr, "FAIL: h264 stream\n"); return 1; }
	printf("cmd_test PASSED (opcodes + field masking + H.264 register stream)\n");
	return 0;
}
