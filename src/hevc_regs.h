/*
 * hevc_regs.h - VC8000D (HW_ID 0x8001) HEVC/H.265 decode register map.
 *
 * Auto-extracted from the open VC8000D SDK (alphagocc/vc8000d):
 *   software/source/common/8170table.h, used by SetDecRegister for HW_ID
 *   0x8001 (= Loongson LG200 VPU, per IS_VC8000D in the kernel driver).
 * Field programming follows HevcSetRegs()/HevcRunAsic() in hevc_asic.c.
 *
 * Each field: { swreg, bits, lsb }; value is masked with (1<<bits)-1 and
 * shifted left by lsb into shadow[swreg] (SDK SetDecRegister).
 *
 * NOTE: HEVC register aliases differ from H.264. In HEVC mode:
 *   stream base  -> swreg 169/168 (RLC_VLC_BASE alias)
 *   scale list   -> swreg 171/170 (MB_CTRL_BASE alias)
 *   output Y/C   -> swreg 65/64 and 99/98
 * DEC_MODE = 12 (DEC_MODE_HEVC).
 */

#ifndef HEVC_REGS_H
#define HEVC_REGS_H

#include <stdint.h>

struct hevc_reg_field { unsigned int swreg, bits, lsb; };

enum {
	HF_DEC_MODE, HF_PIC_MB_WIDTH, HF_PIC_MB_HEIGHT, HF_REF_FRAMES,
	HF_INIT_QP, HF_DEC_E, HF_IDR_PIC_E, HF_SAO_E, HF_CABAC_INIT_PRESENT,
	HF_MIN_CB_SIZE, HF_MAX_CB_SIZE, HF_MIN_TRB_SIZE, HF_MAX_TRB_SIZE,
	HF_BIT_DEPTH_Y, HF_BIT_DEPTH_C, HF_TEMPOR_MVP_E, HF_SLICE_HDR_EBITS,
	HF_WRITE_MVS_E, HF_BLACKWHITE_E, HF_PCM_E, HF_ASYM_PRED_E,
	HF_CONST_INTRA_E, HF_TRANSFORM_SKIP_E, HF_CU_QPD_E, HF_MAX_CU_QPD_DEPTH,
	HF_CH_QP_OFFSET, HF_CH_QP_OFFSET2, HF_WEIGHT_PRED_E, HF_WEIGHT_BIPR_IDC,
	HF_SIGN_DATA_HIDE, HF_REFPICLIST_MOD_E, HF_MAX_INTER_HIERDEPTH,
	HF_MAX_INTRA_HIERDEPTH, HF_STRONG_SMOOTH_E, HF_ENTR_CODE_SYNCH_E,
	HF_PARALLEL_MERGE, HF_FILTERING_DIS, HF_REFIDX0_ACTIVE, HF_REFIDX1_ACTIVE,
	HF_DEC_OUT_DIS, HF_START_CODE_E, HF_DEC_CLK_GATE_E, HF_DEC_IRQ_DIS,
	HF_APF_THRESHOLD, HF_SCALING_LIST_E,
	HF_COUNT
};

