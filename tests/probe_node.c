/*
 * probe_node.c - find the LoongVPU DRM render node on a target machine.
 *
 * Probes /dev/dri/renderD* with DRM_IOCTL_HANTRO_GET_VCMDSUP: the Hantro VPU
 * driver answers it; the GPU (loonggpu) and other DRM drivers return
 * EINVAL/ENOTTY. Prints the node to set in LOONGVPU_RENDER_NODE, or says none.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <libdrm/drm.h>

#define HANTRO_DRM_IOCTL_START DRM_COMMAND_BASE
#define DRM_IOCTL_HANTRO_GET_VCMDSUP \
	DRM_IOWR(HANTRO_DRM_IOCTL_START + 13, unsigned int *)

int main(int argc, char **argv)
{
	char path[64];
	const char *env = getenv("LOONGVPU_RENDER_NODE");
	int i, fd, r, found = 0;
	unsigned int sup = 0;

	if (argc > 1) {
		fd = open(argv[1], O_RDWR | O_CLOEXEC);
		if (fd < 0) { perror(argv[1]); return 1; }
		r = ioctl(fd, DRM_IOCTL_HANTRO_GET_VCMDSUP, &sup);
		printf("%s: %s (errno=%d, vcmdsup=%u)\n", argv[1],
		       r == 0 ? "VPU" : "not a VPU", errno, sup);
		close(fd);
		return r == 0 ? 0 : 1;
	}

	for (i = 0; i < 256; i++) {
		snprintf(path, sizeof(path), "/dev/dri/renderD%d", i);
		fd = open(path, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			continue;
		r = ioctl(fd, DRM_IOCTL_HANTRO_GET_VCMDSUP, &sup);
		if (r == 0) {
			printf("VPU node: %s (vcmdsup=%u)\n", path, sup);
			printf("Set: LOONGVPU_RENDER_NODE=%s\n", path);
			close(fd);
			found = 1;
			break;
		}
		close(fd);
	}
	if (!found) {
		fprintf(stderr, "No LoongVPU render node found. Is the loongvpu "
		                "kernel module loaded?\n");
		(void)env;
		return 1;
	}
	return 0;
}
