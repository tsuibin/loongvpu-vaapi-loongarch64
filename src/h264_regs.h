/*
 * h264_regs.h - VC8000D (HW_ID 0x8001) H.264 decode register map.
 *
 * Auto-extracted from the open-source VC8000D SDK:
 *   alphagocc/vc8000d  software/source/common/8170table.h
 *   (the same table used by SetDecRegister for HW_ID 0x8001)
 *
 * Loongson LG200 VPU identifies as VC8000D (0x8001) per the open kernel
 * driver (IS_VC8000D in hantro/hantrodec.c), so these registers apply.
 *
 * Each field: { swreg, bits, lsb }. Value is masked with (1<<bits)-1 and
 * shifted left by lsb into shadow[swreg] (see SetDecRegister in SDK regdrv.c).
 */

#ifndef H264_REGS_H
#define H264_REGS_H

struct h264_reg_field { unsigned int swreg, bits, lsb; };

/* Field indices into h264_fields[]. */
enum {
	F_DEC_MODE, F_PIC_MB_WIDTH, F_PIC_MB_HEIGHT, F_REF_FRAMES,
	F_STRM_START_BIT, F_STREAM_LEN, F_INIT_QP, F_CH_QP_OFFSET,
	F_CH_QP_OFFSET2, F_CABAC_E, F_FRAMENUM_LEN, F_FRAMENUM,
	F_DEC_OUT_BASE_LSB, F_DEC_OUT_BASE_MSB,
	F_RLC_VLC_BASE_LSB, F_RLC_VLC_BASE_MSB,
	F_MB_CTRL_BASE_LSB, F_DIR_8X8_INFER_E, F_DEC_E, F_FILTERING_DIS,
	F_POC_LENGTH, F_PIC_INTERLACE_E, F_PIC_FIELDMODE_E, F_IDR_PIC_ID,
	F_START_CODE_E, F_WRITE_MVS_E, F_APF_THRESHOLD, F_REFPIC_MK_LEN,
	F_DEC_CLK_GATE_E, F_DEC_IRQ_DIS,
	F_COUNT
};

static const struct h264_reg_field h264_fields[F_COUNT] = {
	{  3,  5, 27 }, /* F_DEC_MODE            HWIF_DEC_MODE */
	{  4,  9, 23 }, /* F_PIC_MB_WIDTH        HWIF_PIC_MB_WIDTH */
	{  4,  8, 11 }, /* F_PIC_MB_HEIGHT       HWIF_PIC_MB_HEIGHT_P */
	{  4,  5,  0 }, /* F_REF_FRAMES          HWIF_REF_FRAMES */
	{  5,  7, 25 }, /* F_STRM_START_BIT      HWIF_STRM_START_BIT */
	{  6, 32,  0 }, /* F_STREAM_LEN          HWIF_STREAM_LEN */
	{ 13,  7, 24 }, /* F_INIT_QP             HWIF_INIT_QP */
	{  5,  5, 19 }, /* F_CH_QP_OFFSET        HWIF_CH_QP_OFFSET */
	{  5,  5, 14 }, /* F_CH_QP_OFFSET2       HWIF_CH_QP_OFFSET2 */
	{  7,  1, 31 }, /* F_CABAC_E             HWIF_CABAC_E */
	{  7,  5, 16 }, /* F_FRAMENUM_LEN        HWIF_FRAMENUM_LEN */
	{  7, 16,  0 }, /* F_FRAMENUM            HWIF_FRAMENUM */
	{ 65, 32,  0 }, /* F_DEC_OUT_BASE_LSB    HWIF_DEC_OUT_BASE_LSB */
	{ 64, 32,  0 }, /* F_DEC_OUT_BASE_MSB    HWIF_DEC_OUT_BASE_MSB */
	{169, 32,  0 }, /* F_RLC_VLC_BASE_LSB    HWIF_RLC_VLC_BASE_LSB */
	{168, 32,  0 }, /* F_RLC_VLC_BASE_MSB    HWIF_RLC_VLC_BASE_MSB */
	{171, 32,  0 }, /* F_MB_CTRL_BASE_LSB    HWIF_MB_CTRL_BASE_LSB */
	{  7,  1, 29 }, /* F_DIR_8X8_INFER_E     HWIF_DIR_8X8_INFER_E */
	{  1,  1,  0 }, /* F_DEC_E               HWIF_DEC_E */
	{  3,  1, 14 }, /* F_FILTERING_DIS       HWIF_FILTERING_DIS */
	{  9,  8,  0 }, /* F_POC_LENGTH          HWIF_POC_LENGTH */
	{  3,  1, 23 }, /* F_PIC_INTERLACE_E     HWIF_PIC_INTERLACE_E */
	{  3,  1, 22 }, /* F_PIC_FIELDMODE_E     HWIF_PIC_FIELDMODE_E */
	{  8, 16,  0 }, /* F_IDR_PIC_ID          HWIF_IDR_PIC_ID */
	{ 13,  1, 31 }, /* F_START_CODE_E        HWIF_START_CODE_E */
	{  3,  1, 12 }, /* F_WRITE_MVS_E         HWIF_WRITE_MVS_E */
	{ 55, 16,  0 }, /* F_APF_THRESHOLD       HWIF_APF_THRESHOLD */
	{  8, 11, 17 }, /* F_REFPIC_MK_LEN       HWIF_REFPIC_MK_LEN */
	{  2,  1, 10 }, /* F_DEC_CLK_GATE_E      HWIF_DEC_CLK_GATE_E */
	{  1,  1,  4 }, /* F_DEC_IRQ_DIS         HWIF_DEC_IRQ_DIS */
};

#define H264_REG_SHADOW_WORDS 256
#define DEC_X170_MODE_H264    0x00

/* SDK SetDecRegister(): value -> masked field in shadow[swreg]. */
static inline void h264_set_field(unsigned int *shadow, int field,
                                  unsigned int value)
{
	const struct h264_reg_field *f = &h264_fields[field];
	unsigned int mask;

	if (f->bits == 0 || f->bits >= 32)
		mask = 0xffffffffu;
	else
		mask = (1u << f->bits) - 1u;

	shadow[f->swreg] &= ~(mask << f->lsb);
	shadow[f->swreg] |= (value & mask) << f->lsb;
}

#endif /* H264_REGS_H */
