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
	F_PICORD_COUNT_E,      /* HWIF_PICORD_COUNT_E     swreg 3 lsb 9  (POC decode) */
	F_TYPE1_QUANT_E,       /* HWIF_TYPE1_QUANT_E      swreg 5 lsb 24 (scaling list) */
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
	{  3,  1,  9 }, /* F_PICORD_COUNT_E      HWIF_PICORD_COUNT_E */
	{  5,  1, 24 }, /* F_TYPE1_QUANT_E       HWIF_TYPE1_QUANT_E */
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

/* Raw register write (for whole-register fields like address LSB/MSB). */
static inline void h264_set_raw(unsigned int *shadow, unsigned int swreg,
                                unsigned int value)
{
	if (swreg < H264_REG_SHADOW_WORDS)
		shadow[swreg] = value;
}

/* ---- DPB reference-frame registers (VC8000D, HW_ID 0x8001) ------------ */
/* Extracted from 8170table.h:
 *   REFERn_BASE_LSB  = swreg 67+2n, MSB = 66+2n  (n=0..15)
 *   REFERn_NBR       = swreg 30+n/2, lsb = (n%2)*16, 16 bits (frame_num)
 *   REFER_VALID_E    = swreg 39 (bit n = ref n valid)
 *   REFER_LTERM_E    = swreg 38 (bit n = ref n long-term)
 *   REFER0_TOPC_E    = swreg 67 lsb0, REFER0_FIELD_E = swreg 67 lsb1
 *   REF0_POC0..6     = swreg 51 lsb20, 51 lsb10, 51 lsb0,
 *                      52 lsb20, 52 lsb10, 52 lsb0, 53 lsb20 (10 bits each)
 */

/* Write reference frame luma base (64-bit addr split LSB/MSB). */
static inline void h264_set_ref_base(unsigned int *shadow, int n,
                                     unsigned long addr)
{
	h264_set_raw(shadow, 67 + 2 * n, (unsigned int)(addr & 0xffffffffu));
	h264_set_raw(shadow, 66 + 2 * n, (unsigned int)((uint64_t)addr >> 32));
}

/* Write reference frame number (frame_num) into REFERn_NBR. */
static inline void h264_set_ref_nbr(unsigned int *shadow, int n,
                                    unsigned int frame_num)
{
	unsigned int swreg = 30 + n / 2;
	unsigned int lsb = (n % 2) * 16;
	unsigned int mask = 0xffffu;

	shadow[swreg] &= ~(mask << lsb);
	shadow[swreg] |= (frame_num & mask) << lsb;
}

/* Valid / long-term bitmasks. */
static inline void h264_set_ref_valid(unsigned int *shadow, unsigned int mask)
{
	h264_set_raw(shadow, 39, mask);
}
static inline void h264_set_ref_longterm(unsigned int *shadow, unsigned int mask)
{
	h264_set_raw(shadow, 38, mask);
}

/* REF0 top-field / field-mode flags (swreg 67 bits 0/1). NOTE: these share
 * the register with REFER0_BASE_LSB, so set only the flag bits, preserving
 * the address that h264_set_ref_base() wrote. */
static inline void h264_set_ref0_flags(unsigned int *shadow, int topc,
                                       int field)
{
	if (topc)  shadow[67] |= 1u << 0;
	if (field) shadow[67] |= 1u << 1;
}

/* REF0 POC table (7 entries of 10 bits). */
static inline void h264_set_ref0_poc(unsigned int *shadow, int idx, int poc)
{
	static const unsigned int reg[7] = {51, 51, 51, 52, 52, 52, 53};
	static const unsigned int lsb[7] = {20, 10, 0, 20, 10, 0, 20};
	unsigned int mask = 0x3ffu;

	if (idx < 0 || idx > 6)
		return;
	shadow[reg[idx]] &= ~(mask << lsb[idx]);
	shadow[reg[idx]] |= ((unsigned int)poc & mask) << lsb[idx];
}

/* ---- Reference picture list registers (VC8000D) ----------------------- */
/* Three list families, each 16 entries x 5 bits packed into registers:
 *   INIT_RLIST_Fn : swreg {14,15,16,17,18,19}, lsb {0,10,20} per reg
 *   INIT_RLIST_Bn : same regs, lsb {5,15,25}
 *   PINIT_RLIST_Fn: swreg {47,10,11}, lsb {10,15,20,25} then {0,5,..25}
 *   BINIT_RLIST_Fn: swreg {42,43,44,45,46,47}, lsb {0,10,20}
 *   BINIT_RLIST_Bn: same regs, lsb {5,15,25}
 * 'value' is the DPB slot index (0..15) or 0x1f for invalid.
 */
enum h264_rlist_kind { RLIST_PF, RLIST_PB, RLIST_BF, RLIST_BB };

static inline void h264_set_ref_list(unsigned int *shadow,
                                     enum h264_rlist_kind kind, int idx,
                                     unsigned int value)
{
	unsigned int swreg, lsb;
	unsigned int mask = 0x1fu;

	if (idx < 0 || idx > 15)
		return;
	switch (kind) {
	case RLIST_PF: /* INIT_RLIST_Fn */
		swreg = 14 + idx / 3;
		lsb = (idx % 3) * 10;
		break;
	case RLIST_PB: /* INIT_RLIST_Bn */
		swreg = 14 + idx / 3;
		lsb = 5 + (idx % 3) * 10;
		break;
	case RLIST_BF: /* BINIT_RLIST_Fn */
		swreg = 42 + idx / 3;
		lsb = (idx % 3) * 10;
		break;
	case RLIST_BB: /* BINIT_RLIST_Bn */
		swreg = 42 + idx / 3;
		lsb = 5 + (idx % 3) * 10;
		break;
	default:
		return;
	}
	shadow[swreg] &= ~(mask << lsb);
	shadow[swreg] |= (value & mask) << lsb;
}

#endif /* H264_REGS_H */
