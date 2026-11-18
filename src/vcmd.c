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
 * Both H.264 and HEVC register maps are extracted from the open VC8000D SDK
 * (alphagocc/vc8000d, 8170table.h) for HW_ID 0x8001 = Loongson LG200 VPU.
 * The builders program the core decode registers from VA-API params and emit
 * a WREG command stream; real-hardware validation is still required.
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
#include "hevc_regs.h"
#include "hevc_decode_params.h"

/* Register maps (h264_regs.h / hevc_regs.h) are auto-extracted from VC8000D SDK. */

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
	h264_set_field(shadow, F_FILTERING_DIS,     0);   /* deblocking filter enabled */
	h264_set_field(shadow, F_START_CODE_E,      1);   /* start-code emulation on */
	h264_set_field(shadow, F_DEC_CLK_GATE_E,    1);
	h264_set_field(shadow, F_DEC_IRQ_DIS,       1);   /* VCMD uses polling/IRQ via arbiter */
	h264_set_field(shadow, F_APF_THRESHOLD,     8);
	/* HW-side POC decoding + scaling-list enable. POC decoding is used for
	 * any non-baseline stream (we always provide a POC table); scaling list
	 * is enabled only when the IQ buffer supplied one. */
	h264_set_field(shadow, F_PICORD_COUNT_E,   1);
	h264_set_field(shadow, F_TYPE1_QUANT_E,    prm->scaling_list_present);

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

/* ---------------- HEVC/H.265 decode command builder -------------------- */
/*
 * Program the VC8000D HEVC decoder registers from VA-API HEVC params and emit
 * them as a VCMD OPCODE_WREG stream.  Core SPS/PPS/slice fields, DPB refs and
 * buffer addresses are covered; see hevc_regs.h and README for scope.
 * Returns number of uint32 words written, or negative on error.
 */
