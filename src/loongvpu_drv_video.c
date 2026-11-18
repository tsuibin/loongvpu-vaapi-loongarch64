/*
 * loongvpu_drv_video.c -- open-source VA-API driver scaffold for LoongVPU.
 *
 * This is a WORKING SKELETON, not a complete hardware decoder.  It implements
 * the libva entry point and the mandatory driver hooks so that the driver can
 * be discovered and loaded by libva, advertises the codecs the LoongVPU VPU
 * supports (H.264/H.265/AV1 decode, H.264/H.265 encode), establishes a DRM
 * connection to the kernel driver, and provides object management stubs that
 * can be filled in with real register-stream submission later.
 *
 * Kernel ABI: AOSC-Tracking/loongvpu-kernel-dkms (open source, GPL).
 * This file is licensed under MIT so it can be freely reused.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

#include <va/va.h>
#include <va/va_backend.h>
#include <va/va_dec_hevc.h>

#include "loongvpu_abi.h"
#include "vpu_buffer.h"
#include "vcmd.h"
#include "h264_decode_params.h"
#include "hevc_decode_params.h"
#include "work_buffer.h"
#include "hevc_work_buffer.h"

#define LOONGVPU_DRIVER_NAME   "LoongVPU (open-source scaffold)"
#define LOONGVPU_VENDOR        "loongvpu-vaapi"
#define LOONGVPU_RENDER_NODE   "/dev/dri/renderD128"

#define MAX_SURFACES 64
#define MAX_BUFFERS  64
#define VCMD_CMD_WORDS 256

/* One tracked VA surface: maps a VASurfaceID to its VPU buffer. */
struct tracked_surface {
	VASurfaceID       id;
	struct vpu_buffer *buf;
	int               in_use;
};

/* One tracked VA buffer (slice data / params).  Buffers that the VPU reads
 * directly (bitstream) also own a GEM/DMA-BUF + MMU mapping; vpu_buf is NULL
 * for host-only parameter buffers. */
struct tracked_buffer {
	VABufferID id;
	VABufferType type;
	void       *data;          /* CPU pointer (vpu_buffer vaddr when vpu_buf) */
	size_t      size;
	int         in_use;
	struct vpu_buffer *vpu_buf; /* non-NULL for VPU-visible buffers */
};

/* Driver-private data, reachable via ctx->pDriverData. */
struct loongvpu_driver_data {
	int            drm_fd;          /* fd of /dev/dri/renderD* */
	int            vcmd_supported;  /* result of GET_VCMDSUP */
	int            pm_supported;    /* result of GET_PMSUP */
	unsigned int   asic_id;
	char           render_node[64];

	struct vpu_vcmd      *vcmd;                   /* VCMD submission ctx */
	struct tracked_surface surfaces[MAX_SURFACES];
	struct tracked_buffer  buffers[MAX_BUFFERS];
	VASurfaceID            current_target;       /* from vaBeginPicture */
	uint32_t               cmd[VCMD_CMD_WORDS];  /* scratch command buffer */

	/* codec routing: config/profile tables and active context profile */
	VAConfigID   config_ids[MAX_BUFFERS];
	VAProfile    config_profiles[MAX_BUFFERS];
	int          config_count;
	VAContextID  context_ids[MAX_BUFFERS];
	VAProfile    context_profiles[MAX_BUFFERS];
	int          context_count;
	VAProfile    current_profile;                /* set in vaBeginPicture */

	/* Decoder work buffers (allocated lazily on first decode). */
	struct vpu_buffer *rlc_vlc_work;   /* RLC/VLC + CABAC/POC work area */
	struct vpu_buffer *mb_ctrl_work;   /* MB control work area */
	struct vpu_buffer *scale_work;     /* HEVC scaling-list work area (1024 B) */
	unsigned int       work_mbs;       /* macroblock count work was sized for */
};

/* ------------------------------------------------------------------------- */
/* DRM backend                                                                */
/* ------------------------------------------------------------------------- */

/* Find the VPU DRM render node. The GPU (LG200) and VPU can both expose
 * /dev/dri/renderD* nodes; hardcoding renderD128 is wrong. The kernel Hantro
 * driver answers DRM_IOCTL_HANTRO_GET_VCMDSUP on a VPU render node and rejects
 * it (EINVAL/ENOTTY) on non-VPU nodes, so we probe each node with that ioctl.
 * LOONGVPU_RENDER_NODE env var forces a specific node for debugging. */
static int find_vpu_render_node(struct loongvpu_driver_data *drv)
{
	int i, fd, r;
	const char *env = getenv("LOONGVPU_RENDER_NODE");
	char path[64];
	uint32_t sup = 0;

	if (env && *env) {
		fd = open(env, O_RDWR | O_CLOEXEC);
		if (fd < 0) {
			fprintf(stderr, "loongvpu: cannot open forced node %s: %s\n",
			        env, strerror(errno));
			return -1;
		}
		r = ioctl(fd, DRM_IOCTL_HANTRO_GET_VCMDSUP, &sup);
		if (r < 0) {
			fprintf(stderr, "loongvpu: forced node %s is not a VPU "
			                "(GET_VCMDSUP: %s)\n", env, strerror(errno));
			close(fd);
			return -1;
		}
		snprintf(path, sizeof(path), "%s", env);
		fprintf(stderr, "loongvpu: using forced VPU node %s\n", path);
	} else {
		for (i = 0; i < 256; i++) {
			snprintf(path, sizeof(path), "/dev/dri/renderD%d", i);
			fd = open(path, O_RDWR | O_CLOEXEC);
			if (fd < 0)
				continue;
			r = ioctl(fd, DRM_IOCTL_HANTRO_GET_VCMDSUP, &sup);
			if (r == 0)
				break;            /* this is the VPU node */
			close(fd);
			fd = -1;
		}
		if (fd < 0) {
			fprintf(stderr, "loongvpu: no LoongVPU render node found "
			                "(probed /dev/dri/renderD0..255); is the "
			                "loongvpu kernel module loaded? Override "
			                "with LOONGVPU_RENDER_NODE=/dev/dri/renderD<N>\n");
			return -1;
		}
	}

	snprintf(drv->render_node, sizeof(drv->render_node), "%s", path);
	drv->vcmd_supported = (int)sup;
	return fd;
}

/* Open the discovered render node and query basic capabilities.
 * Failures are soft: libva should still load the driver with no VPU. */
static void loongvpu_drm_init(struct loongvpu_driver_data *drv)
{
	int fd;
	uint32_t sup = 0;

	drv->drm_fd = -1;
	fd = find_vpu_render_node(drv);
	if (fd < 0)
		return;

	fprintf(stderr, "loongvpu: VPU render node = %s\n", drv->render_node);

	/* These ioctls are advisory; do not fail driver init if unsupported. */
	sup = 0;
	if (ioctl(fd, DRM_IOCTL_HANTRO_GET_PMSUP, &sup) == 0)
		drv->pm_supported = (int)sup;

	drv->drm_fd = fd;

	/* Open the VCMD command-buffer pool (soft-fail if unsupported). */
	drv->vcmd = vpu_vcmd_open(fd);
	if (drv->vcmd)
		fprintf(stderr, "loongvpu: VCMD pool ready\n");
	else
		fprintf(stderr, "loongvpu: VCMD pool unavailable (decode disabled)\n");
}

