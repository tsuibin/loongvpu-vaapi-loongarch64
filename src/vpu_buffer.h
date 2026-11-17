#ifndef VPU_BUFFER_H
#define VPU_BUFFER_H

#include <stddef.h>

/* Opaque DMA-BUF / MMU-mapped buffer. */
struct vpu_buffer;

/* Allocate a GEM dumb buffer of width x height @ bpp and mmap it to CPU. */
struct vpu_buffer *vpu_buffer_alloc(int drm_fd, size_t width, size_t height,
                                    unsigned int bpp);

/* Map / unmap the buffer into the VPU MMU for the given slice. */
int vpu_buffer_mmu_map(struct vpu_buffer *buf, unsigned int slice);
int vpu_buffer_mmu_unmap(struct vpu_buffer *buf);

/* Accessors. */
void  *vpu_buffer_vaddr(struct vpu_buffer *b);
size_t vpu_buffer_size(struct vpu_buffer *b);
unsigned long vpu_buffer_phy(struct vpu_buffer *b);   /* physical/bus addr */
unsigned long vpu_buffer_mmu(struct vpu_buffer *b);   /* VPU MMU addr */
unsigned int  vpu_buffer_id(struct vpu_buffer *b);    /* MMU id */

/* Full teardown (unmap + munmap + GEM close). */
void vpu_buffer_free(struct vpu_buffer *buf);

#endif /* VPU_BUFFER_H */