static const struct hevc_reg_field hevc_fields[HF_COUNT] = {
	{  3,  5, 27 }, /* HF_DEC_MODE            HWIF_DEC_MODE (12 = HEVC) */
	{  4,  9, 23 }, /* HF_PIC_MB_WIDTH        HWIF_PIC_MB_WIDTH */
	{  4,  8, 11 }, /* HF_PIC_MB_HEIGHT       HWIF_PIC_MB_HEIGHT_P */
	{  4,  5,  0 }, /* HF_REF_FRAMES          HWIF_REF_FRAMES */
	{ 13,  7, 24 }, /* HF_INIT_QP             HWIF_INIT_QP */
	{  1,  1,  0 }, /* HF_DEC_E               HWIF_DEC_E */
	{  8,  1, 16 }, /* HF_IDR_PIC_E           HWIF_IDR_PIC_E */
	{  7,  1, 22 }, /* HF_SAO_E               HWIF_SAO_E */
	{  7,  1, 31 }, /* HF_CABAC_INIT_PRESENT  HWIF_CABAC_INIT_PRESENT */
	{ 12,  3, 13 }, /* HF_MIN_CB_SIZE         HWIF_MIN_CB_SIZE */
	{ 12,  3, 10 }, /* HF_MAX_CB_SIZE         HWIF_MAX_CB_SIZE */
	{ 13,  3, 13 }, /* HF_MIN_TRB_SIZE        HWIF_MIN_TRB_SIZE */
	{ 13,  3, 10 }, /* HF_MAX_TRB_SIZE        HWIF_MAX_TRB_SIZE */
	{  8,  2,  6 }, /* HF_BIT_DEPTH_Y         HWIF_BIT_DEPTH_Y_MINUS8 */
	{  8,  2,  4 }, /* HF_BIT_DEPTH_C         HWIF_BIT_DEPTH_C_MINUS8 */
	{  5,  1, 11 }, /* HF_TEMPOR_MVP_E        HWIF_TEMPOR_MVP_E */
	{  7,  3,  3 }, /* HF_SLICE_HDR_EBITS     HWIF_SLICE_HDR_EBITS */
	{  3,  1, 12 }, /* HF_WRITE_MVS_E         HWIF_WRITE_MVS_E */
	{  7,  1, 30 }, /* HF_BLACKWHITE_E        HWIF_BLACKWHITE_E */
	{ 12,  1,  3 }, /* HF_PCM_E               HWIF_PCM_E */
	{  7,  1, 23 }, /* HF_ASYM_PRED_E         HWIF_ASYM_PRED_E */
	{  8,  1, 31 }, /* HF_CONST_INTRA_E       HWIF_CONST_INTRA_E */
	{ 12,  1,  2 }, /* HF_TRANSFORM_SKIP_E    HWIF_TRANSFORM_SKIP_E */
	{  5,  1,  4 }, /* HF_CU_QPD_E            HWIF_CU_QPD_E */
	{  5,  6,  5 }, /* HF_MAX_CU_QPD_DEPTH    HWIF_MAX_CU_QPD_DEPTH */
	{  5,  5, 19 }, /* HF_CH_QP_OFFSET        HWIF_CH_QP_OFFSET */
	{  5,  5, 14 }, /* HF_CH_QP_OFFSET2       HWIF_CH_QP_OFFSET2 */
	{  7,  1, 28 }, /* HF_WEIGHT_PRED_E       HWIF_WEIGHT_PRED_E */
	{  7,  2, 26 }, /* HF_WEIGHT_BIPR_IDC     HWIF_WEIGHT_BIPR_IDC */
	{  5,  1, 12 }, /* HF_SIGN_DATA_HIDE      HWIF_SIGN_DATA_HIDE */
	{ 12,  1,  0 }, /* HF_REFPICLIST_MOD_E    HWIF_REFPICLIST_MOD_E */
	{ 13,  3,  4 }, /* HF_MAX_INTER_HIERDEPTH HWIF_MAX_INTER_HIERDEPTH */
	{ 13,  3,  7 }, /* HF_MAX_INTRA_HIERDEPTH HWIF_MAX_INTRA_HIERDEPTH */
	{  7,  1, 17 }, /* HF_STRONG_SMOOTH_E     HWIF_STRONG_SMOOTH_E */
	{ 10,  1,  0 }, /* HF_ENTR_CODE_SYNCH_E   HWIF_ENTR_CODE_SYNCH_E */
	{ 13,  4,  0 }, /* HF_PARALLEL_MERGE      HWIF_PARALLEL_MERGE */
	{  3,  1, 14 }, /* HF_FILTERING_DIS       HWIF_FILTERING_DIS */
	{  9,  5, 14 }, /* HF_REFIDX0_ACTIVE      HWIF_REFIDX0_ACTIVE */
	{  9,  5, 19 }, /* HF_REFIDX1_ACTIVE      HWIF_REFIDX1_ACTIVE */
	{  3,  1, 15 }, /* HF_DEC_OUT_DIS         HWIF_DEC_OUT_DIS */
	{ 13,  1, 31 }, /* HF_START_CODE_E        HWIF_START_CODE_E */
	{  2,  1, 10 }, /* HF_DEC_CLK_GATE_E      HWIF_DEC_CLK_GATE_E */
	{  1,  1,  4 }, /* HF_DEC_IRQ_DIS         HWIF_DEC_IRQ_DIS */
	{ 55, 16,  0 }, /* HF_APF_THRESHOLD       HWIF_APF_THRESHOLD */
	{  5,  1, 24 }, /* HF_SCALING_LIST_E      HWIF_SCALING_LIST_E */
};