static void loongvpu_drm_fini(struct loongvpu_driver_data *drv)
{
	int i;

	if (!drv)
		return;

	/* Free all tracked surfaces (unmap + munmap + GEM close). */
	for (i = 0; i < MAX_SURFACES; i++) {
		if (drv->surfaces[i].in_use) {
			vpu_buffer_free(drv->surfaces[i].buf);
			drv->surfaces[i].in_use = 0;
		}
	}

	/* Free all tracked VA buffers.  Buffers with a VPU mapping are freed
	 * through vpu_buffer_free (which unmaps + closes GEM); host-only
	 * parameter buffers use plain free(). */
	for (i = 0; i < MAX_BUFFERS; i++) {
		if (drv->buffers[i].in_use) {
			if (drv->buffers[i].vpu_buf)
				vpu_buffer_free(drv->buffers[i].vpu_buf);
			else
				free(drv->buffers[i].data);
			drv->buffers[i].vpu_buf = NULL;
			drv->buffers[i].data = NULL;
			drv->buffers[i].in_use = 0;
		}
	}

	/* Free decoder work buffers. */
	if (drv->rlc_vlc_work) {
		vpu_buffer_free(drv->rlc_vlc_work);
		drv->rlc_vlc_work = NULL;
	}
	if (drv->mb_ctrl_work) {
		vpu_buffer_free(drv->mb_ctrl_work);
		drv->mb_ctrl_work = NULL;
	}
	if (drv->scale_work) {
		vpu_buffer_free(drv->scale_work);
		drv->scale_work = NULL;
	}

	if (drv->vcmd) {
		vpu_vcmd_close(drv->vcmd);
		drv->vcmd = NULL;
	}

	if (drv->drm_fd >= 0) {
		close(drv->drm_fd);
		drv->drm_fd = -1;
	}
}

/* ------------------------------------------------------------------------- */
/* Mandatory libva hooks                                                      */
/* ------------------------------------------------------------------------- */

static VAStatus loongvpu_Terminate(VADriverContextP ctx)
{
	struct loongvpu_driver_data *drv;

	if (!ctx)
		return VA_STATUS_ERROR_INVALID_CONTEXT;

	drv = (struct loongvpu_driver_data *)ctx->pDriverData;
	if (drv) {
		loongvpu_drm_fini(drv);
		free(drv);
		ctx->pDriverData = NULL;
	}
	free(ctx->vtable);
	ctx->vtable = NULL;

	return VA_STATUS_SUCCESS;
}

/* Profiles advertised to libva.  These mirror what the closed-source driver
 * exposes and what the VPU silicon supports per the AOSC release notes. */
static const VAProfile supported_profiles[] = {
	VAProfileH264ConstrainedBaseline,
	VAProfileH264Main,
	VAProfileH264High,
	VAProfileHEVCMain,
	VAProfileHEVCMain10,
	VAProfileAV1Profile0,
};

static VAStatus loongvpu_QueryConfigProfiles(
	VADriverContextP ctx,
	VAProfile *profile_list,
	int *num_profiles)
{
	unsigned int i;

	(void)ctx;
	if (!profile_list || !num_profiles)
		return VA_STATUS_ERROR_INVALID_PARAMETER;

	for (i = 0; i < sizeof(supported_profiles)/sizeof(supported_profiles[0]); i++)
		profile_list[i] = supported_profiles[i];

	*num_profiles = (int)(sizeof(supported_profiles)/sizeof(supported_profiles[0]));
	return VA_STATUS_SUCCESS;
}

static VAStatus loongvpu_QueryConfigEntrypoints(
	VADriverContextP ctx,
	VAProfile profile,
	VAEntrypoint *entrypoint_list,
	int *num_entrypoints)
{
	unsigned int i;
	int n = 0;

	(void)ctx;
	if (!entrypoint_list || !num_entrypoints)
		return VA_STATUS_ERROR_INVALID_PARAMETER;

	/* Validate the profile is one we advertise. */
	for (i = 0; i < sizeof(supported_profiles)/sizeof(supported_profiles[0]); i++) {
		if (supported_profiles[i] == profile)
			break;
	}
	if (i == sizeof(supported_profiles)/sizeof(supported_profiles[0]))
		return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;

	entrypoint_list[n++] = VAEntrypointVLD;   /* hardware decode */

	/* The VPU supports H.264/H.265 encode (not AV1 encode). */
	if (profile == VAProfileH264ConstrainedBaseline || profile == VAProfileH264Main ||
	    profile == VAProfileH264High || profile == VAProfileHEVCMain ||
	    profile == VAProfileHEVCMain10)
		entrypoint_list[n++] = VAEntrypointEncSlice;

	*num_entrypoints = n;
	return VA_STATUS_SUCCESS;
}

static VAStatus loongvpu_GetConfigAttributes(
	VADriverContextP ctx,
	VAProfile profile,
	VAEntrypoint entrypoint,
	VAConfigAttrib *attrib_list,
	int num_attribs)
{
	int i;

	(void)ctx; (void)profile; (void)entrypoint;
	if (!attrib_list)
		return VA_STATUS_ERROR_INVALID_PARAMETER;

	for (i = 0; i < num_attribs; i++) {
		switch (attrib_list[i].type) {
		case VAConfigAttribRTFormat:
			attrib_list[i].value = VA_RT_FORMAT_YUV420;
			break;
		case VAConfigAttribRateControl:
			attrib_list[i].value = VA_RC_CBR | VA_RC_VBR | VA_RC_CQP;
			break;
		case VAConfigAttribDecSliceMode:
			attrib_list[i].value = VA_DEC_SLICE_MODE_NORMAL;
			break;
		default:
			attrib_list[i].value = VA_ATTRIB_NOT_SUPPORTED;
			break;
		}
	}
	return VA_STATUS_SUCCESS;
}

/* Simple monotonic ID allocator for skeleton objects. */
static int next_id(struct loongvpu_driver_data *drv)
{
	static int id = 1;
	(void)drv;
	return id++;
}

static VAStatus loongvpu_CreateConfig(
	VADriverContextP ctx,
	VAProfile profile,
	VAEntrypoint entrypoint,
	VAConfigAttrib *attrib_list,
	int num_attribs,
	VAConfigID *config_id)
{
	struct loongvpu_driver_data *drv = ctx ? ctx->pDriverData : NULL;
	int id;

	(void)entrypoint; (void)attrib_list; (void)num_attribs;
	if (!config_id || !drv)
		return VA_STATUS_ERROR_INVALID_PARAMETER;
	if (drv->config_count >= MAX_BUFFERS)
		return VA_STATUS_ERROR_ALLOCATION_FAILED;

	id = next_id(drv);
	drv->config_ids[drv->config_count] = (VAConfigID)id;
	drv->config_profiles[drv->config_count] = profile;
	drv->config_count++;
	*config_id = (VAConfigID)id;
	return VA_STATUS_SUCCESS;
}

/* Look up the profile recorded for a config; default H.264 if unknown. */
static VAProfile config_profile(struct loongvpu_driver_data *drv, VAConfigID id)
{
	int i;
	for (i = 0; i < drv->config_count; i++)
		if (drv->config_ids[i] == id)
			return drv->config_profiles[i];
	return VAProfileH264High;
}

static VAStatus loongvpu_DestroyConfig(VADriverContextP ctx, VAConfigID config_id)
{
	(void)ctx; (void)config_id;
	return VA_STATUS_SUCCESS;
}

