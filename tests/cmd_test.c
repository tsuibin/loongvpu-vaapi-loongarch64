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

static int test_dpb(void)
{
	unsigned int sh[H264_REG_SHADOW_WORDS] = {0};
	int i, reg;

	/* ref 3 base: lsb swreg 67+6=73, msb 72 */
	h264_set_ref_base(sh, 3, 0x0000000112345678ULL);
	if (sh[73] != 0x12345678u || sh[72] != 1u) return 1;

	/* ref 3 frame_num -> swreg 30+1=31, lsb 16 */
	h264_set_ref_nbr(sh, 3, 0xabcd);
	if (((sh[31] >> 16) & 0xffff) != 0xabcd) return 1;

	/* valid/lterm bit order is 31-n */
	h264_set_ref_valid(sh, 1u << (31 - 0));
	if (sh[39] != (1u << 31)) return 1;
	h264_set_ref_longterm(sh, 1u << (31 - 2));
	if (sh[38] != (1u << 29)) return 1;

	/* INIT_RLIST_F0 = swreg14 lsb0, F3 = swreg15 lsb0, B0 = swreg14 lsb5 */
	h264_set_ref_list(sh, RLIST_PF, 0, 3);
	if ((sh[14] & 0x1f) != 3) return 1;
	h264_set_ref_list(sh, RLIST_PF, 3, 7);
	if ((sh[15] & 0x1f) != 7) return 1;
	h264_set_ref_list(sh, RLIST_PB, 0, 2);
	if (((sh[14] >> 5) & 0x1f) != 2) return 1;

	/* REF0 POC0 = swreg51 lsb20 */
	h264_set_ref0_poc(sh, 0, 0x2aa);
	if (((sh[51] >> 20) & 0x3ff) != 0x2aa) return 1;

	/* base + flags share swreg 67: ref0 base set then flags must preserve it */
	{
		unsigned int s2[H264_REG_SHADOW_WORDS] = {0};
		h264_set_ref_base(s2, 0, 0xdeadbeefULL);
		h264_set_ref0_flags(s2, 1, 1);
		if ((s2[67] & 0xfffffff0u) != (0xdeadbeefu & 0xfffffff0u)) return 1;
		if ((s2[67] & 0x3) != 0x3) return 1;
	}

	(void)i; (void)reg;
	return 0;
}

static int test_h264_dpb_stream(void)
{
	uint32_t cmd[256];
	struct h264_decode_params prm;
	int words, i, found_ref_base = 0, found_valid = 0, found_rlist = 0;

	memset(&prm, 0, sizeof(prm));
	prm.pic_width_in_mbs_minus1 = 79;
	prm.pic_height_in_mbs_minus1 = 44;
	prm.slice_type = 0; /* P slice */
	prm.dpb[0].valid = 1;
	prm.dpb[0].luma_mmu_addr = 0x12345000;
	prm.dpb[0].frame_num = 5;
	prm.dpb[1].valid = 1;
	prm.dpb[1].long_term = 1;
	prm.dpb[1].luma_mmu_addr = 0x67899000;
	prm.refs.list0[0] = 1;
	prm.refs.list0[1] = 0;
	prm.refs.list0_count = 2;
	prm.output_mmu_addr = 0xabcd0000;

	words = vcmd_h264_build_decode_cmd(cmd, 256, &prm);
	if (words < 20) return 1;

	for (i = 0; i + 1 < words; i += 2) {
		uint32_t off = cmd[i] & 0xffffu;
		if (off == 67 * 4) found_ref_base = 1; /* REFER0_BASE_LSB */
		if (off == 39 * 4) found_valid = 1;    /* REFER_VALID_E */
		if (off == 14 * 4) found_rlist = 1;    /* INIT_RLIST_F0.. */
	}
	if (!found_ref_base || !found_valid || !found_rlist) return 1;
	return 0;
}

int main(void)
{
	if (test_opcodes())   { fprintf(stderr, "FAIL: opcodes\n"); return 1; }
	if (test_field_masking()) { fprintf(stderr, "FAIL: field masking\n"); return 1; }
	if (test_h264_stream()) { fprintf(stderr, "FAIL: h264 stream\n"); return 1; }
	if (test_dpb()) { fprintf(stderr, "FAIL: dpb registers\n"); return 1; }
	if (test_h264_dpb_stream()) { fprintf(stderr, "FAIL: h264 dpb stream\n"); return 1; }
	printf("cmd_test PASSED (opcodes + masking + H.264 stream + DPB registers + DPB stream)\n");
	return 0;
}
