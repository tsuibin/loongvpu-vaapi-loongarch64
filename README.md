# loongvpu-vaapi

An **open-source VA-API driver scaffold** for the Loongson **LoongVPU** video
processing unit (found in 2K3000 / 3B6000M SoCs, including the LG200-based
platforms).

> **Status: working skeleton.** This driver compiles, is discovered and fully
> loaded by libva, advertises the codecs the VPU supports, and opens a DRM
> connection to the open-source kernel driver. It does **not** yet submit real
> decode work to the hardware — that is the next development step (see Roadmap).

## Why this exists

The proprietary `loongvpu_drv_video.so` shipped by Loongson is closed source.
The kernel-mode driver, however, **is** open source:

- `AOSC-Tracking/loongvpu-kernel-dkms`
- `loongson-community/loongvpu-kernel-dkms`

That kernel driver exposes a full set of DRM/character-device ioctls
(`DRM_IOCTL_HANTRO_*`, `HANTRODEC_IOCS_*_PUSH_REG`, MMU map ioctls, ...). This
project provides an open, MIT-licensed libva driver that speaks that ABI,
so the userspace side no longer has to be a black box.

## What works today (verified on AOSC OS, libva 1.23)

- `make` produces `loongvpu_drv_video.so`.
- libva loads it via `LIBVA_DRIVER_NAME=loongvpu` — `va_openDriver()` returns 0.
- `vainfo` lists all supported profiles/entrypoints:
  - H.264 (Baseline/Main/High) decode + encode
  - H.265 (Main/Main10) decode + encode
  - AV1 Profile 0 decode
- `make test` passes an `dlopen()` + entry-point smoke test with no VPU needed.
- Driver opens `/dev/dri/renderD128` and queries VCMD/PM capabilities from the
  kernel driver (soft failure when no hardware is present).

## Architecture

```
   application (mpv / Chromium / ffmpeg)
            │  VA-API
            ▼
   libva  ──dlopen──▶  loongvpu_drv_video.so   (this project)
                            │  ioctl()
                            ▼
              /dev/dri/renderD*  (LoongVPU DRM driver)
                            │
                            ▼
                    Hantro VPU hardware
```

Source layout:

- `src/loongvpu_abi.h` — userspace copy of the kernel ioctl ABI (structures and
  command numbers), kept byte-identical to `include/hantro.h` of the kernel
  driver.
- `src/loongvpu_drv_video.c` — libva entry points (`__vaDriverInit_1_15`,
  `__vaDriverInit_1_23`), vtable hooks, capability query, object-ID stubs, and
  the DRM backend.
- `tests/load_test.c` — hardware-free loading smoke test.

## Build & test

```bash
make
make test
```

Install into libva's driver directory (needs write access to `/usr/lib/dri`):

```bash
sudo make install
# or, user-local:
LIBVA_DRIVERS_PATH=$PWD LIBVA_DRIVER_NAME=loongvpu vainfo
```

## Roadmap (what is intentionally missing)

The hooks currently return `VA_STATUS_ERROR_UNIMPLEMENTED` or hand out
placeholder IDs. To turn this scaffold into a real decoder:

1. **Surfaces**: allocate DMA-BUF / system buffers, map them into the VPU via
   `HANTRO_IOCS_MMU_MEM_MAP`, track them in `pDriverData`.
2. **Decode buffers**: parse VA slice/parameter buffers into Hantro VPU
   register commands (the hard part; mirrors what the closed driver does).
3. **Submission**: implement `vaRenderPicture` / `vaEndPicture` using
   `HANTRODEC_IOCS_DEC_PUSH_REG` + `HANTRODEC_IOCX_DEC_WAIT` / fence ioctls.
4. **Per-codec support**: H.264 first, then H.265, then AV1.
5. **Encode path**: `VAEntrypointEncSlice` via `HANTROENC_IOC*` ioctls.
6. **Browser integration**: even with a working driver, note the AOSC advisory
   that Chromium currently fails to use VA-API on this VPU; mpv/GStreamer are
   easier early test targets.

## Known limitations / caveats

- Only the kernel-mode driver is open source (DKMS); upstream targets Linux 6.6 (AOSC backports to 6.18). This machine runs Linux 7.1, so a matching kernel-module build is required.
- The `loongvpu` userspace package (closed) is not required for this scaffold.
- ABI structs must track any changes in the kernel driver's `include/hantro.h`.

## License

MIT for this scaffold. The upstream LoongVPU kernel driver is GPL-2.0; when
porting code from it, preserve its GPL license and keep it in separate files.

## References

- AOSC announcement: https://aosc.io/news/2025-12-31-loonggpu-and-loongvpu-preview
- Kernel ABI source: https://github.com/AOSC-Tracking/loongvpu-kernel-dkms
- libva: https://github.com/intel/libva