static VAStatus loongvpu_QueryConfigAttributes(
	VADriverContextP ctx, VAConfigID config_id,
	VAProfile *profile, VAEntrypoint *entrypoint,
	VAConfigAttrib *attrib_list, int *num_attribs)
{
	struct loongvpu_driver_data *drv = ctx ? ctx->pDriverData : NULL;

	if (profile) *profile = drv ? config_profile(drv, config_id) : VAProfileH264High;
	if (entrypoint) *entrypoint = VAEntrypointVLD;
	if (num_attribs) *num_attribs = 0;
	(void)attrib_list;
	return VA_STATUS_SUCCESS;
}

/* Find a tracked surface by id; NULL if not found. */
static struct tracked_surface *find_surface(struct loongvpu_driver_data *drv,
                                            VASurfaceID id)
{
	int i;
	for (i = 0; i < MAX_SURFACES; i++)
		if (drv->surfaces[i].in_use && drv->surfaces[i].id == id)
			return &drv->surfaces[i];
	return NULL;
}

/* Find a tracked buffer by id; NULL if not found. */
static struct tracked_buffer *find_buffer(struct loongvpu_driver_data *drv,
                                          VABufferID id)
{
	int i;
	for (i = 0; i < MAX_BUFFERS; i++)
		if (drv->buffers[i].in_use && drv->buffers[i].id == id)
			return &drv->buffers[i];
	return NULL;
}

static VAStatus loongvpu_CreateSurfaces(
	VADriverContextP ctx, int width, int height, int format,
	int num_surfaces, VASurfaceID *surfaces)
{
	struct loongvpu_driver_data *drv = ctx ? ctx->pDriverData : NULL;
	int i, created = 0;

	if (!surfaces || num_surfaces <= 0 || !drv)
		return VA_STATUS_ERROR_INVALID_PARAMETER;
	(void)format;

	/* Allocate a GEM dumb buffer per surface and map it into the VPU MMU.
	 * YUV420 needs 1.5 bytes/pixel; we request bpp=8 and 1.5x height to
	 * keep it simple (the GEM allocator just needs a byte size). */
	for (i = 0; i < MAX_SURFACES && created < num_surfaces; i++) {
		struct vpu_buffer *buf;
		int id;

		if (drv->surfaces[i].in_use)
			continue;

		buf = vpu_buffer_alloc(drv->drm_fd, (size_t)width,
		                       (size_t)((height * 3) / 2), 8);
		if (!buf) {
			fprintf(stderr, "loongvpu: surface alloc failed at %dx%d\n",
			        width, height);
			break;
		}
		if (vpu_buffer_mmu_map(buf, 0) < 0) {
			fprintf(stderr, "loongvpu: surface MMU map failed\n");
			vpu_buffer_free(buf);
			break;
		}

		id = next_id(drv);
		drv->surfaces[i].id     = (VASurfaceID)id;
		drv->surfaces[i].buf    = buf;
		drv->surfaces[i].in_use = 1;
		surfaces[created++]     = (VASurfaceID)id;
	}

	if (created < num_surfaces)
		/* VA has no partial-count return; report success for what we
		 * managed to create, or failure if none. */
		return created ? VA_STATUS_SUCCESS : VA_STATUS_ERROR_ALLOCATION_FAILED;

	return VA_STATUS_SUCCESS;
}

static VAStatus loongvpu_DestroySurfaces(
	VADriverContextP ctx, VASurfaceID *surface_list, int num_surfaces)
{
	struct loongvpu_driver_data *drv = ctx ? ctx->pDriverData : NULL;
	int j;

	if (!drv || !surface_list)
		return VA_STATUS_ERROR_INVALID_PARAMETER;

	for (j = 0; j < num_surfaces; j++) {
		struct tracked_surface *s = find_surface(drv, surface_list[j]);
		if (s) {
			vpu_buffer_free(s->buf);
			s->in_use = 0;
		}
	}
	return VA_STATUS_SUCCESS;
}

static VAStatus loongvpu_CreateContext(
	VADriverContextP ctx, VAConfigID config_id,
	int picture_width, int picture_height, int flag,
	VASurfaceID *render_targets, int num_render_targets,
	VAContextID *context)
{
	struct loongvpu_driver_data *drv = ctx ? ctx->pDriverData : NULL;
	int id;

	(void)picture_width; (void)picture_height; (void)flag;
	(void)render_targets; (void)num_render_targets;
	if (!context || !drv)
		return VA_STATUS_ERROR_INVALID_PARAMETER;
	if (drv->context_count >= MAX_BUFFERS)
		return VA_STATUS_ERROR_ALLOCATION_FAILED;

	id = next_id(drv);
	drv->context_ids[drv->context_count] = (VAContextID)id;
	drv->context_profiles[drv->context_count] = config_profile(drv, config_id);
	drv->context_count++;
	*context = (VAContextID)id;
	return VA_STATUS_SUCCESS;
}

/* Look up the profile recorded for a context. */
static VAProfile context_profile(struct loongvpu_driver_data *drv, VAContextID id)
{
	int i;
	for (i = 0; i < drv->context_count; i++)
		if (drv->context_ids[i] == id)
			return drv->context_profiles[i];
	return VAProfileH264High;
}

static VAStatus loongvpu_DestroyContext(VADriverContextP ctx, VAContextID context)
{
	(void)ctx; (void)context;
	return VA_STATUS_SUCCESS;
}

/* Slice data is read directly by the VPU: back it with a GEM/DMA-BUF and
 * map it into the VPU MMU.  If the render node is unavailable (build machine
 * or non-LoongGPU), fall back to host memory so libva still functions. */
static VAStatus alloc_tracked_buffer(struct loongvpu_driver_data *drv,
                                     struct tracked_buffer *b,
                                     size_t total, void *data)
{
	if (b->type == VASliceDataBufferType && drv->drm_fd >= 0) {
		struct vpu_buffer *vb = vpu_buffer_alloc_bytes(drv->drm_fd, total);
		if (vb && vpu_buffer_mmu_map(vb, 0) == 0) {
			b->vpu_buf = vb;
			b->data = vpu_buffer_vaddr(vb);
		} else {
			if (vb) vpu_buffer_free(vb);
			vb = NULL;
		}
	}
	if (!b->data) {
		b->data = calloc(1, total ? total : 1);
		if (!b->data)
			return VA_STATUS_ERROR_ALLOCATION_FAILED;
	}
	if (data && total)
		memcpy(b->data, data, total);
	return VA_STATUS_SUCCESS;
}

static VAStatus loongvpu_CreateBuffer(
	VADriverContextP ctx, VAContextID context, VABufferType type,
	unsigned int size, unsigned int num_elements, void *data,
	VABufferID *buf_id)
{
	struct loongvpu_driver_data *drv = ctx ? ctx->pDriverData : NULL;
	struct tracked_buffer *b = NULL;
	size_t total = (size_t)size * (num_elements ? num_elements : 1);
	VAStatus st;
	int i;

	(void)context;
	if (!buf_id || !drv)
		return VA_STATUS_ERROR_INVALID_PARAMETER;

	for (i = 0; i < MAX_BUFFERS; i++)
		if (!drv->buffers[i].in_use) { b = &drv->buffers[i]; break; }
	if (!b)
		return VA_STATUS_ERROR_ALLOCATION_FAILED;

	memset(b, 0, sizeof(*b));
	st = alloc_tracked_buffer(drv, b, total, data);
	if (st != VA_STATUS_SUCCESS)
		return st;

	b->id     = (VABufferID)next_id(drv);
	b->type   = type;
	b->size   = total;
	b->in_use = 1;
	*buf_id   = b->id;
	return VA_STATUS_SUCCESS;
}

