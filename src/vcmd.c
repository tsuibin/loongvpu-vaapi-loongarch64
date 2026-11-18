/*
 * vcmd.c - VCMD command-buffer submission to the LoongVPU VCD core.
 *
 * Submission sequence (all ioctls are on the render node fd):
 *   1. VCMD_IOCH_GET_CMDBUF_PARAMETER  - obtain the command/status buffer pool
 *   2. VCMD_IOCH_RESERVE_CMDBUF        - reserve one command buffer (returns id)
 *   3. fill the command buffer with OPCODE_WREG/END words (see below)
 *   4. VCMD_IOCH_LINK_RUN_CMDBUF       - submit it to hardware
 *   5. VCMD_IOCH_WAIT_CMDBUF           - wait for completion
 *   6. VCMD_IOCH_RELEASE_CMDBUF        - release the buffer slot
 *
 * Command word layout (mirrors VCMD_READ_CMD in the kernel driver):
 *   WREG:  [ OPCODE_WREG | (nwords<<16) | reg_offset ], then value words
 *   END:   [ OPCODE_END ]
 *   JMP:   [ OPCODE_JMP | flags ], then 0, 0, cmdbuf_id
 *
 * !! IMPORTANT !!
 * The concrete H.264 register values that make the VCD core decode a frame
 * (SPS/PPS/slice-header -> register writes) live in the closed-source DWL
 * and the VeriSilicon/Loongson chip manual. They are NOT in the open kernel
 * tree. h264_build_decode_cmd() below is therefore a STRUCTURAL SKELETON:
 * it emits a syntactically valid command stream using the few register
 * definitions we do have (dwl_defs.h), but it will not produce a correct
 * decoded image until the real register sequence is supplied.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "loongvpu_abi.h"
#include "vcmd.h"
#include "h264_regs.h"
#include "h264_decode_params.h"

/* Register map is in h264_regs.h (auto-extracted from VC8000D SDK). */

struct vpu_vcmd {
	int                        drm_fd;
	struct cmdbuf_mem_parameter pool;
	int                        pool_ready;
	u16                        next_cmdbuf_id;
};

/* ---------------- command word builders -------------------------------- */

void vcmd_cmd_wreg(uint32_t **pp, uint32_t offset, uint32_t value)
{
	uint32_t *p = *pp;
	*p++ = OPCODE_WREG | (1u << 16) | (offset & 0xffffu);
	*p++ = value;
	*pp = p;
}

void vcmd_cmd_end(uint32_t **pp)
{
	*(*pp)++ = OPCODE_END;
}

void vcmd_cmd_jmp(uint32_t **pp, u16 cmdbuf_id)
{
	uint32_t *p = *pp;
	*p++ = OPCODE_JMP | JMP_IE_1;
	*p++ = 0;
	*p++ = 0;
	*p++ = cmdbuf_id;
	*pp = p;
}

/* ---------------- pool setup ------------------------------------------- */

struct vpu_vcmd *vpu_vcmd_open(int drm_fd)
{
	struct vpu_vcmd *v;

	v = (struct vpu_vcmd *)calloc(1, sizeof(*v));
	if (!v)
		return NULL;
	v->drm_fd = drm_fd;

	memset(&v->pool, 0, sizeof(v->pool));
	if (ioctl(drm_fd, VCMD_IOCH_GET_CMDBUF_PARAMETER, &v->pool) < 0) {
		fprintf(stderr, "vcmd: GET_CMDBUF_PARAMETER failed: %s\n", strerror(errno));
		free(v);
		return NULL;
	}
	v->pool_ready = 1;
	return v;
}

