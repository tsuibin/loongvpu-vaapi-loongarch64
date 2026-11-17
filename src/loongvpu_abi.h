/*
 * loongvpu_abi.h - Userspace copy of the LoongVPU kernel DRM ioctl ABI.
 *
 * This header is deliberately self-contained (only depends on <libdrm/drm.h>
 * and standard integer types) so that the VA-API driver does not need to
 * include kernel headers.  The command numbers and structures MUST stay
 * byte-for-byte identical to the open-source kernel driver:
 *
 *   AOSC-Tracking/loongvpu-kernel-dkms  include/hantro.h  (aosc/v0.1.0-*)
 *   https://github.com/AOSC-Tracking/loongvpu-kernel-dkms
 *
 * Any change here requires a corresponding change in the kernel driver,
 * otherwise the ioctl() calls will fail with ENOTTY/EINVAL.
 */

#ifndef LOONGVPU_ABI_H
#define LOONGVPU_ABI_H

#include <stdint.h>
#include <libdrm/drm.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* ioctl payload structures (mirrors include/hantro.h)                        */
/* ------------------------------------------------------------------------- */

struct hantro_fencecheck {
	uint32_t handle;
	int32_t  ready;
};

struct hantro_domainset {
	uint32_t handle;
	uint32_t writedomain;
	uint32_t readdomain;
};

struct hantro_addrmap {
	uint32_t      handle;
	unsigned long long vm_addr;
	unsigned long long phy_addr;
	unsigned long long mem_base;
};

struct hantro_acquirebuf {
	unsigned long handle;
	unsigned long flags;
	unsigned long timeout;
	unsigned long fence_handle;
};

struct hantro_releasebuf {
	unsigned long fence_handle;
};

struct nor32_parameter {
	uint32_t data;
	uint32_t id;
};

struct core_desc {
	uint32_t  core_id;
	uintptr_t regs;
	uint32_t  size;
	uint32_t  rw;
};

struct addr_desc {
	unsigned long long virt_addr;
	unsigned long long phy_addr;
	uint32_t           size;
	uint32_t           mem_type;
};

/*
 * NOTE: struct cmdbuf_mem_parameter, hantro_metadata_params and several
 * codec-specific descriptors are intentionally omitted here.  They are only
 * needed once real decode submission is implemented; keeping the ABI header
 * minimal reduces the risk of struct-layout drift.  See README "Roadmap".
 */

/* ------------------------------------------------------------------------- */
/* DRM ioctl command numbers (mirrors include/hantro.h)                       */
/* ------------------------------------------------------------------------- */

#define HANTRO_DRM_IOCTL_START DRM_COMMAND_BASE

#define DRM_IOCTL_HANTRO_TESTCMD \
	DRM_IOWR(HANTRO_DRM_IOCTL_START, uint32_t)
#define DRM_IOCTL_HANTRO_GETPADDR \
	DRM_IOWR(HANTRO_DRM_IOCTL_START + 1, struct hantro_addrmap)
#define DRM_IOCTL_HANTRO_HWCFG \
	DRM_IO(HANTRO_DRM_IOCTL_START + 2)
#define DRM_IOCTL_HANTRO_TESTREADY \
	DRM_IOWR(HANTRO_DRM_IOCTL_START + 3, struct hantro_fencecheck)
#define DRM_IOCTL_HANTRO_SETDOMAIN \
	DRM_IOWR(HANTRO_DRM_IOCTL_START + 4, struct hantro_domainset)
#define DRM_IOCTL_HANTRO_ACQUIREBUF \
	DRM_IOWR(HANTRO_DRM_IOCTL_START + 6, struct hantro_acquirebuf)
#define DRM_IOCTL_HANTRO_RELEASEBUF \
	DRM_IOWR(HANTRO_DRM_IOCTL_START + 7, struct hantro_releasebuf)
#define DRM_IOCTL_HANTRO_GETPRIMEADDR \
	DRM_IOWR(HANTRO_DRM_IOCTL_START + 8, unsigned long *)
#define DRM_IOCTL_HANTRO_PTR_PHYADDR \
	DRM_IOWR(HANTRO_DRM_IOCTL_START + 9, unsigned long *)
#define DRM_IOCTL_HANTRO_UPDATE_METADATA \
	DRM_IOWR(HANTRO_DRM_IOCTL_START + 11, struct hantro_metadata_params_stub *)
#define DRM_IOCTL_HANTRO_GET_SLICENUM \
	DRM_IO(HANTRO_DRM_IOCTL_START + 12)
#define DRM_IOCTL_HANTRO_GET_VCMDSUP \
	DRM_IOWR(HANTRO_DRM_IOCTL_START + 13, uint32_t *)
#define DRM_IOCTL_HANTRO_GET_IRQINFO \
	DRM_IOWR(HANTRO_DRM_IOCTL_START + 14, uint32_t *)
#define DRM_IOCTL_HANTRO_GET_PMSUP \
	DRM_IOWR(HANTRO_DRM_IOCTL_START + 15, uint32_t *)

/* stub only keeps the macro compilable; not used by the scaffold */
struct hantro_metadata_params_stub { uint32_t _pad; };

/* Character-device ioctls (magic 'h'), mirrors include/hantro.h */
#define HANTRO_IOC_MAGIC   'h'
#define HANTRO_IOCTL_START HANTRO_IOC_MAGIC

#define HANTRODEC_IOCGHWIOSIZE \
	_IOWR(HANTRO_IOCTL_START, 44, uint32_t *)
#define HANTRODEC_IOCS_DEC_PUSH_REG \
	_IOW(HANTRO_IOCTL_START, 49, struct core_desc *)
#define HANTRODEC_IOCH_DEC_RESERVE \
	_IOW(HANTRO_IOCTL_START, 51, struct nor32_parameter *)
#define HANTRODEC_IOCT_DEC_RELEASE \
	_IO(HANTRO_IOCTL_START, 52)
#define HANTRODEC_IOCX_DEC_WAIT \
	_IOW(HANTRO_IOCTL_START, 55, struct core_desc *)
#define HANTRODEC_IOX_IOCX_POLL \
	_IOWR(HANTRO_IOCTL_START, 66, uint32_t *)

#define HANTRO_IOCS_MMU_MEM_MAP \
	_IOWR(HANTRO_IOCTL_START, 101, struct addr_desc *)
#define HANTRO_IOCS_MMU_MEM_UNMAP \
	_IOWR(HANTRO_IOCTL_START, 102, struct addr_desc *)
#define HANTRO_IOCS_MMU_FLUSH \
	_IOW(HANTRO_IOCTL_START, 103, struct addr_desc *)

#ifdef __cplusplus
}
#endif

#endif /* LOONGVPU_ABI_H */