static VAStatus loongvpu_MapBuffer(VADriverContextP ctx, VABufferID buf_id, void **pbuf)
{
	struct loongvpu_driver_data *drv = ctx ? ctx->pDriverData : NULL;
	struct tracked_buffer *b;

	if (!pbuf || !drv)
		return VA_STATUS_ERROR_INVALID_PARAMETER;
	b = find_buffer(drv, buf_id);
	if (!b)
		return VA_STATUS_ERROR_INVALID_BUFFER;
	*pbuf = b->data;
	return VA_STATUS_SUCCESS;
}

static VAStatus loongvpu_UnmapBuffer(VADriverContextP ctx, VABufferID buf_id)
{
	(void)ctx; (void)buf_id;
	return VA_STATUS_SUCCESS;
}

static VAStatus loongvpu_DestroyBuffer(VADriverContextP ctx, VABufferID buffer_id)
{
	struct loongvpu_driver_data *drv = ctx ? ctx->pDriverData : NULL;
	struct tracked_buffer *b;

	if (!drv)
		return VA_STATUS_ERROR_INVALID_CONTEXT;
	b = find_buffer(drv, buffer_id);
	if (!b)
		return VA_STATUS_ERROR_INVALID_BUFFER;
	if (b->vpu_buf) {
		vpu_buffer_free(b->vpu_buf);
		b->vpu_buf = NULL;
	} else {
		free(b->data);
	}
	b->data = NULL;
	b->in_use = 0;
	return VA_STATUS_SUCCESS;
}

static VAStatus loongvpu_BeginPicture(
	VADriverContextP ctx, VAContextID context, VASurfaceID render_target)
{
	struct loongvpu_driver_data *drv = ctx ? ctx->pDriverData : NULL;

	if (!drv)
		return VA_STATUS_ERROR_INVALID_CONTEXT;
	drv->current_target  = render_target;
	drv->current_profile = context_profile(drv, context);
	return VA_STATUS_SUCCESS;
}

static VAStatus loongvpu_RenderPicture(
	VADriverContextP ctx, VAContextID context,
	VABufferID *buffers, int num_buffers)
{
	struct loongvpu_driver_data *drv = ctx ? ctx->pDriverData : NULL;

	(void)context;
	if (!drv || !buffers || num_buffers <= 0)
		return VA_STATUS_ERROR_INVALID_PARAMETER;
	/* Buffers are now stored in the tracked table; EndPicture consumes
	 * the first VASliceDataBufferType as the bitstream. */
	return VA_STATUS_SUCCESS;
}

/* Lazy work-buffer allocator. Sizes follow the VC8000D SDK H264 ASIC buffer
 * requirements (h264hwd_asic.h): RLC/VLC = 880 bytes/MB plus CABAC init
 * (3680), scaling lists (224) and POC (136); MB control = 8 bytes/MB. Buffers
 * are (re)allocated when the frame's macroblock count grows. */
static VAStatus ensure_work_buffers(struct loongvpu_driver_data *drv,
                                    unsigned int mbs)
{
	size_t rlc_size, mb_size;

	if (mbs == 0)
		mbs = 1;
	if (drv->rlc_vlc_work && mbs <= drv->work_mbs)
		return VA_STATUS_SUCCESS;

	if (drv->rlc_vlc_work) vpu_buffer_free(drv->rlc_vlc_work);
	if (drv->mb_ctrl_work) vpu_buffer_free(drv->mb_ctrl_work);
	drv->rlc_vlc_work = NULL;
	drv->mb_ctrl_work = NULL;

	rlc_size = (size_t)880 * mbs + 3680 + 224 + 136;
	mb_size  = (size_t)8 * mbs;

	if (drv->drm_fd >= 0) {
		drv->rlc_vlc_work = vpu_buffer_alloc_bytes(drv->drm_fd, rlc_size);
		drv->mb_ctrl_work = vpu_buffer_alloc_bytes(drv->drm_fd, mb_size);
		if (drv->rlc_vlc_work)
			vpu_buffer_mmu_map(drv->rlc_vlc_work, 0);
		if (drv->mb_ctrl_work)
			vpu_buffer_mmu_map(drv->mb_ctrl_work, 0);
	}
	if (!drv->rlc_vlc_work || !drv->mb_ctrl_work) {
		/* No render node: decode is not possible, but keep the driver
		 * loadable. EndPicture will fall back to zero addresses. */
		fprintf(stderr, "loongvpu: work buffers unavailable (no VPU)\n");
		if (drv->rlc_vlc_work) vpu_buffer_free(drv->rlc_vlc_work);
		if (drv->mb_ctrl_work) vpu_buffer_free(drv->mb_ctrl_work);
		drv->rlc_vlc_work = NULL;
		drv->mb_ctrl_work = NULL;
		return VA_STATUS_SUCCESS;
	}
	drv->work_mbs = mbs;
	return VA_STATUS_SUCCESS;
}