/* ---------------- H.264 decode command builder ------------------------- */
/*
 * Program the VC8000D H.264 decoder registers from VA-API params and emit
 * them as a VCMD OPCODE_WREG stream.
 *
 * Register map (h264_regs.h) is auto-extracted from the open VC8000D SDK
 * (8170table.h, HW_ID 0x8001 = Loongson LG200 VPU). The field programming
 * order follows H264RunAsic() in the same SDK and the mainline Linux
 * hantro_g1_h264_dec.c reference.
 *
 * NOTE: this covers the core decode configuration. The full SDK also sets
 * DPB reference lists, POC and CABAC/scale-list tables; those need the
 * reconstructed DPB surfaces from the client and are marked TODO below.
 * This is a functional base, not a complete multi-reference decoder yet.
 *
 * Returns number of uint32 words written, or negative on error.
 */
int vcmd_h264_build_decode_cmd(uint32_t *cmd, size_t cmd_words,
                               const struct h264_decode_params *prm)
{
	unsigned int shadow[H264_REG_SHADOW_WORDS] = {0};
	uint32_t *p = cmd;
	uint32_t *end = cmd + cmd_words;
	int i;

	if (!cmd || !prm || cmd_words < 64)
		return -1;

	/* ---- core decoder control (mirrors H264RunAsic / G1 set_params) ---- */
	h264_set_field(shadow, F_DEC_MODE, DEC_X170_MODE_H264);   /* H.264 mode */

	/* Picture dimensions in macroblocks. */
	h264_set_field(shadow, F_PIC_MB_WIDTH,  prm->pic_width_in_mbs_minus1 + 1);
	h264_set_field(shadow, F_PIC_MB_HEIGHT, prm->pic_height_in_mbs_minus1 + 1);
	h264_set_field(shadow, F_REF_FRAMES,    prm->num_ref_frames);

	/* Bitstream start: slice_data_bit_offset (bits into NAL) + stream length. */
	h264_set_field(shadow, F_STRM_START_BIT, prm->slice_data_bit_offset);
	h264_set_field(shadow, F_STREAM_LEN,     prm->slice_data_size);

	/* Quantization. */
	h264_set_field(shadow, F_INIT_QP,       (unsigned int)(prm->pic_init_qp_minus26 + 26));
	h264_set_field(shadow, F_CH_QP_OFFSET,  (unsigned int)(prm->chroma_qp_index_offset & 0x1f));
	h264_set_field(shadow, F_CH_QP_OFFSET2, (unsigned int)(prm->second_chroma_qp_index_offset & 0x1f));

	/* Frame numbering / POC. */
	h264_set_field(shadow, F_FRAMENUM_LEN, prm->log2_max_frame_num_minus4 + 4);
	h264_set_field(shadow, F_FRAMENUM,     prm->frame_num);
	h264_set_field(shadow, F_POC_LENGTH,   prm->log2_max_pic_order_cnt_lsb_minus4 + 4);
	h264_set_field(shadow, F_IDR_PIC_ID,   prm->idr_pic_id);
	h264_set_field(shadow, F_REFPIC_MK_LEN, prm->dec_ref_pic_marking_bit_size);

	/* Flags from SPS/PPS/slice. */
	h264_set_field(shadow, F_CABAC_E,           prm->entropy_coding_mode_flag);
	h264_set_field(shadow, F_DIR_8X8_INFER_E,   prm->direct_8x8_inference_flag);
	h264_set_field(shadow, F_PIC_INTERLACE_E,   !prm->frame_mbs_only_flag &&
	                                            prm->mb_adaptive_frame_field_flag);
	h264_set_field(shadow, F_PIC_FIELDMODE_E,   prm->field_pic_flag);
	h264_set_field(shadow, F_WRITE_MVS_E,       prm->reference_pic_flag);
	h264_set_field(shadow, F_FILTERING_DIS,     !prm->constrained_intra_pred_flag ? 0 : 0);
	h264_set_field(shadow, F_START_CODE_E,      1);   /* start-code emulation on */
	h264_set_field(shadow, F_DEC_CLK_GATE_E,    1);
	h264_set_field(shadow, F_DEC_IRQ_DIS,       1);   /* VCMD uses polling/IRQ via arbiter */
	h264_set_field(shadow, F_APF_THRESHOLD,     8);

	/* ---- buffer addresses (64-bit split into LSB/MSB registers) ---- */
	h264_set_field(shadow, F_RLC_VLC_BASE_LSB, (unsigned int)(prm->rlc_vlc_mmu_addr & 0xffffffffu));
	h264_set_field(shadow, F_RLC_VLC_BASE_MSB, (unsigned int)((uint64_t)prm->rlc_vlc_mmu_addr >> 32));
	h264_set_field(shadow, F_MB_CTRL_BASE_LSB, (unsigned int)(prm->mb_ctrl_mmu_addr & 0xffffffffu));
	h264_set_field(shadow, F_DEC_OUT_BASE_LSB, (unsigned int)(prm->output_mmu_addr & 0xffffffffu));
	h264_set_field(shadow, F_DEC_OUT_BASE_MSB, (unsigned int)((uint64_t)prm->output_mmu_addr >> 32));

	/* Stream base lives in swreg 20/21 (HWIF_STRM_BASE_LSB/MSB). */
	{
		const struct h264_reg_field stream_base = {20, 32, 0};
		const struct h264_reg_field stream_base_msb = {21, 32, 0};
		shadow[stream_base.swreg]    = (unsigned int)(prm->stream_mmu_addr & 0xffffffffu);
		shadow[stream_base_msb.swreg] = (unsigned int)((uint64_t)prm->stream_mmu_addr >> 32);
	}

	/* ---- DPB reference frames (mirrors H264RunAsic in the VC8000D SDK) ---- */
	{
		unsigned int valid_mask = 0, lterm_mask = 0;
		int n;

		for (n = 0; n < 16; n++) {
			const struct h264_dpb_ref *r = &prm->dpb[n];

			/* Every slot gets a base address; unused slots point at ref[0]
			 * (SDK defensive default for erroneous streams). */
			unsigned long base = r->valid ? r->luma_mmu_addr
			                              : prm->dpb[0].luma_mmu_addr;
			h264_set_ref_base(shadow, n, base);
			h264_set_ref_nbr(shadow, n, r->frame_num);

			if (r->valid) {
				valid_mask |= 1u << (31 - n);  /* SDK bit order: 31-n */
				if (r->long_term)
					lterm_mask |= 1u << (31 - n);
			}
		}
		h264_set_ref_valid(shadow, valid_mask);
		h264_set_ref_longterm(shadow, lterm_mask);

		/* REF0 flags: top-field-available + field-mode (shared swreg 67). */
		if (prm->dpb[0].valid) {
			h264_set_ref0_flags(shadow,
			                    prm->dpb[0].poc_top != prm->dpb[0].poc_bottom,
			                    prm->dpb[0].field_pic);
			/* REF0 POC table: top, bottom, and derived values. */
			h264_set_ref0_poc(shadow, 0, prm->dpb[0].poc_top);
			h264_set_ref0_poc(shadow, 1, prm->dpb[0].poc_bottom);
			h264_set_ref0_poc(shadow, 2, prm->frame_num);
		}

		/* Reference picture lists.
		 * P.slice: INIT_RLIST_F (list0) + INIT_RLIST_B (list1).
		 * B.slice: BINIT_RLIST_F (list0) + BINIT_RLIST_B (list1).
		 * An invalid entry (0x1f) tells HW to ignore that slot. */
		for (n = 0; n < 16; n++) {
			unsigned int l0 = n < prm->refs.list0_count
			                ? (prm->refs.list0[n] & 0x1f) : 0x1f;
			unsigned int l1 = n < prm->refs.list1_count
			                ? (prm->refs.list1[n] & 0x1f) : 0x1f;

			if (prm->slice_type == 1 || prm->slice_type == 6) { /* B */
				h264_set_ref_list(shadow, RLIST_BF, n, l0);
				h264_set_ref_list(shadow, RLIST_BB, n, l1);
			} else { /* P / I */
				h264_set_ref_list(shadow, RLIST_PF, n, l0);
				h264_set_ref_list(shadow, RLIST_PB, n, l1);
			}
		}
	}

	/* TODO: POC table for refs 1..15 (SDK writes them into a work buffer,
	 * not registers — only REF0 has POC registers), CABAC init + scaling
	 * lists, and chroma output base. Those need a POC work buffer allocated
	 * like the surfaces; see h264PreparePOC() in the SDK. */

	/* ---- kick off decode: set DEC_E in the SAME shadow so that other bits
	 * in swreg1 (e.g. DEC_IRQ_DIS at lsb4) are preserved ---- */
	h264_set_field(shadow, F_DEC_E, 1);

	/* ---- emit shadow registers as WREG commands, skipping zero regs ---- */
	for (i = 0; i < H264_REG_SHADOW_WORDS; i++) {
		if (shadow[i] == 0)
			continue;
		if (p + 2 >= end)
			return -1;
		vcmd_cmd_wreg(&p, (uint32_t)i * 4, shadow[i]); /* reg offset in bytes */
	}

	vcmd_cmd_end(&p);

	return (int)(p - cmd);
}

