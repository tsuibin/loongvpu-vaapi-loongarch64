/*
 * loongvpu_abi.h - Userspace copy of the LoongVPU kernel DRM / VCMD ioctl ABI.
 *
 * Kept byte-for-byte identical to the open-source kernel driver:
 *   AOSC-Tracking/loongvpu-kernel-dkms (aosc/v0.1.0-lnd25.1-rc1.7)
 *     include/hantro.h      - ioctl commands, hantro_addrmap, ioctl_id
 *     include/hantrommu.h   - struct addr_desc
 *     include/hantro_vcmd.h - cmdbuf_mem_parameter, exchange_parameter, opcodes
 *
 * If any struct here diverges from the kernel, ioctl() will fail with
 * EINVAL/ENOTTY (Gitea issue-style note: keep the two trees in sync).
 */

#ifndef LOONGVPU_ABI_H
#define LOONGVPU_ABI_H

#include <stdint.h>
#include <stddef.h>
#include <sys/ioctl.h>
#include <libdrm/drm.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

/* ------------------------------------------------------------------------- */
/* ioctl id helpers (include/hantro.h)                                        */
/* ------------------------------------------------------------------------- */
#define NODETYPE(id) (((u32)(id) >> 8) & 0xff)
#define SLICE(id)    ((u32)(id) >> 16)

#define NODE_TYPE_DEC   (1u << 0)
#define NODE_TYPE_ENC   (1u << 1)
#define NODE_TYPE_VCMD  (1u << 2)

/* ------------------------------------------------------------------------- */
/* DRM ioctl payload structures                                               */
/* ------------------------------------------------------------------------- */

struct hantro_fencecheck { uint32_t handle; int32_t ready; };
struct hantro_domainset  { uint32_t handle; uint32_t writedomain; uint32_t readdomain; };

struct hantro_addrmap {
	uint32_t      handle;
	unsigned long long vm_addr;
	unsigned long long phy_addr;
	unsigned long long mem_base;
};

struct hantro_acquirebuf {
	unsigned long handle, flags, timeout, fence_handle;
};
struct hantro_releasebuf { unsigned long fence_handle; };
struct nor32_parameter   { u32 data; u32 id; };

struct core_desc {
	u32      core_id;
	uintptr_t regs;
	u32      size;
	u32      rw;
};

/* include/hantrommu.h - NOTE: exact field names/types matter. */
struct addr_desc {
	void          *virtual_address;
	unsigned long  bus_address;
	unsigned long  mmu_bus_address;
	unsigned long  size;
	unsigned int   id;
};

/* include/hantro_vcmd.h */
enum vcmd_module_type {
	VCMD_TYPE_ENCODER = 0,
	VCMD_TYPE_CUTREE,
	VCMD_TYPE_DECODER,
	VCMD_TYPE_JPEG_ENCODER,
	VCMD_TYPE_JPEG_DECODER,
	MAX_VCMD_TYPE
};

struct noncache_mem {
	u32          *virtualAddress;
	unsigned long busAddress;
	size_t        mmu_bus_address;
	u32           size;
	u16           cmdbuf_id;
};

struct cmdbuf_id_parameter { u16 cmdbuf_id; u32 id; };

struct cmdbuf_mem_parameter {
	u32   *virt_cmdbuf_addr;
	size_t phy_cmdbuf_addr;
	size_t mmu_phy_cmdbuf_addr;
	u32    cmdbuf_total_size;
	u16    cmdbuf_unit_size;
	u32   *virt_status_cmdbuf_addr;
	size_t phy_status_cmdbuf_addr;
	size_t mmu_phy_status_cmdbuf_addr;
	u32    status_cmdbuf_total_size;
	u16    status_cmdbuf_unit_size;
	size_t base_ddr_addr;
	u32   *reg_virt_addr;
	size_t reg_phy_addr;
	size_t reg_hw_addr;
	u32    reg_total_size;
	u32    reg_unit_size;
	u32    id;
};

struct exchange_parameter {
	u32   interrupt_ctrl;
	u32   executing_time;
	u32   client_type;
	u16   module_type;
	u16   cmdbuf_size;
	u16   priority;
	u16   cmdbuf_id;
	u16   core_id;
	u8    cmdbuf_reserve;
	u32   id;
	u32   req_cores_info;
	u16   core_mask;
	u16   has_end_cmd;
};

/* VCMD opcodes (include/hantro_vcmd.h) */
#define OPCODE_WREG   (0x01u << 27)
#define OPCODE_END    (0x02u << 27)
#define OPCODE_NOP    (0x03u << 27)
#define OPCODE_RREG   (0x16u << 27)
#define OPCODE_INT    (0x18u << 27)
#define OPCODE_JMP    (0x19u << 27)
#define OPCODE_STALL  (0x09u << 27)
#define OPCODE_CLRINT (0x1au << 27)

