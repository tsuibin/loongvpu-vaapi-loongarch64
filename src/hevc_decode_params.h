#ifndef HEVC_DECODE_PARAMS_H
#define HEVC_DECODE_PARAMS_H

#include <stdint.h>

/* Plain-C subset of VA-API HEVC decode params for VC8000D register programming. */
/* VA_PICTURE_HEVC_RPS_* flags, used to build the hardware reference lists. */
#define HEVC_RPS_ST_CURR_BEFORE 0x10u
#define HEVC_RPS_ST_CURR_AFTER  0x20u
#define HEVC_RPS_LT_CURR        0x40u

struct hevc_dpb_ref {
	unsigned long luma_mmu_addr;
	unsigned long chroma_mmu_addr;
	int32_t       poc;
	uint8_t       valid;
	uint8_t       long_term;
	uint8_t       rps_before;     /* CURR_BEFORE */
	uint8_t       rps_after;      /* CURR_AFTER */
	uint8_t       rps_lt;         /* LT_CURR */
};

struct hevc_decode_params {
	/* picture dimension / SPS */
	uint16_t pic_width_in_luma_samples;
	uint16_t pic_height_in_luma_samples;
	uint8_t  log2_min_cb_size;      /* log2_min_luma_coding_block_size_minus3 + 3 */
	uint8_t  log2_max_cb_size;      /* log2_min_cb + log2_diff_max_min */
	uint8_t  log2_min_trb_size;     /* log2_min_transform_block_size_minus2 + 2 */
	uint8_t  log2_max_trb_size;
	uint8_t  bit_depth_luma_minus8;
	uint8_t  bit_depth_chroma_minus8;
	uint8_t  chroma_format_idc;
	uint8_t  max_dec_pic_buffering;
	uint8_t  pcm_enabled;
	uint8_t  pcm_bit_depth_y;
	uint8_t  pcm_bit_depth_c;
	uint8_t  max_transform_hierarchy_depth_intra;
	uint8_t  max_transform_hierarchy_depth_inter;
	uint8_t  log2_parallel_merge_level;  /* minus2 + 2 */

	/* PPS / flags */
	int8_t   init_qp_minus26;
	int8_t   cb_qp_offset;
	int8_t   cr_qp_offset;
	uint8_t  diff_cu_qp_delta_depth;
	uint8_t  num_ref_idx_l0_active;  /* minus1 + 1 */
	uint8_t  num_ref_idx_l1_active;
	uint8_t  weighted_pred_flag;
	uint8_t  weighted_bipred_flag;
	uint8_t  cu_qp_delta_enabled;
	uint8_t  constrained_intra_pred;
	uint8_t  transform_skip_enabled;
	uint8_t  sign_data_hiding;
	uint8_t  lists_modification_present;
	uint8_t  cabac_init_present;
	uint8_t  temporal_mvp_enabled;
	uint8_t  sample_adaptive_offset_enabled;
	uint8_t  strong_intra_smoothing;
	uint8_t  entropy_coding_sync_enabled;
	uint8_t  deblocking_filter_override;
	uint8_t  disable_deblocking_filter;
	uint8_t  slice_segment_header_extension_present;
	uint8_t  idr_pic_flag;
	uint8_t  rap_pic_flag;
	uint8_t  dependent_slice_segments;
	uint8_t  slice_hdr_extra_bits;

	/* slice */
	uint32_t slice_data_size;
	uint32_t slice_data_byte_offset;
	uint8_t  slice_type;            /* 0=B,1=P,2=I per VA */
	int32_t  curr_poc;              /* current picture POC, for RPS POC diffs */

	/* DPB (15 refs per HEVC VA) + ref lists */
	struct hevc_dpb_ref dpb[15];
	uint8_t  ref_list0[15];
	uint8_t  ref_list1[15];
	uint8_t  ref_list0_count;
	uint8_t  ref_list1_count;

	/* buffers (VPU MMU addresses) */
	unsigned long output_y_mmu;
	unsigned long output_c_mmu;
	unsigned long stream_mmu;
	unsigned long scale_list_mmu;
	unsigned long rlc_vlc_mmu;      /* HEVC work buffer */

	/* Scaling lists (from VAIQMatrixBufferHEVC) */
	uint8_t  scaling_list_enable;          /* SPS scaling_list_enabled_flag */
	uint8_t  scaling_list4x4[6][16];
	uint8_t  scaling_list8x8[6][64];
	uint8_t  scaling_list16x16[6][64];
	uint8_t  scaling_list32x32[2][64];
	uint8_t  scaling_list_dc16[6];
	uint8_t  scaling_list_dc32[2];
};

#endif /* HEVC_DECODE_PARAMS_H */
