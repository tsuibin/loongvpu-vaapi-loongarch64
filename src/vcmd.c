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

/* Known register offsets from dwl_defs.h (open source, GPL). */
#define HANTRODEC_HW_ID_OFF         (0   * 4)
#define HANTRODEC_SYNTH_CFG_OFF     (50  * 4)
#define HANTRODEC_CFG_STAT_OFF      (23  * 4)
#define HANTRODEC_IRQ_STAT_OFF      (1   * 4)
#define HANTRO_DEC_E                0x01
#define DWL_H264_E_BIT              24   /* DWL_H264_E is 2 bits @24 */

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
 * STUB / STRUCTURAL SKELETON.
 *
 * 'bs' is a caller-provided undecoded H.264 access unit; 'out_mmu' is the
 * MMU-mapped address of the output surface (from vpu_buffer_mmu_map()).
 *
 * A real implementation must parse SPS/PPS/slice headers and emit the exact
 * VCD register sequence (stream base/length, frame dimensions, DPB refs,
 * pic order cnt, cabac flags, output base, DEC_E start bit...). Those values
 * are chip-specific and unavailable in the open tree; this skeleton emits a
 * minimal, well-formed command stream that exercises the same ioctl path.
 *
 * Returns number of uint32 words written, or negative on error.
 */
int vcmd_h264_build_decode_cmd(uint32_t *cmd, size_t cmd_words,
                               const void *bs, size_t bs_size,
                               unsigned long out_mmu, unsigned int out_id)
{
	uint32_t *p = cmd;
	uint32_t *end = cmd + cmd_words;
	const unsigned char *stream = (const unsigned char *)bs;

	(void)stream; (void)bs_size; (void)out_id;

	if (!cmd || cmd_words < 16)
		return -1;

	/* ---- structural skeleton: identify + configure ---- */
	/* Read HW id/config (harmless, validates the register path). */
	vcmd_cmd_wreg(&p, HANTRODEC_HW_ID_OFF, 0);         /* trigger read */
	if (p >= end) return -1;

	/*
	 * TODO (chip manual required):
	 *   - write H.264 stream base address / length
	 *   - write picture width/height, profile, DPB base, ref lists
	 *   - write output (MMU) address: out_mmu
	 *   - set DEC_E (HANTRO_DEC_E) to start decode
	 * Example placeholder writes using known bit positions:
	 */
	vcmd_cmd_wreg(&p, HANTRODEC_CFG_STAT_OFF, (1u << DWL_H264_E_BIT));
	if (p >= end) return -1;
	vcmd_cmd_wreg(&p, HANTRODEC_SYNTH_CFG_OFF, (uint32_t)(out_mmu & 0xffffffffu));

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
