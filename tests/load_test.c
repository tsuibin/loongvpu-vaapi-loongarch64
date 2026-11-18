/*
 * load_test.c -- smoke test for the LoongVPU VA-API driver scaffold.
 *
 * It dlopen()s the driver .so, resolves the __vaDriverInit_1_15 entry point,
 * and calls it with a fake VADriverContext to verify:
 *   - the shared object loads (all symbols resolve)
 *   - the entry point runs without crashing
 *   - the vtable and capability hooks are installed
 *
 * This does NOT go through libva's real display/loader path; it is only a
 * lightweight build/ABI sanity check that needs no VPU hardware.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include <va/va.h>
#include <va/va_backend.h>

typedef VAStatus (*driver_init_fn)(VADriverContextP);

int main(int argc, char **argv)
{
	const char *path = (argc > 1) ? argv[1] : "./loongvpu_drv_video.so";
	void *handle;
	driver_init_fn init;
	struct VADriverContext ctx;
	VAProfile profiles[16];
	VAEntrypoint eps[8];
	int np = 0, ne = 0, i;
	VAStatus st;

	handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (!handle) {
		fprintf(stderr, "dlopen failed: %s\n", dlerror());
		return 1;
	}

	init = (driver_init_fn)dlsym(handle, "__vaDriverInit_1_15");
	if (!init) {
		fprintf(stderr, "dlsym(__vaDriverInit_1_15) failed: %s\n", dlerror());
		dlclose(handle);
		return 1;
	}

	memset(&ctx, 0, sizeof(ctx));
	st = init(&ctx);
	if (st != VA_STATUS_SUCCESS) {
		fprintf(stderr, "driver init returned %d\n", st);
		dlclose(handle);
		return 1;
	}

	if (!ctx.vtable || !ctx.pDriverData) {
		fprintf(stderr, "driver init did not set vtable / pDriverData\n");
		dlclose(handle);
		return 1;
	}

	st = ctx.vtable->vaQueryConfigProfiles(&ctx, profiles, &np);
	if (st != VA_STATUS_SUCCESS || np <= 0) {
		fprintf(stderr, "vaQueryConfigProfiles failed: %d (np=%d)\n", st, np);
		dlclose(handle);
		return 1;
	}

	printf("driver loaded OK; advertised profiles (%d):\n", np);
	{
		int av1_advertised = 0;
		for (i = 0; i < np; i++) {
			ne = 0;
			ctx.vtable->vaQueryConfigEntrypoints(&ctx, profiles[i], eps, &ne);
			printf("  profile %2d: %d entrypoint(s)\n", profiles[i], ne);
			if (profiles[i] == VAProfileAV1Profile0)
				av1_advertised = 1;
		}
		if (!av1_advertised) {
			fprintf(stderr, "AV1 profile not advertised\n");
			ctx.vtable->vaTerminate(&ctx);
			dlclose(handle);
			return 1;
		}

		/* Exercise the AV1 stub: create config + context + begin/end and
		 * assert decode is explicitly refused (not mis-routed to H.264). */
		{
			VAConfigID cid = 0;
			VAContextID ctxid = 0;
			VASurfaceID target = 1;
			st = ctx.vtable->vaCreateConfig(&ctx, VAProfileAV1Profile0,
			                                VAEntrypointVLD, NULL, 0, &cid);
			if (st != VA_STATUS_SUCCESS) {
				fprintf(stderr, "AV1 CreateConfig failed: %d\n", st);
				ctx.vtable->vaTerminate(&ctx); dlclose(handle); return 1;
			}
			st = ctx.vtable->vaCreateContext(&ctx, cid, 64, 64, 0,
			                                 &target, 1, &ctxid);
			if (st != VA_STATUS_SUCCESS) {
				fprintf(stderr, "AV1 CreateContext failed: %d\n", st);
				ctx.vtable->vaTerminate(&ctx); dlclose(handle); return 1;
			}
			st = ctx.vtable->vaBeginPicture(&ctx, ctxid, target);
			if (st != VA_STATUS_SUCCESS) {
				fprintf(stderr, "AV1 BeginPicture failed: %d\n", st);
				ctx.vtable->vaTerminate(&ctx); dlclose(handle); return 1;
			}
			st = ctx.vtable->vaEndPicture(&ctx, ctxid);
			if (st != VA_STATUS_ERROR_UNSUPPORTED_PROFILE) {
				fprintf(stderr, "AV1 EndPicture should be UNSUPPORTED_PROFILE, got %d\n", st);
				ctx.vtable->vaTerminate(&ctx); dlclose(handle); return 1;
			}
			printf("AV1 stub: decode correctly refused (UNSUPPORTED_PROFILE)\n");
		}
	}

	/* Tear down through the driver's own terminate hook. */
	ctx.vtable->vaTerminate(&ctx);

	dlclose(handle);
	printf("smoke test PASSED\n");
	return 0;
}