int vcmd_hevc_build_decode_cmd(uint32_t *cmd, size_t cmd_words,
                               const struct hevc_decode_params *prm)
{
	unsigned int shadow[HEVC_REG_SHADOW_WORDS] = {0};
	uint32_t *p = cmd;
	uint32_t *end = cmd + cmd_words;
	int i;
	unsigned int mb_w, mb_h, valid_mask = 0, lterm_mask = 0;

	if (!cmd || !prm || cmd_words < 64)
		return -1;

	mb_w = (prm->pic_width_in_luma_samples + 15) / 16;
	mb_h = (prm->pic_height_in_luma_samples + 15) / 16;

	/* ---- core decode control (HevcSetRegs) ---- */
	hevc_set_field(shadow, HF_DEC_MODE, DEC_MODE_HEVC);
	hevc_set_field(shadow, HF_PIC_MB_WIDTH, mb_w);
	hevc_set_field(shadow, HF_PIC_MB_HEIGHT, mb_h);
	hevc_set_field(shadow, HF_REF_FRAMES, prm->max_dec_pic_buffering);
	hevc_set_field(shadow, HF_INIT_QP, (unsigned int)(prm->init_qp_minus26 + 26));

	/* SPS block-size / bit-depth / feature flags */
	hevc_set_field(shadow, HF_MIN_CB_SIZE, prm->log2_min_cb_size);
	hevc_set_field(shadow, HF_MAX_CB_SIZE, prm->log2_max_cb_size);
	hevc_set_field(shadow, HF_MIN_TRB_SIZE, prm->log2_min_trb_size);
	hevc_set_field(shadow, HF_MAX_TRB_SIZE, prm->log2_max_trb_size);
	hevc_set_field(shadow, HF_BIT_DEPTH_Y, prm->bit_depth_luma_minus8);
	hevc_set_field(shadow, HF_BIT_DEPTH_C, prm->bit_depth_chroma_minus8);
	hevc_set_field(shadow, HF_BLACKWHITE_E, prm->chroma_format_idc == 0);
	hevc_set_field(shadow, HF_PCM_E, prm->pcm_enabled);
	hevc_set_field(shadow, HF_ASYM_PRED_E, prm->temporal_mvp_enabled); /* placeholder mapping */
	hevc_set_field(shadow, HF_SAO_E, prm->sample_adaptive_offset_enabled);
	hevc_set_field(shadow, HF_STRONG_SMOOTH_E, prm->strong_intra_smoothing);
	hevc_set_field(shadow, HF_ENTR_CODE_SYNCH_E, prm->entropy_coding_sync_enabled);
	hevc_set_field(shadow, HF_CONST_INTRA_E, prm->constrained_intra_pred);
	hevc_set_field(shadow, HF_TRANSFORM_SKIP_E, prm->transform_skip_enabled);
	hevc_set_field(shadow, HF_CU_QPD_E, prm->cu_qp_delta_enabled);
	hevc_set_field(shadow, HF_MAX_CU_QPD_DEPTH, prm->diff_cu_qp_delta_depth);
	hevc_set_field(shadow, HF_SIGN_DATA_HIDE, prm->sign_data_hiding);
	hevc_set_field(shadow, HF_REFPICLIST_MOD_E, prm->lists_modification_present);
	hevc_set_field(shadow, HF_CABAC_INIT_PRESENT, prm->cabac_init_present);
	hevc_set_field(shadow, HF_TEMPOR_MVP_E, prm->temporal_mvp_enabled);
	hevc_set_field(shadow, HF_MAX_INTRA_HIERDEPTH, prm->max_transform_hierarchy_depth_intra);
	hevc_set_field(shadow, HF_MAX_INTER_HIERDEPTH, prm->max_transform_hierarchy_depth_inter);
	hevc_set_field(shadow, HF_PARALLEL_MERGE, prm->log2_parallel_merge_level);

	/* PPS QP / weights / filter */
	hevc_set_field(shadow, HF_CH_QP_OFFSET, (unsigned int)(prm->cb_qp_offset & 0x1f));
	hevc_set_field(shadow, HF_CH_QP_OFFSET2, (unsigned int)(prm->cr_qp_offset & 0x1f));
	hevc_set_field(shadow, HF_WEIGHT_PRED_E, prm->weighted_pred_flag);
	hevc_set_field(shadow, HF_WEIGHT_BIPR_IDC, prm->weighted_bipred_flag);
	hevc_set_field(shadow, HF_REFIDX0_ACTIVE, prm->num_ref_idx_l0_active);
	hevc_set_field(shadow, HF_REFIDX1_ACTIVE, prm->num_ref_idx_l1_active);
	hevc_set_field(shadow, HF_FILTERING_DIS, prm->disable_deblocking_filter);
	hevc_set_field(shadow, HF_SLICE_HDR_EBITS, prm->slice_hdr_extra_bits);
	hevc_set_field(shadow, HF_IDR_PIC_E, prm->idr_pic_flag);
	hevc_set_field(shadow, HF_WRITE_MVS_E, 1);
	hevc_set_field(shadow, HF_START_CODE_E, 1);
	hevc_set_field(shadow, HF_DEC_CLK_GATE_E, 1);
	hevc_set_field(shadow, HF_DEC_IRQ_DIS, 1);
	hevc_set_field(shadow, HF_APF_THRESHOLD, 8);

	/* ---- DPB reference frames (15 slots) ---- */
	for (i = 0; i < 15; i++) {
		const struct hevc_dpb_ref *r = &prm->dpb[i];
		unsigned long base = r->valid ? r->luma_mmu_addr
		                              : prm->dpb[0].luma_mmu_addr;
		hevc_set_ref_base(shadow, i, base);
		hevc_set_ref_poc(shadow, i, (unsigned int)(r->poc & 0xffff));
		if (r->valid) {
			valid_mask |= 1u << i;
			if (r->long_term) lterm_mask |= 1u << i;
		}
	}
	hevc_set_ref_valid(shadow, valid_mask);
	hevc_set_ref_longterm(shadow, lterm_mask);

	/* Reference list entries (HEVC VA RefPicList[n][] are DPB indices).
	 * The SDK writes these via HWIF_INIT_RLIST_*; use the H.264-style list
	 * registers here as the shared VC8000D reference-list mechanism. */
	for (i = 0; i < 15; i++) {
		unsigned int l0 = i < prm->ref_list0_count ? (prm->ref_list0[i] & 0x1f) : 0x1f;
		unsigned int l1 = i < prm->ref_list1_count ? (prm->ref_list1[i] & 0x1f) : 0x1f;
		struct h264_reg_field f0 = {14 + i / 3, 5, (i % 3) * 10};
		struct h264_reg_field f1 = {14 + i / 3, 5, 5 + (i % 3) * 10};
		shadow[f0.swreg] &= ~(0x1fu << f0.lsb);
		shadow[f0.swreg] |= (l0 & 0x1f) << f0.lsb;
		shadow[f1.swreg] &= ~(0x1fu << f1.lsb);
		shadow[f1.swreg] |= (l1 & 0x1f) << f1.lsb;
	}

	/* ---- buffer addresses (HEVC aliases) ---- */
	/* stream base -> 169/168;  output Y -> 65/64, C -> 99/98;
	 * scale list -> 171/170.  (Work buffer already supplied via RLC base.) */
	shadow[169] = (unsigned int)(prm->stream_mmu & 0xffffffffu);
	shadow[168] = (unsigned int)((uint64_t)prm->stream_mmu >> 32);
	shadow[65]  = (unsigned int)(prm->output_y_mmu & 0xffffffffu);
	shadow[64]  = (unsigned int)((uint64_t)prm->output_y_mmu >> 32);
	shadow[99]  = (unsigned int)(prm->output_c_mmu & 0xffffffffu);
	shadow[98]  = (unsigned int)((uint64_t)prm->output_c_mmu >> 32);
	shadow[171] = (unsigned int)(prm->scale_list_mmu & 0xffffffffu);
	shadow[170] = (unsigned int)((uint64_t)prm->scale_list_mmu >> 32);

	/* Kick off decode in the same shadow to preserve other swreg1 bits. */
	hevc_set_field(shadow, HF_DEC_E, 1);

	for (i = 0; i < HEVC_REG_SHADOW_WORDS; i++) {
		if (shadow[i] == 0)
			continue;
		if (p + 2 >= end)
			return -1;
		vcmd_cmd_wreg(&p, (uint32_t)i * 4, shadow[i]);
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