#define HEVC_REG_SHADOW_WORDS 256
#define DEC_MODE_HEVC         12u

static inline void hevc_set_field(unsigned int *shadow, int field,
                                  unsigned int value)
{
	const struct hevc_reg_field *f = &hevc_fields[field];
	unsigned int mask = (f->bits >= 32) ? 0xffffffffu : ((1u << f->bits) - 1u);
	shadow[f->swreg] &= ~(mask << f->lsb);
	shadow[f->swreg] |= (value & mask) << f->lsb;
}

/* HEVC reference-frame base registers (from SDK hevc_asic.c). The decoder
 * uses a rotating DPB; these are the REFERn base regs like H.264:
 *   REFERn_BASE_LSB = 67+2n, MSB = 66+2n (n=0..14), VALID swreg39, LTERM swreg38
 *   REFERn_NBR (POC) = swreg 30+n/2 lsb (n%2)*16, 16 bits
 */
static inline void hevc_set_ref_base(unsigned int *shadow, int n,
                                     unsigned long addr)
{
	shadow[67 + 2 * n] = (unsigned int)(addr & 0xffffffffu);
	shadow[66 + 2 * n] = (unsigned int)((uint64_t)addr >> 32);
}
static inline void hevc_set_ref_poc(unsigned int *shadow, int n, unsigned int poc)
{
	unsigned int swreg = 30 + n / 2, lsb = (n % 2) * 16;
	shadow[swreg] &= ~(0xffffu << lsb);
	shadow[swreg] |= (poc & 0xffffu) << lsb;
}
static inline void hevc_set_ref_valid(unsigned int *shadow, unsigned int mask)
{
	shadow[39] = mask;
}
static inline void hevc_set_ref_longterm(unsigned int *shadow, unsigned int mask)
{
	shadow[38] = mask;
}

/* HEVC reference POC differences: HWIF_CUR_POC_00..15 are 8-bit fields,
 * 4 per 32-bit register at swreg 46..49, lsb = 24 - (i%4)*8. */
static inline void hevc_set_cur_poc(unsigned int *shadow, int i, int diff)
{
	unsigned int swreg = 46 + i / 4;
	unsigned int lsb = 24 - (i % 4) * 8;
	unsigned int val = (unsigned int)(diff & 0xff);
	shadow[swreg] &= ~(0xffu << lsb);
	shadow[swreg] |= (val & 0xffu) << lsb;
}

/* HEVC reference list registers (shared VC8000D INIT_RLIST):
 *   INIT_RLIST_F0..15 = swreg {14..19}, lsb (i%3)*10
 *   INIT_RLIST_B0..15 = swreg {14..19}, lsb 5+(i%3)*10 */
static inline void hevc_set_rlist0(unsigned int *shadow, int i, unsigned int v)
{
	unsigned int swreg = 14 + i / 3, lsb = (i % 3) * 10;
	shadow[swreg] &= ~(0x1fu << lsb);
	shadow[swreg] |= (v & 0x1fu) << lsb;
}
static inline void hevc_set_rlist1(unsigned int *shadow, int i, unsigned int v)
{
	unsigned int swreg = 14 + i / 3, lsb = 5 + (i % 3) * 10;
	shadow[swreg] &= ~(0x1fu << lsb);
	shadow[swreg] |= (v & 0x1fu) << lsb;
}

#endif /* HEVC_REGS_H */