/* H.264 decode submission (vaEndPicture for AVC configs). */
static VAStatus end_picture_h264(VADriverContextP ctx, VAContextID context)
{
	struct loongvpu_driver_data *drv = ctx ? ctx->pDriverData : NULL;
	struct tracked_surface *out;
	struct h264_decode_params prm;
	struct tracked_buffer *pic = NULL, *slice = NULL, *bsbuf = NULL, *iq = NULL;
	int i, words;

	(void)context;
	if (!drv || !drv->vcmd)
		return VA_STATUS_ERROR_INVALID_CONTEXT;

	out = find_surface(drv, drv->current_target);
	if (!out)
		return VA_STATUS_ERROR_INVALID_SURFACE;

	/* Collect the H.264 VA buffers by type. */
	for (i = 0; i < MAX_BUFFERS; i++) {
		struct tracked_buffer *b = &drv->buffers[i];
		if (!b->in_use)
			continue;
		if (b->type == VAPictureParameterBufferType && !pic)
			pic = b;
		else if (b->type == VASliceParameterBufferType && !slice)
			slice = b;
		else if (b->type == VASliceDataBufferType && !bsbuf)
			bsbuf = b;
		else if (b->type == VAIQMatrixBufferType && !iq)
			iq = b;
	}
	if (!pic || !slice)
		return VA_STATUS_ERROR_INVALID_PARAMETER;

	/* ---- map VA-API params -> VC8000D register params ---- */
	memset(&prm, 0, sizeof(prm));
	{
		VAPictureParameterBufferH264 *p = pic->data;
		VASliceParameterBufferH264   *s = slice->data;

		prm.pic_width_in_mbs_minus1  = p->picture_width_in_mbs_minus1;
		prm.pic_height_in_mbs_minus1 = p->picture_height_in_mbs_minus1;
		prm.num_ref_frames           = p->num_ref_frames;
		prm.bit_depth_luma_minus8    = p->bit_depth_luma_minus8;
		prm.bit_depth_chroma_minus8  = p->bit_depth_chroma_minus8;
		prm.pic_init_qp_minus26      = p->pic_init_qp_minus26;
		prm.chroma_qp_index_offset   = p->chroma_qp_index_offset;
		prm.second_chroma_qp_index_offset = p->second_chroma_qp_index_offset;
		prm.frame_num                = p->frame_num;

		prm.chroma_format_idc             = p->seq_fields.bits.chroma_format_idc;
		prm.frame_mbs_only_flag           = p->seq_fields.bits.frame_mbs_only_flag;
		prm.mb_adaptive_frame_field_flag  = p->seq_fields.bits.mb_adaptive_frame_field_flag;
		prm.direct_8x8_inference_flag     = p->seq_fields.bits.direct_8x8_inference_flag;
		prm.log2_max_frame_num_minus4     = p->seq_fields.bits.log2_max_frame_num_minus4;
		prm.pic_order_cnt_type            = p->seq_fields.bits.pic_order_cnt_type;
		prm.log2_max_pic_order_cnt_lsb_minus4 = p->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4;

		prm.entropy_coding_mode_flag      = p->pic_fields.bits.entropy_coding_mode_flag;
		prm.field_pic_flag                = p->pic_fields.bits.field_pic_flag;
		prm.reference_pic_flag            = p->pic_fields.bits.reference_pic_flag;
		prm.constrained_intra_pred_flag   = p->pic_fields.bits.constrained_intra_pred_flag;

		/* Current picture POC (for the work-buffer POC table tail). */
		prm.curr_poc_top    = p->CurrPic.TopFieldOrderCnt;
		prm.curr_poc_bottom = p->CurrPic.BottomFieldOrderCnt;

		prm.slice_data_size       = s->slice_data_size;
		prm.slice_data_bit_offset = s->slice_data_bit_offset;
		prm.first_mb_in_slice     = s->first_mb_in_slice;
		prm.slice_type            = s->slice_type;
		prm.num_ref_idx_l0_active_minus1 = s->num_ref_idx_l0_active_minus1;
		prm.num_ref_idx_l1_active_minus1 = s->num_ref_idx_l1_active_minus1;

		/* infer IDR / bit lengths conservatively from flags */
		prm.idr_pic_id = (p->pic_fields.bits.reference_pic_flag &&
		                  s->slice_type == 7) ? 1 : 0;
		prm.dec_ref_pic_marking_bit_size = 16;   /* safe default */
		prm.pic_order_cnt_bit_size = prm.log2_max_pic_order_cnt_lsb_minus4 + 4;

		/* ---- DPB reference frames: VAPictureH264[16] -> VPU MMU addrs ---- */
		{
			int n;
			for (n = 0; n < 16; n++) {
				VAPictureH264 *ref = &p->ReferenceFrames[n];
				struct h264_dpb_ref *d = &prm.dpb[n];
				struct tracked_surface *surf;

				d->frame_num = ref->frame_idx;
				d->poc_top    = ref->TopFieldOrderCnt;
				d->poc_bottom = ref->BottomFieldOrderCnt;
				d->valid      = !(ref->flags & VA_PICTURE_H264_INVALID);
				d->long_term  = !!(ref->flags & VA_PICTURE_H264_LONG_TERM_REFERENCE);
				d->field_pic  = !!(ref->flags &
				                   (VA_PICTURE_H264_TOP_FIELD |
				                    VA_PICTURE_H264_BOTTOM_FIELD));
				d->top_field  = !!(ref->flags & VA_PICTURE_H264_TOP_FIELD);

				/* picture_id is the client's VASurfaceID for this DPB slot;
				 * resolve it to the MMU-mapped VPU buffer. */
				surf = d->valid ? find_surface(drv, ref->picture_id) : NULL;
				d->luma_mmu_addr = surf ? vpu_buffer_mmu(surf->buf) : 0;
			}

			/* Reference picture lists -> DPB slot indices.  VA clients
			 * encode the slot in picture_id (0..15); treat it directly. */
			prm.refs.list0_count = 0;
			for (n = 0; n < 16 && n < 32; n++) {
				VAPictureH264 *r = &s->RefPicList0[n];
				if (r->flags & VA_PICTURE_H264_INVALID) break;
				prm.refs.list0[n] = (uint8_t)(r->picture_id & 0x1f);
				prm.refs.list0_count++;
			}
			prm.refs.list1_count = 0;
			for (n = 0; n < 16 && n < 32; n++) {
				VAPictureH264 *r = &s->RefPicList1[n];
				if (r->flags & VA_PICTURE_H264_INVALID) break;
				prm.refs.list1[n] = (uint8_t)(r->picture_id & 0x1f);
				prm.refs.list1_count++;
			}

			/* Scaling lists from the IQ matrix buffer. */
			if (iq) {
				VAIQMatrixBufferH264 *q = iq->data;
				prm.scaling_list_present = 1;
				memcpy(prm.scaling_list4x4, q->ScalingList4x4,
				       sizeof(prm.scaling_list4x4));
				memcpy(prm.scaling_list8x8, q->ScalingList8x8,
				       sizeof(prm.scaling_list8x8));
			}
		}
	}

	prm.output_mmu_addr = vpu_buffer_mmu(out->buf);

	/* Bitstream: VA slice-data buffer now owns a GEM/DMA-BUF mapped into
	 * the VPU MMU (see alloc_tracked_buffer). Use its real VPU address. */
	prm.stream_mmu_addr = (bsbuf && bsbuf->vpu_buf)
	                    ? vpu_buffer_mmu(bsbuf->vpu_buf) : 0;

	/* Decoder work buffers (RLC/VLC + MB control), sized from picture MBs. */
	{
		unsigned int mbs = ((unsigned int)prm.pic_width_in_mbs_minus1 + 1) *
		                   ((unsigned int)prm.pic_height_in_mbs_minus1 + 1);
		ensure_work_buffers(drv, mbs);
	}
	prm.rlc_vlc_mmu_addr = drv->rlc_vlc_work
	                     ? vpu_buffer_mmu(drv->rlc_vlc_work) : 0;
	prm.mb_ctrl_mmu_addr = drv->mb_ctrl_work
	                     ? vpu_buffer_mmu(drv->mb_ctrl_work) : 0;

	/* Fill CABAC init table, POC table and scaling lists into the RLC/VLC
	 * work buffer before submission (mirrors SDK h264Prepare* functions). */
	if (drv->rlc_vlc_work)
		h264_fill_work_buffer(vpu_buffer_vaddr(drv->rlc_vlc_work), &prm);

	words = vcmd_h264_build_decode_cmd(drv->cmd, VCMD_CMD_WORDS, &prm);
	if (words < 0)
		return VA_STATUS_ERROR_OPERATION_FAILED;

	return vpu_vcmd_submit(drv->vcmd, drv->cmd, words) == 0
	       ? VA_STATUS_SUCCESS : VA_STATUS_ERROR_OPERATION_FAILED;
}

static int is_hevc_profile(VAProfile p)
{
	return p == VAProfileHEVCMain || p == VAProfileHEVCMain10;
}

static int is_av1_profile(VAProfile p)
{
	return p == VAProfileAV1Profile0 || p == VAProfileAV1Profile1;
}

