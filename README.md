# loongvpu-vaapi

An **open-source VA-API driver scaffold** for the Loongson **LoongVPU** video
processing unit (found in 2K3000 / 3B6000M SoCs, including the LG200-based
platforms).

> **Status: working skeleton with H.264 submission path implemented.** The
> driver now allocates real GEM/DMA buffers, maps them into the VPU MMU, and
> drives the full VCMD reserve → link/run → wait → release submission flow for
> H.264 decode. The **concrete H.264 register sequence** (SPS/PPS/slice → VCD
> register writes) is still a structural skeleton: those values live in the
> closed-source DWL / chip manual and are not in the open kernel tree (see
> Roadmap). Without a real LoongVPU device the driver soft-degrades to
> "decode disabled" but still loads.

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
- `make test` passes both the `dlopen()` smoke test and the VCMD command-word
  unit test (`cmd_test`), with no VPU needed.
- Driver opens `/dev/dri/renderD128`, queries VCMD/PM capabilities, and opens
  the VCMD command-buffer pool (soft failure when no hardware is present).
- **DMA-BUF + MMU** (`src/vpu_buffer.c`): GEM dumb create → GETPADDR → MAP_DUMB
  /mmap → `HANTRO_IOCS_MMU_MEM_MAP`, with matching unmap/close teardown.
  `vaCreateSurfaces` now allocates and MMU-maps real buffers.
- **VCMD submission** (`src/vcmd.c`): command word builders (`OPCODE_WREG/END/JMP`,
  matching the kernel's command layout), pool setup, and the full
  reserve→copy→link/run→wait→release flow wired into `vaEndPicture`.

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
  command numbers), kept byte-identical to `include/hantro.h`,
  `include/hantrommu.h` and `include/hantro_vcmd.h` of the kernel driver.
- `src/loongvpu_drv_video.c` — libva entry points (`__vaDriverInit_1_15`,
  `__vaDriverInit_1_23`), vtable hooks, capability query, surface/buffer object
  tables, and the DRM backend.
- `src/vpu_buffer.{c,h}` — GEM/DMA-BUF allocation + VPU MMU map/unmap.
- `src/vcmd.{c,h}` — VCMD command builders, pool setup, reserve→run→wait→release,
  and the H.264 decode command skeleton.
- `tests/load_test.c` — hardware-free `dlopen()` smoke test.
- `tests/cmd_test.c` — VCMD command-word encoding unit test.

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

## Roadmap

Done:

1. ~~Surfaces: DMA-BUF allocation + MMU map~~ — `src/vpu_buffer.c`.
2. ~~VCMD submission path: reserve → link/run → wait → release~~ — `src/vcmd.c`.

Remaining (the hard part):

3. **H.264 register sequence** — `vcmd_h264_build_decode_cmd()` is currently a
   structural skeleton. Parsing SPS/PPS/slice headers and emitting the exact
   VCD register writes (stream base/length, dimensions, DPB, ref lists, POC,
   CABAC flags, output base, DEC_E start) requires the VeriSilicon/Loongson
   chip manual or the closed DWL. This is the real blocker to a decoded image,
   not the ioctl plumbing.
4. **Per-codec support**: fill in H.264, then H.265, then AV1.
5. **Encode path**: `VAEntrypointEncSlice` via `HANTROENC_IOC*` ioctls.
6. **Browser integration**: even with a working driver, the AOSC advisory notes
   Chromium currently fails to use VA-API on this VPU; mpv/GStreamer are easier
   early test targets.

### Alternative: CPU-side decode optimization
If the VPU register manual is unavailable, the other route is software H.264
decode accelerated by LoongArch LSX/LASX SIMD intrinsics. Reference:
[unofficial-loongarch-intrinsics-guide (lg200_wip)](https://github.com/phorcys/unofficial-loongarch-intrinsics-guide/tree/lg200_wip).
That path bypasses the VPU entirely (FFmpeg/libavcodec + LSX) and is a separate
project from this VA-API driver.

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
