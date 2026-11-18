/*
 * vpu_buffer.c - DMA-BUF (GEM dumb) allocation and VPU MMU mapping.
 *
 * Flow per buffer:
 *   DRM_IOCTL_MODE_CREATE_DUMB   -> GEM handle + size
 *   DRM_IOCTL_HANTRO_GETPADDR    -> physical address (paddr) + mem_base
 *   DRM_IOCTL_MODE_MAP_DUMB      -> mmap offset -> CPU virtual address
 *   HANTRO_IOCS_MMU_MEM_MAP      -> map physical address into the VPU MMU
 *                                   (id encodes slice/module type)
 *
 * Teardown reverses the steps: MMU unmap -> munmap -> GEM close.
 *
 * The MMU "id" uses the kernel's bit layout:
 *   SLICE(id)   = id >> 16      (slice/core index)
 *   NODETYPE(id)= (id >> 8)&0xff (NODE_TYPE_DEC / ENC / VCMD)
 * We map decode buffers for slice 0 as NODE_TYPE_DEC | NODE_TYPE_VCMD,
 * matching getMMUcore()'s behaviour (it always ORs in HANTRO_CORE_VCMD).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <libdrm/drm.h>
#include "loongvpu_abi.h"
#include "vpu_buffer.h"

#define VPU_SLICE_ID(slice, nodetype) \
	(((u32)(slice) << 16) | ((u32)(nodetype) << 8))

struct vpu_buffer {
	uint32_t   gem_handle;
	void      *vaddr;
	size_t     size;
	size_t     pitch;
	unsigned long phy_addr;   /* bus / physical address from GETPADDR */
	unsigned long mem_base;
	unsigned long mmu_addr;   /* address in VPU MMU space (filled by map) */
	unsigned int id;          /* MMU id used at map time */
	int         mapped;
	int         drm_fd;       /* not owned; borrowed from driver_data */
};

/* ---------------- allocation ------------------------------------------- */

struct vpu_buffer *vpu_buffer_alloc(int drm_fd, size_t width, size_t height,
                                    unsigned int bpp)
{
	struct vpu_buffer *buf;
	struct drm_mode_create_dumb create = {0};
	struct drm_mode_map_dumb map = {0};
	struct hantro_addrmap addrmap = {0};
	int ret;

	buf = (struct vpu_buffer *)calloc(1, sizeof(*buf));
	if (!buf)
		return NULL;
	buf->drm_fd = drm_fd;

	/* 1) create GEM dumb buffer */
	create.width  = (uint32_t)width;
	create.height = (uint32_t)height;
	create.bpp    = bpp;
	ret = ioctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);
	if (ret < 0) {
		fprintf(stderr, "vpu_buffer: CREATE_DUMB failed: %s\n", strerror(errno));
		goto err_free;
	}
	buf->gem_handle = create.handle;
	buf->size       = create.size;
	buf->pitch      = create.pitch;

	/* 2) get physical address for the MMU */
	addrmap.handle = create.handle;
	ret = ioctl(drm_fd, DRM_IOCTL_HANTRO_GETPADDR, &addrmap);
	if (ret < 0) {
		fprintf(stderr, "vpu_buffer: GETPADDR failed: %s\n", strerror(errno));
		goto err_destroy;
	}
	buf->phy_addr = (unsigned long)addrmap.phy_addr;
	buf->mem_base = (unsigned long)addrmap.mem_base;
	buf->vaddr    = (void *)(uintptr_t)addrmap.vm_addr; /* kernel hint; may be 0 */

	/* 3) map into CPU address space (authoritative CPU pointer) */
	map.handle = create.handle;
	ret = ioctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map);
	if (ret < 0) {
		fprintf(stderr, "vpu_buffer: MAP_DUMB failed: %s\n", strerror(errno));
		goto err_destroy;
	}
	buf->vaddr = mmap(NULL, buf->size, PROT_READ | PROT_WRITE,
	                  MAP_SHARED, drm_fd, (off_t)map.offset);
	if (buf->vaddr == MAP_FAILED) {
		fprintf(stderr, "vpu_buffer: mmap failed: %s\n", strerror(errno));
		buf->vaddr = NULL;
		goto err_destroy;
	}

	return buf;