/* HEVC/H.265 decode submission (vaEndPicture for HEVC configs). */
static VAStatus end_picture_hevc(VADriverContextP ctx, VAContextID context)
{
	struct loongvpu_driver_data *drv = ctx ? ctx->pDriverData : NULL;
	struct tracked_surface *out;
	struct hevc_decode_params prm;
	struct tracked_buffer *pic = NULL, *slice = NULL, *bsbuf = NULL, *iq = NULL;
	int i, words;

	(void)context;
	if (!drv || !drv->vcmd)
		return VA_STATUS_ERROR_INVALID_CONTEXT;
	out = find_surface(drv, drv->current_target);
	if (!out)
		return VA_STATUS_ERROR_INVALID_SURFACE;

	for (i = 0; i < MAX_BUFFERS; i++) {
		struct tracked_buffer *b = &drv->buffers[i];
		if (!b->in_use) continue;
		if (b->type == VAPictureParameterBufferType && !pic) pic = b;
		else if (b->type == VASliceParameterBufferType && !slice) slice = b;
		else if (b->type == VASliceDataBufferType && !bsbuf) bsbuf = b;
		else if (b->type == VAIQMatrixBufferType && !iq) iq = b;
	}
	if (!pic || !slice)
		return VA_STATUS_ERROR_INVALID_PARAMETER;

	memset(&prm, 0, sizeof(prm));
	{
		VAPictureParameterBufferHEVC *p = pic->data;
		VASliceParameterBufferHEVC *s = slice->data;

		prm.pic_width_in_luma_samples  = p->pic_width_in_luma_samples;
		prm.pic_height_in_luma_samples = p->pic_height_in_luma_samples;
		prm.chroma_format_idc = p->pic_fields.bits.chroma_format_idc;
		prm.scaling_list_enable = p->pic_fields.bits.scaling_list_enabled_flag;
		prm.bit_depth_luma_minus8  = p->bit_depth_luma_minus8;
		prm.bit_depth_chroma_minus8 = p->bit_depth_chroma_minus8;
		prm.log2_min_cb_size  = p->log2_min_luma_coding_block_size_minus3 + 3;
		prm.log2_max_cb_size  = prm.log2_min_cb_size +
		                        p->log2_diff_max_min_luma_coding_block_size;
		prm.log2_min_trb_size = p->log2_min_transform_block_size_minus2 + 2;
		prm.log2_max_trb_size = prm.log2_min_trb_size +
		                        p->log2_diff_max_min_transform_block_size;
		prm.max_dec_pic_buffering = p->sps_max_dec_pic_buffering_minus1 + 1;
		prm.pcm_enabled   = p->pic_fields.bits.pcm_enabled_flag;
		prm.max_transform_hierarchy_depth_intra = p->max_transform_hierarchy_depth_intra;
		prm.max_transform_hierarchy_depth_inter = p->max_transform_hierarchy_depth_inter;
		prm.log2_parallel_merge_level = p->log2_parallel_merge_level_minus2 + 2;

		prm.init_qp_minus26      = p->init_qp_minus26;
		prm.cb_qp_offset         = p->pps_cb_qp_offset;
		prm.cr_qp_offset         = p->pps_cr_qp_offset;
		prm.diff_cu_qp_delta_depth = p->diff_cu_qp_delta_depth;
		prm.num_ref_idx_l0_active = s->num_ref_idx_l0_active_minus1 + 1;
		prm.num_ref_idx_l1_active = s->num_ref_idx_l1_active_minus1 + 1;
		prm.weighted_pred_flag   = p->pic_fields.bits.weighted_pred_flag;
		prm.weighted_bipred_flag = p->pic_fields.bits.weighted_bipred_flag;
		prm.cu_qp_delta_enabled  = p->pic_fields.bits.cu_qp_delta_enabled_flag;
		prm.constrained_intra_pred = p->pic_fields.bits.constrained_intra_pred_flag;
		prm.transform_skip_enabled = p->pic_fields.bits.transform_skip_enabled_flag;
		prm.sign_data_hiding = p->pic_fields.bits.sign_data_hiding_enabled_flag;
		prm.lists_modification_present = p->slice_parsing_fields.bits.lists_modification_present_flag;
		prm.cabac_init_present = p->slice_parsing_fields.bits.cabac_init_present_flag;
		prm.temporal_mvp_enabled = p->slice_parsing_fields.bits.sps_temporal_mvp_enabled_flag;
		prm.sample_adaptive_offset_enabled = p->slice_parsing_fields.bits.sample_adaptive_offset_enabled_flag;
		prm.strong_intra_smoothing = p->pic_fields.bits.strong_intra_smoothing_enabled_flag;
		prm.entropy_coding_sync_enabled = p->pic_fields.bits.entropy_coding_sync_enabled_flag;
		prm.deblocking_filter_override = p->slice_parsing_fields.bits.deblocking_filter_override_enabled_flag;
		prm.disable_deblocking_filter = p->slice_parsing_fields.bits.pps_disable_deblocking_filter_flag;
		prm.slice_segment_header_extension_present =
		        p->slice_parsing_fields.bits.slice_segment_header_extension_present_flag;
		prm.idr_pic_flag = p->slice_parsing_fields.bits.IdrPicFlag;
		prm.rap_pic_flag = p->slice_parsing_fields.bits.RapPicFlag;
		prm.dependent_slice_segments = p->slice_parsing_fields.bits.dependent_slice_segments_enabled_flag;
		prm.slice_hdr_extra_bits = p->num_extra_slice_header_bits;

		prm.slice_data_size = s->slice_data_size;
		prm.slice_data_byte_offset = s->slice_data_byte_offset;
		prm.slice_type = s->LongSliceFlags.fields.slice_type;

		/* DPB: 15 reference frames, picture_id -> surface MMU address. */
		for (i = 0; i < 15; i++) {
			VAPictureHEVC *r = &p->ReferenceFrames[i];
			struct tracked_surface *surf;
			prm.dpb[i].poc = r->pic_order_cnt;
			prm.dpb[i].valid = !(r->flags & VA_PICTURE_HEVC_INVALID);
			prm.dpb[i].long_term = !!(r->flags & VA_PICTURE_HEVC_LONG_TERM_REFERENCE);
			/* RPS categories -> hardware L0/L1 list construction */
			prm.dpb[i].rps_before = !!(r->flags & VA_PICTURE_HEVC_RPS_ST_CURR_BEFORE);
			prm.dpb[i].rps_after  = !!(r->flags & VA_PICTURE_HEVC_RPS_ST_CURR_AFTER);
			prm.dpb[i].rps_lt     = !!(r->flags & VA_PICTURE_HEVC_RPS_LT_CURR);
			surf = prm.dpb[i].valid ? find_surface(drv, r->picture_id) : NULL;
			prm.dpb[i].luma_mmu_addr = surf ? vpu_buffer_mmu(surf->buf) : 0;
		}
		/* Current picture POC for the RPS POC-difference registers. */
		prm.curr_poc = p->CurrPic.pic_order_cnt;

		/* Reference lists: RefPicList[2][15] are DPB indices. */
		for (i = 0; i < 15; i++) {
			prm.ref_list0[i] = s->RefPicList[0][i];
			prm.ref_list1[i] = s->RefPicList[1][i];
			if (s->RefPicList[0][i] != 0xff) prm.ref_list0_count = i + 1;
			if (s->RefPicList[1][i] != 0xff) prm.ref_list1_count = i + 1;
		}
	}

	prm.output_y_mmu = vpu_buffer_mmu(out->buf);
	prm.stream_mmu = (bsbuf && bsbuf->vpu_buf)
	               ? vpu_buffer_mmu(bsbuf->vpu_buf) : 0;

	/* HEVC work buffer (scale list at swreg 171/170). Reuse the work buffer. */
	{
		unsigned int mbs = ((prm.pic_width_in_luma_samples + 15) / 16) *
		                   ((prm.pic_height_in_luma_samples + 15) / 16);
		ensure_work_buffers(drv, mbs);
	}
	prm.rlc_vlc_mmu = drv->rlc_vlc_work ? vpu_buffer_mmu(drv->rlc_vlc_work) : 0;

	/* HEVC scaling lists: map VAIQMatrixBufferHEVC into params, allocate a
	 * dedicated 1 KiB work buffer, pack it per the SDK and set its VPU MMU
	 * address (swreg 171/170). When scaling is disabled the register is off
	 * and the address is unused. */
	if (iq) {
		VAIQMatrixBufferHEVC *q = iq->data;
		memcpy(prm.scaling_list4x4,   q->ScalingList4x4,   sizeof(prm.scaling_list4x4));
		memcpy(prm.scaling_list8x8,   q->ScalingList8x8,   sizeof(prm.scaling_list8x8));
		memcpy(prm.scaling_list16x16, q->ScalingList16x16, sizeof(prm.scaling_list16x16));
		memcpy(prm.scaling_list32x32, q->ScalingList32x32, sizeof(prm.scaling_list32x32));
		memcpy(prm.scaling_list_dc16, q->ScalingListDC16x16, sizeof(prm.scaling_list_dc16));
		memcpy(prm.scaling_list_dc32, q->ScalingListDC32x32, sizeof(prm.scaling_list_dc32));
	}
	if (prm.scaling_list_enable && drv->drm_fd >= 0) {
		if (!drv->scale_work) {
			drv->scale_work = vpu_buffer_alloc_bytes(drv->drm_fd, HEVC_WB_SCALE_BYTES);
			if (drv->scale_work)
				vpu_buffer_mmu_map(drv->scale_work, 0);
		}
		if (drv->scale_work) {
			hevc_fill_scaling_list(vpu_buffer_vaddr(drv->scale_work), &prm);
			prm.scale_list_mmu = vpu_buffer_mmu(drv->scale_work);
		}
	}

	words = vcmd_hevc_build_decode_cmd(drv->cmd, VCMD_CMD_WORDS, &prm);
	if (words < 0)
		return VA_STATUS_ERROR_OPERATION_FAILED;
	return vpu_vcmd_submit(drv->vcmd, drv->cmd, words) == 0
	       ? VA_STATUS_SUCCESS : VA_STATUS_ERROR_OPERATION_FAILED;
}

