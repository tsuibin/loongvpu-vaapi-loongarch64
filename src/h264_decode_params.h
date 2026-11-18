#ifndef H264_DECODE_PARAMS_H
#define H264_DECODE_PARAMS_H

#include <stdint.h>

/*
 * Plain-C subset of VA-API H.264 params needed to program the VC8000D.
 * The driver extracts these from VAPictureParameterBufferH264 /
 * VASliceParameterBufferH264 and passes them here, so vcmd.c stays
 * independent of <va/va.h>.
 */
struct h264_decode_params {
	/* from VAPictureParameterBufferH264 */
	uint16_t pic_width_in_mbs_minus1;
	uint16_t pic_height_in_mbs_minus1;
	uint8_t  num_ref_frames;
	uint8_t  bit_depth_luma_minus8;
	uint8_t  bit_depth_chroma_minus8;
	int8_t   pic_init_qp_minus26;
	int8_t   chroma_qp_index_offset;
	int8_t   second_chroma_qp_index_offset;

	uint32_t chroma_format_idc       : 2;
	uint32_t frame_mbs_only_flag     : 1;
	uint32_t mb_adaptive_frame_field_flag : 1;
	uint32_t direct_8x8_inference_flag    : 1;
	uint32_t log2_max_frame_num_minus4    : 4;
	uint32_t pic_order_cnt_type           : 2;
	uint32_t log2_max_pic_order_cnt_lsb_minus4 : 4;

	uint32_t entropy_coding_mode_flag  : 1; /* CABAC */
	uint32_t field_pic_flag            : 1;
	uint32_t bottom_field_flag         : 1;
	uint32_t reference_pic_flag        : 1; /* nal_ref_idc != 0 */
	uint32_t constrained_intra_pred_flag : 1;

	uint16_t frame_num;

	/* from VASliceParameterBufferH264 */
	uint32_t slice_data_size;
	uint16_t slice_data_bit_offset;  /* strm_start_bit */
	uint16_t first_mb_in_slice;
	uint8_t  slice_type;
	uint8_t  dec_ref_pic_marking_bit_size;
	uint8_t  pic_order_cnt_bit_size;
	uint16_t idr_pic_id;
	uint32_t num_ref_idx_l0_active_minus1;
	uint32_t num_ref_idx_l1_active_minus1;

	/* from this driver (VPU buffers) */
	unsigned long stream_mmu_addr;      /* bitstream buffer, VPU MMU address */
	unsigned int  stream_id;
	unsigned long output_mmu_addr;      /* reconstructed output surface */
	unsigned long output_chroma_mmu_addr; /* chroma plane (if planar) */
	unsigned long rlc_vlc_mmu_addr;     /* RLC/VLC working buffer */
	unsigned long mb_ctrl_mmu_addr;     /* MB control working buffer */
};

#endif /* H264_DECODE_PARAMS_H */