/* ---------------- submit / wait ---------------------------------------- */

int vpu_vcmd_submit(struct vpu_vcmd *v, uint32_t *cmd, int cmd_words)
{
	struct exchange_parameter xchg;
	struct cmdbuf_id_parameter cid;
	u16 id;
	int ret;

	if (!v || !v->pool_ready || !cmd || cmd_words <= 0)
		return -1;

	/* 1) reserve a command buffer */
	memset(&xchg, 0, sizeof(xchg));
	xchg.client_type   = 0;
	xchg.module_type   = VCMD_TYPE_DECODER;
	xchg.cmdbuf_size   = (u16)(cmd_words * (int)sizeof(uint32_t));
	xchg.priority      = 0;
	xchg.has_end_cmd   = 1;
	xchg.core_id       = 0;
	xchg.core_mask     = 0;
	xchg.req_cores_info = 0;

	if (ioctl(v->drm_fd, VCMD_IOCH_RESERVE_CMDBUF, &xchg) < 0) {
		fprintf(stderr, "vcmd: RESERVE_CMDBUF failed: %s\n", strerror(errno));
		return -1;
	}
	id = xchg.cmdbuf_id;

	/* 2) copy command stream into the reserved slot */
	if (v->pool.virt_cmdbuf_addr && id < (v->pool.cmdbuf_total_size /
	                                       (v->pool.cmdbuf_unit_size ? v->pool.cmdbuf_unit_size : 1))) {
		uint32_t *slot = v->pool.virt_cmdbuf_addr +
		                 (size_t)id * (v->pool.cmdbuf_unit_size / sizeof(uint32_t));
		memcpy(slot, cmd, (size_t)cmd_words * sizeof(uint32_t));
	}

	/* 3) link + run */
	if (ioctl(v->drm_fd, VCMD_IOCH_LINK_RUN_CMDBUF, &xchg) < 0) {
		fprintf(stderr, "vcmd: LINK_RUN_CMDBUF failed: %s\n", strerror(errno));
		return -1;
	}

	/* 4) wait for completion */
	memset(&cid, 0, sizeof(cid));
	cid.cmdbuf_id = id;
	ret = ioctl(v->drm_fd, VCMD_IOCH_WAIT_CMDBUF, &cid);
	if (ret < 0) {
		fprintf(stderr, "vcmd: WAIT_CMDBUF failed: %s\n", strerror(errno));
		return -1;
	}

	/* 5) release */
	if (ioctl(v->drm_fd, VCMD_IOCH_RELEASE_CMDBUF, &cid) < 0) {
		fprintf(stderr, "vcmd: RELEASE_CMDBUF failed: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

void vpu_vcmd_close(struct vpu_vcmd *v)
{
	free(v);
}