/* Dispatch vaEndPicture by the configured codec profile. */
static VAStatus loongvpu_EndPicture(VADriverContextP ctx, VAContextID context)
{
	struct loongvpu_driver_data *drv = ctx ? ctx->pDriverData : NULL;

	if (!drv)
		return VA_STATUS_ERROR_INVALID_CONTEXT;
	if (is_av1_profile(drv->current_profile)) {
		/* AV1 is advertised for capability completeness, but this open
		 * VC8000D SDK has no AV1 register map and the AOSC advisory notes
		 * VPU AV1 decode is broken. Refuse explicitly rather than feed the
		 * AV1 bitstream to the H.264/HEVC builders (which would misdecode). */
		fprintf(stderr, "loongvpu: AV1 decode is not supported by this driver "
		                "(no open AV1 VPU register map); use software dav1d\n");
		return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
	}
	if (is_hevc_profile(drv->current_profile))
		return end_picture_hevc(ctx, context);
	return end_picture_h264(ctx, context);
}

static VAStatus loongvpu_QueryDisplayAttributes(
	VADriverContextP ctx, VADisplayAttribute *attr_list, int *num_attributes)
{
	(void)ctx; (void)attr_list;
	if (num_attributes) *num_attributes = 0;
	return VA_STATUS_SUCCESS;
}

