#ifndef VCMD_H
#define VCMD_H

#include <stdint.h>
#include <stddef.h>

/* Opaque VCMD submission context. */
struct vpu_vcmd;

/* Open the VCMD pool on the given DRM render fd. */
struct vpu_vcmd *vpu_vcmd_open(int drm_fd);
void vpu_vcmd_close(struct vpu_vcmd *v);

/* --- command word builders (write sequential uint32 into a buffer) ----- */
void vcmd_cmd_wreg(uint32_t **pp, uint32_t offset, uint32_t value);
void vcmd_cmd_end(uint32_t **pp);
void vcmd_cmd_jmp(uint32_t **pp, uint16_t cmdbuf_id);

/*
 * Build a (skeletal) H.264 decode command stream.
 * Returns word count written, or negative on error.
 * NOTE: concrete H.264 VCD register values require the chip manual; see
 * the comment in vcmd.c. This emits a structurally valid, well-formed
 * stream that exercises the submission path.
 */
int vcmd_h264_build_decode_cmd(uint32_t *cmd, size_t cmd_words,
                               const void *bs, size_t bs_size,
                               unsigned long out_mmu, unsigned int out_id);

/* Reserve -> fill -> link/run -> wait -> release one command buffer. */
int vpu_vcmd_submit(struct vpu_vcmd *v, uint32_t *cmd, int cmd_words);

#endif /* VCMD_H */