#define JMP_IE_1      (0x01u << 25)
#define JMP_RDY_1     (0x01u << 26)

#define CMDBUF_MAX_SIZE      (512 * 4 * 4)
#define VCMD_HW_ID            0x4342
#define ANY_CMDBUF_ID         0xFFFF

/* ------------------------------------------------------------------------- */
/* DRM ioctl command numbers                                                  */
/* ------------------------------------------------------------------------- */
#define HANTRO_DRM_IOCTL_START DRM_COMMAND_BASE

#define DRM_IOCTL_HANTRO_TESTCMD \
	DRM_IOWR(HANTRO_DRM_IOCTL_START, u32)
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
#define DRM_IOCTL_HANTRO_GET_SLICENUM \
	DRM_IO(HANTRO_DRM_IOCTL_START + 12)
#define DRM_IOCTL_HANTRO_GET_VCMDSUP \
	DRM_IOWR(HANTRO_DRM_IOCTL_START + 13, u32 *)
#define DRM_IOCTL_HANTRO_GET_IRQINFO \
	DRM_IOWR(HANTRO_DRM_IOCTL_START + 14, u32 *)
#define DRM_IOCTL_HANTRO_GET_PMSUP \
	DRM_IOWR(HANTRO_DRM_IOCTL_START + 15, u32 *)

/* Character device ioctls (magic 'h') */
#define HANTRO_IOC_MAGIC    'h'
#define HANTRO_IOCTL_START  HANTRO_IOC_MAGIC

#define HANTRODEC_IOCGHWIOSIZE \
	_IOWR(HANTRO_IOCTL_START, 44, u32 *)
#define HANTRODEC_IOCS_DEC_PUSH_REG \
	_IOW(HANTRO_IOCTL_START, 49, struct core_desc *)
#define HANTRODEC_IOCH_DEC_RESERVE \
	_IOW(HANTRO_IOCTL_START, 51, struct nor32_parameter *)
#define HANTRODEC_IOCT_DEC_RELEASE \
	_IO(HANTRO_IOCTL_START, 52)
#define HANTRODEC_IOCX_DEC_WAIT \
	_IOW(HANTRO_IOCTL_START, 55, struct core_desc *)
#define HANTRODEC_IOX_IOCX_POLL \
	_IOWR(HANTRO_IOCTL_START, 66, u32 *)

#define HANTRO_IOCS_MMU_MEM_MAP \
	_IOWR(HANTRO_IOCTL_START, 101, struct addr_desc *)
#define HANTRO_IOCS_MMU_MEM_UNMAP \
	_IOWR(HANTRO_IOCTL_START, 102, struct addr_desc *)
#define HANTRO_IOCS_MMU_FLUSH \
	_IOW(HANTRO_IOCTL_START, 103, struct addr_desc *)

#define VCMD_IOC_START _IO(HANTRO_IOCTL_START, 110)
#define VCMD_IOCH_GET_CMDBUF_PARAMETER \
	_IOWR(HANTRO_IOCTL_START, 111, struct cmdbuf_mem_parameter *)
#define VCMD_IOCH_GET_CMDBUF_POOL_SIZE \
	_IOWR(HANTRO_IOCTL_START, 112, unsigned long)
#define VCMD_IOCH_SET_CMDBUF_POOL_BASE \
	_IOWR(HANTRO_IOCTL_START, 113, unsigned long)
#define VCMD_IOCH_GET_VCMD_PARAMETER \
	_IOWR(HANTRO_IOCTL_START, 114, void *)
#define VCMD_IOCH_RESERVE_CMDBUF \
	_IOWR(HANTRO_IOCTL_START, 115, struct exchange_parameter *)
#define VCMD_IOCH_LINK_RUN_CMDBUF \
	_IOR(HANTRO_IOCTL_START, 116, struct exchange_parameter *)
#define VCMD_IOCH_WAIT_CMDBUF \
	_IOR(HANTRO_IOCTL_START, 117, struct cmdbuf_id_parameter *)
#define VCMD_IOCH_RELEASE_CMDBUF \
	_IOR(HANTRO_IOCTL_START, 118, struct cmdbuf_id_parameter *)
#define VCMD_IOCH_POLLING_CMDBUF \
	_IOR(HANTRO_IOCTL_START, 119, u32 *)

#ifdef __cplusplus
}
#endif

#endif /* LOONGVPU_ABI_H */