err_destroy:
	{
		struct drm_gem_close close = {.handle = buf->gem_handle};
		ioctl(drm_fd, DRM_IOCTL_GEM_CLOSE, &close);
	}
err_free:
	free(buf);
	return NULL;
}

/* Allocate a byte-sized buffer. GEM dumb create rounds pitch up, so we ask
 * for width=bytes, height=1, bpp=8; the resulting size is >= bytes. */
struct vpu_buffer *vpu_buffer_alloc_bytes(int drm_fd, size_t bytes)
{
	return vpu_buffer_alloc(drm_fd, bytes ? bytes : 1, 1, 8);
}

/* ---------------- MMU mapping ------------------------------------------ */

int vpu_buffer_mmu_map(struct vpu_buffer *buf, unsigned int slice)
{
	struct addr_desc desc;
	int ret;

	if (!buf)
		return -1;

	memset(&desc, 0, sizeof(desc));
	desc.virtual_address = buf->vaddr;
	desc.bus_address     = buf->phy_addr;
	desc.size            = buf->size;
	desc.id              = VPU_SLICE_ID(slice, NODE_TYPE_DEC | NODE_TYPE_VCMD);

	ret = ioctl(buf->drm_fd, HANTRO_IOCS_MMU_MEM_MAP, &desc);
	if (ret < 0) {
		fprintf(stderr, "vpu_buffer: MMU_MEM_MAP failed: %s\n", strerror(errno));
		return -1;
	}

	buf->mmu_addr = desc.mmu_bus_address; /* kernel writes mapped address back */
	buf->id       = desc.id;
	buf->mapped   = 1;
	return 0;
}

int vpu_buffer_mmu_unmap(struct vpu_buffer *buf)
{
	struct addr_desc desc;
	int ret;

	if (!buf || !buf->mapped)
		return 0;

	memset(&desc, 0, sizeof(desc));
	desc.virtual_address = buf->vaddr;
	desc.bus_address     = buf->phy_addr;
	desc.mmu_bus_address = buf->mmu_addr;
	desc.size            = buf->size;
	desc.id              = buf->id;

	ret = ioctl(buf->drm_fd, HANTRO_IOCS_MMU_MEM_UNMAP, &desc);
	if (ret < 0) {
		fprintf(stderr, "vpu_buffer: MMU_MEM_UNMAP failed: %s\n", strerror(errno));
		return -1;
	}
	buf->mapped = 0;
	return 0;
}

/* ---------------- accessors -------------------------------------------- */

void  *vpu_buffer_vaddr(struct vpu_buffer *b) { return b ? b->vaddr : NULL; }
size_t vpu_buffer_size(struct vpu_buffer *b)  { return b ? b->size : 0; }
unsigned long vpu_buffer_phy(struct vpu_buffer *b) { return b ? b->phy_addr : 0; }
unsigned long vpu_buffer_mmu(struct vpu_buffer *b) { return b ? b->mmu_addr : 0; }
unsigned int  vpu_buffer_id(struct vpu_buffer *b)  { return b ? b->id : 0; }

/* ---------------- teardown --------------------------------------------- */

void vpu_buffer_free(struct vpu_buffer *buf)
{
	struct drm_gem_close close;

	if (!buf)
		return;

	if (buf->mapped)
		vpu_buffer_mmu_unmap(buf);

	if (buf->vaddr)
		munmap(buf->vaddr, buf->size);

	if (buf->gem_handle) {
		close.handle = buf->gem_handle;
		close.pad = 0;
		ioctl(buf->drm_fd, DRM_IOCTL_GEM_CLOSE, &close);
	}

	free(buf);
}