static VAStatus loongvpu_GetDisplayAttributes(
	VADriverContextP ctx, VADisplayAttribute *attr_list, int num_attributes)
{
	(void)ctx; (void)attr_list; (void)num_attributes;
	return VA_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------- */
/* Stub hooks for the remaining mandatory vtable entries.                     */
/*                                                                            */
/* libva checks that a broad set of vtable entries are non-NULL at init time. */
/* The scaffold does not implement surfaces-as-images or subpictures, so these */
/* return VA_STATUS_ERROR_UNIMPLEMENTED (or safe empty results where libva
 * expects an out-parameter).  Real decode only needs a subset of these, but
 * providing them keeps libva's loader happy. */
/* ------------------------------------------------------------------------- */

static VAStatus stub_unimplemented(void) { return VA_STATUS_ERROR_UNIMPLEMENTED; }

#define STUB0(name) \
	static VAStatus name(VADriverContextP c) { (void)c; return stub_unimplemented(); }

static VAStatus loongvpu_BufferSetNumElements(
	VADriverContextP c, VABufferID b, unsigned int n)
{ (void)c; (void)b; (void)n; return stub_unimplemented(); }

static VAStatus loongvpu_SyncSurface(VADriverContextP c, VASurfaceID s)
{ (void)c; (void)s; return stub_unimplemented(); }

static VAStatus loongvpu_QuerySurfaceStatus(
	VADriverContextP c, VASurfaceID s, VASurfaceStatus *st)
{ (void)c; (void)s; if (st) *st = VASurfaceReady; return VA_STATUS_SUCCESS; }

static VAStatus loongvpu_QuerySurfaceError(
	VADriverContextP c, VASurfaceID s, VAStatus es, void **ei)
{ (void)c; (void)s; (void)es; if (ei) *ei = NULL; return stub_unimplemented(); }

static VAStatus loongvpu_PutSurface(
	VADriverContextP c, VASurfaceID s, void *d,
	short sx, short sy, unsigned short sw, unsigned short sh,
	short dx, short dy, unsigned short dw, unsigned short dh,
	VARectangle *cr, unsigned int nc, unsigned int f)
{ (void)c; (void)s; (void)d; (void)sx; (void)sy; (void)sw; (void)sh;
  (void)dx; (void)dy; (void)dw; (void)dh; (void)cr; (void)nc; (void)f;
  return stub_unimplemented(); }

static VAStatus loongvpu_QueryImageFormats(
	VADriverContextP c, VAImageFormat *fl, int *nf)
{ (void)c; (void)fl; if (nf) *nf = 0; return VA_STATUS_SUCCESS; }

static VAStatus loongvpu_CreateImage(
	VADriverContextP c, VAImageFormat *f, int w, int h, VAImage *i)
{ (void)c; (void)f; (void)w; (void)h; (void)i; return stub_unimplemented(); }

static VAStatus loongvpu_DeriveImage(VADriverContextP c, VASurfaceID s, VAImage *i)
{ (void)c; (void)s; (void)i; return stub_unimplemented(); }

static VAStatus loongvpu_DestroyImage(VADriverContextP c, VAImageID i)
{ (void)c; (void)i; return stub_unimplemented(); }

static VAStatus loongvpu_SetImagePalette(VADriverContextP c, VAImageID i, unsigned char *p)
{ (void)c; (void)i; (void)p; return stub_unimplemented(); }

static VAStatus loongvpu_GetImage(
	VADriverContextP c, VASurfaceID s, int x, int y,
	unsigned int w, unsigned int h, VAImageID i)
{ (void)c; (void)s; (void)x; (void)y; (void)w; (void)h; (void)i; return stub_unimplemented(); }

static VAStatus loongvpu_PutImage(
	VADriverContextP c, VASurfaceID s, VAImageID i,
	int sx, int sy, unsigned int sw, unsigned int sh,
	int dx, int dy, unsigned int dw, unsigned int dh)
{ (void)c; (void)s; (void)i; (void)sx; (void)sy; (void)sw; (void)sh;
  (void)dx; (void)dy; (void)dw; (void)dh; return stub_unimplemented(); }

static VAStatus loongvpu_QuerySubpictureFormats(
	VADriverContextP c, VAImageFormat *fl, unsigned int *flags, unsigned int *nf)
{ (void)c; (void)fl; (void)flags; if (nf) *nf = 0; return VA_STATUS_SUCCESS; }

static VAStatus loongvpu_CreateSubpicture(
	VADriverContextP c, VAImageID i, VASubpictureID *sp)
{ (void)c; (void)i; if (sp) *sp = 0; return stub_unimplemented(); }

static VAStatus loongvpu_DestroySubpicture(VADriverContextP c, VASubpictureID sp)
{ (void)c; (void)sp; return stub_unimplemented(); }

static VAStatus loongvpu_SetSubpictureImage(VADriverContextP c, VASubpictureID sp, VAImageID i)
{ (void)c; (void)sp; (void)i; return stub_unimplemented(); }

static VAStatus loongvpu_SetSubpictureChromakey(
	VADriverContextP c, VASubpictureID sp, unsigned int a, unsigned int b, unsigned int m)
{ (void)c; (void)sp; (void)a; (void)b; (void)m; return stub_unimplemented(); }

static VAStatus loongvpu_SetSubpictureGlobalAlpha(
	VADriverContextP c, VASubpictureID sp, float a)
{ (void)c; (void)sp; (void)a; return stub_unimplemented(); }

static VAStatus loongvpu_AssociateSubpicture(
	VADriverContextP c, VASubpictureID sp, VASurfaceID *ts, int ns,
	short sx, short sy, unsigned short sw, unsigned short sh,
	short dx, short dy, unsigned short dw, unsigned short dh, unsigned int f)
{ (void)c; (void)sp; (void)ts; (void)ns; (void)sx; (void)sy; (void)sw; (void)sh;
  (void)dx; (void)dy; (void)dw; (void)dh; (void)f; return stub_unimplemented(); }

static VAStatus loongvpu_DeassociateSubpicture(
	VADriverContextP c, VASubpictureID sp, VASurfaceID *ts, int ns)
{ (void)c; (void)sp; (void)ts; (void)ns; return stub_unimplemented(); }

static VAStatus loongvpu_SetDisplayAttributes(
	VADriverContextP c, VADisplayAttribute *al, int na)
{ (void)c; (void)al; (void)na; return stub_unimplemented(); }

/* ------------------------------------------------------------------------- */
/* Driver entry point                                                         */
/* ------------------------------------------------------------------------- */

/* libva calls __vaDriverInit_<major>_<minor> matching the ABI it wants.  The
 * system libva is 1.23 which requests ABI 1.15 (see existing drv_video.so).
 * We export both 1.15 and 1.23 covers for robustness. */
static VAStatus loongvpu_DriverInit(VADriverContextP ctx)
{
	struct loongvpu_driver_data *drv;
	struct VADriverVTable *vtable;

	if (!ctx)
		return VA_STATUS_ERROR_INVALID_CONTEXT;

	/* ctx->version_major / version_minor are filled in by libva before it
	 * calls us (they describe the ABI version libva is requesting); the
	 * driver must not overwrite them. */

	drv = (struct loongvpu_driver_data *)calloc(1, sizeof(*drv));
	vtable = (struct VADriverVTable *)calloc(1, sizeof(*vtable));
	if (!drv || !vtable) {
		free(drv);
		free(vtable);
		return VA_STATUS_ERROR_ALLOCATION_FAILED;
	}

	ctx->pDriverData = drv;
	ctx->vtable = vtable;

	/* Fill the mandatory vtable hooks. */
	vtable->vaTerminate              = loongvpu_Terminate;
	vtable->vaQueryConfigProfiles    = loongvpu_QueryConfigProfiles;
	vtable->vaQueryConfigEntrypoints = loongvpu_QueryConfigEntrypoints;
	vtable->vaGetConfigAttributes    = loongvpu_GetConfigAttributes;
	vtable->vaCreateConfig           = loongvpu_CreateConfig;
	vtable->vaDestroyConfig          = loongvpu_DestroyConfig;
	vtable->vaQueryConfigAttributes  = loongvpu_QueryConfigAttributes;
	vtable->vaCreateSurfaces         = loongvpu_CreateSurfaces;
	vtable->vaDestroySurfaces        = loongvpu_DestroySurfaces;
	vtable->vaCreateContext          = loongvpu_CreateContext;
	vtable->vaDestroyContext         = loongvpu_DestroyContext;
	vtable->vaCreateBuffer           = loongvpu_CreateBuffer;
	vtable->vaMapBuffer              = loongvpu_MapBuffer;
	vtable->vaUnmapBuffer            = loongvpu_UnmapBuffer;
	vtable->vaDestroyBuffer          = loongvpu_DestroyBuffer;
	vtable->vaBeginPicture           = loongvpu_BeginPicture;
	vtable->vaRenderPicture          = loongvpu_RenderPicture;
	vtable->vaEndPicture             = loongvpu_EndPicture;
	vtable->vaQueryDisplayAttributes = loongvpu_QueryDisplayAttributes;
	vtable->vaGetDisplayAttributes   = loongvpu_GetDisplayAttributes;

	/* Remaining mandatory vtable entries (stubs) so libva's loader is
	 * satisfied.  Only a subset is exercised by decode in the future. */
	vtable->vaBufferSetNumElements   = loongvpu_BufferSetNumElements;
	vtable->vaSyncSurface            = loongvpu_SyncSurface;
	vtable->vaQuerySurfaceStatus     = loongvpu_QuerySurfaceStatus;
	vtable->vaQuerySurfaceError      = loongvpu_QuerySurfaceError;
	vtable->vaPutSurface             = loongvpu_PutSurface;
	vtable->vaQueryImageFormats      = loongvpu_QueryImageFormats;
	vtable->vaCreateImage            = loongvpu_CreateImage;
	vtable->vaDeriveImage            = loongvpu_DeriveImage;
	vtable->vaDestroyImage           = loongvpu_DestroyImage;
	vtable->vaSetImagePalette        = loongvpu_SetImagePalette;
	vtable->vaGetImage               = loongvpu_GetImage;
	vtable->vaPutImage               = loongvpu_PutImage;
	vtable->vaQuerySubpictureFormats = loongvpu_QuerySubpictureFormats;
	vtable->vaCreateSubpicture       = loongvpu_CreateSubpicture;
	vtable->vaDestroySubpicture      = loongvpu_DestroySubpicture;
	vtable->vaSetSubpictureImage     = loongvpu_SetSubpictureImage;
	vtable->vaSetSubpictureChromakey = loongvpu_SetSubpictureChromakey;
	vtable->vaSetSubpictureGlobalAlpha = loongvpu_SetSubpictureGlobalAlpha;
	vtable->vaAssociateSubpicture    = loongvpu_AssociateSubpicture;
	vtable->vaDeassociateSubpicture  = loongvpu_DeassociateSubpicture;
	vtable->vaSetDisplayAttributes   = loongvpu_SetDisplayAttributes;

	/* ctx limits libva uses to size its own caches. */
	ctx->max_profiles        = sizeof(supported_profiles)/sizeof(supported_profiles[0]);
	ctx->max_entrypoints     = 3;   /* VLD, EncSlice (+ reserved) */
	ctx->max_attributes      = 16;
	ctx->max_image_formats   = 1;
	ctx->max_subpic_formats  = 1;   /* libva requires > 0 */
	ctx->str_vendor          = LOONGVPU_VENDOR;

	loongvpu_drm_init(drv);

	return VA_STATUS_SUCCESS;
}

/* Export the ABI 1.15 entry point (what libva 1.23 actually calls). */
VAStatus __vaDriverInit_1_15(VADriverContextP ctx)
{
	return loongvpu_DriverInit(ctx);
}

/* Also export 1.23 in case a newer libva requests it directly. */
VAStatus __vaDriverInit_1_23(VADriverContextP ctx)
{
	return loongvpu_DriverInit(ctx);
}
