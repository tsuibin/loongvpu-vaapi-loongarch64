# loongvpu-vaapi

An **open-source VA-API driver scaffold** for the Loongson **LoongVPU** video
processing unit (found in 2K3000 / 3B6000M SoCs, including the LG200-based
platforms).

> **Status: H.264 and HEVC/H.265 register programming implemented from an open VC8000D SDK.**
> The driver allocates real GEM/DMA buffers, maps them into the VPU MMU, and
> drives the full VCMD reserve → link/run → wait → release submission flow.
> The H.264 register sequence is now **real, not a stub**: `h264_regs.h` is
> auto-extracted from the open-source VC8000D SDK (8170table.h, HW_ID 0x8001
> = Loongson LG200 VPU), and `vcmd_h264_build_decode_cmd()` programs the core
> decode registers from VA-API SPS/PPS/slice params (dimensions, QP, CABAC,
> frame numbers, buffer addresses, DEC_E) **and the DPB reference list**:
> 16 reference frames (base addresses, frame numbers, valid/long-term masks,
> REF0 POC) plus P/B reference lists, all mapped from VA-API
> `VAPictureParameterBufferH264`, with DPB POC, CABAC init table and scaling
> lists populated into the RLC/VLC work buffer. The full H.264 decode path is
> now wired end to end. An HEVC/H.265 decode path (`hevc_regs.h`,
> `hevc_decode_params.h`, `vcmd_hevc_build_decode_cmd`, codec routing in
> `vaEndPicture`) programs the core HEVC decode registers from VA-API HEVC
> params. What remains is validation on real hardware, the less-common HEVC
> register fields / RPS handling, and AV1 (see Roadmap).
> Without a real LoongVPU device the driver soft-degrades to "decode disabled"
> but still loads.

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
  - AV1 Profile 0 decode — advertised for capability completeness only;
    `vaEndPicture` explicitly returns `VA_STATUS_ERROR_UNSUPPORTED_PROFILE`
    (see "AV1" in Roadmap). No open AV1 VPU register map exists.
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
  and the H.264 decode register programming.
- `src/h264_regs.h` / `h264_decode_params.h` — H.264 VC8000D register map + VA params.
- `src/hevc_regs.h` / `hevc_decode_params.h` — HEVC/H.265 register map + VA params.
- `src/hevc_work_buffer.{c,h}` — HEVC scaling-list work-buffer packing.
- `src/work_buffer.{c,h}` / `h264_cabac_init.h` — RLC/VLC work-buffer fill
  (CABAC init, POC table, scaling lists).
- `tests/load_test.c` — hardware-free `dlopen()` smoke test.
- `tests/cmd_test.c` — VCMD command-word + register encoding unit test.
- `tests/workbuf_test.c` — H.264 work-buffer layout/content unit test.
- `tests/hevcwb_test.c` — HEVC scaling-list work-buffer layout unit test.

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

Nothing here has been validated on real silicon yet. See
[`HARDWARE_VALIDATION.md`](HARDWARE_VALIDATION.md) for the staged hardware
validation checklist (prerequisites, VCMD/buffer gates, H.264/HEVC decode
progression, failure diagnosis, sign-off criteria).

## Roadmap

Done:

1. ~~Surfaces: DMA-BUF allocation + MMU map~~ — `src/vpu_buffer.c`.
2. ~~VCMD submission path: reserve → link/run → wait → release~~ — `src/vcmd.c`.

Remaining (the hard part):

3. **H.264 register sequence (core)** — DONE. `h264_regs.h` holds the real
   VC8000D register map; `vcmd_h264_build_decode_cmd()` programs dimensions,
   QP, CABAC, frame/POC, buffer addresses and DEC_E from VA-API params.
   Source: [alphagocc/vc8000d](https://github.com/alphagocc/vc8000d)
   `software/source/{common/8170table.h,h264high/h264hwd_asic.c}`.
4. **DPB reference list** — DONE. `h264_regs.h` has helpers for REFn_BASE
   (swreg 66..97), REFn_NBR (30..37), VALID/LTERM (39/38), REF0 POC (51..53)
   and the INIT/PINIT/BINIT reference list families. `vaEndPicture` maps
   `VAPictureH264[16]` and `RefPicList0/1` into these registers.
5. **Bitstream + work buffer DMA-BUFs** — DONE. `VASliceDataBufferType`
   buffers are backed by a GEM/DMA-BUF + MMU map (`alloc_tracked_buffer`),
   so the VCMD stream-base (swreg 20/21) uses a real address. Decoder work
   buffers (RLC/VLC incl. CABAC/scaling/POC, and MB control) are allocated
   lazily and sized from the frame's macroblock count per the SDK constants
   (`ensure_work_buffers`), wired to swreg 168/169 and 171. Graceful fallback
   to host memory when no render node is present.
6. **Work-buffer contents** — DONE. `src/work_buffer.c` fills the RLC/VLC
   work buffer: standard H.264 CABAC init table @0 (`h264_cabac_init.h`,
   920 u32 from the SDK), POC table @3680 (16 DPB top/bottom pairs + current
   POC per `h264PreparePOC`), scaling lists @3816 (6x4x4 + 2x8x8 packed
   big-endian per `h264PrepareScaleList`). Also programs `PICORD_COUNT_E`
   and `TYPE1_QUANT_E`.
7. **HEVC/H.265** — core DONE. `hevc_regs.h` (register map, DEC_MODE=12),
   `hevc_decode_params.h`, `vcmd_hevc_build_decode_cmd()` program the core
   SPS/PPS/slice registers, 15-slot DPB, reference lists and HEVC buffer
   aliases (stream 169/168, output Y 65/64 + C 99/98, scale list 171/170).
   `vaEndPicture` routes by profile. HEVC scaling lists are packed into a
   dedicated 1 KiB work buffer by `hevc_work_buffer.c` (DC@0, 4x4@16,
   8x8@112, 16x16@496, 32x32@880, column-by-column per the SDK) from
   `VAIQMatrixBufferHEVC`, with `HWIF_SCALING_LIST_E` programmed. RPS handling
   done: `VAPictureHEVC` RPS flags (ST_CURR_BEFORE / ST_CURR_AFTER / LT_CURR)
   build the hardware L0/L1 lists (`HevcInitRefPicList` order: L0=before+after+LT,
   L1=after+before+LT, padded to 16), and reference POC differences are written
   to `HWIF_CUR_POC_00..15` (swreg 46..49, signed 8-bit clipped). Remaining:
   less-common fields (tiles/SAO details) and real-hardware validation.
   `config/context` profile tables added for routing.
8. **AV1 — unsupported stub.** `VAProfileAV1Profile0` is advertised
   (`VAEntrypointVLD`), but decode is explicitly refused with
   `VA_STATUS_ERROR_UNSUPPORTED_PROFILE` in `vaEndPicture`. Rationale: the open
   VC8000D SDK has no AV1 source (`DEC_MODE` stops at AVS2=16, no AV1 registers
   in `8170table.h`), and the AOSC advisory states VPU AV1 decode produces
   abnormal pictures. No register values are invented — clients get a clean
   error and are directed to software dav1d. To implement real AV1 decode,
   an AV1-capable VPU SDK/datasheet for this chip is required.
9. **Encode path**: `VAEntrypointEncSlice` via `HANTROENC_IOC*` ioctls.
10. **Browser integration**: even with a working driver, the AOSC advisory notes
   Chromium currently fails to use VA-API on this VPU; mpv/GStreamer are easier
   early test targets.

### Alternative: CPU-side decode optimization
Independent of the VPU, software H.264 decode can be accelerated with
LoongArch LSX/LASX SIMD intrinsics. Reference:
[unofficial-loongarch-intrinsics-guide (lg200_wip)](https://github.com/phorcys/unofficial-loongarch-intrinsics-guide/tree/lg200_wip).
That path bypasses the VPU entirely (FFmpeg/libavcodec + LSX) and is a separate
project from this VA-API driver.

## Known limitations / caveats

- Only the kernel-mode driver is open source (DKMS); upstream targets Linux 6.6 (AOSC backports to 6.18). This machine runs Linux 7.1, so a matching kernel-module build is required.
- The `loongvpu` userspace package (closed) is not required for this scaffold.
- ABI structs must track any changes in the kernel driver's `include/hantro.h`.

## License

MIT for this scaffold. The upstream LoongVPU kernel driver is GPL-2.0 and the
VC8000D SDK is BSD-3-Clause; `h264_regs.h` contains only factual register
offsets/bit-widths extracted from the SDK's generated table (data, not
copyrightable expression). If you copy algorithmic code (e.g. H264RunAsic
logic) from those projects, keep their respective licenses in separate files.

## References

- AOSC announcement: https://aosc.io/news/2025-12-31-loonggpu-and-loongvpu-preview
- Kernel ABI source: https://github.com/AOSC-Tracking/loongvpu-kernel-dkms
- **VC8000D open SDK (H.264 register map + H264RunAsic)**: https://github.com/alphagocc/vc8000d
  — `software/source/common/8170table.h`, `software/source/common/regdrv.c`,
  `software/source/h264high/h264hwd_asic.c`, `software/linux/vcmd_pcidriver/`
- mainline Linux Hantro H.264 reference: `drivers/media/platform/verisilicon/hantro_g1_h264_dec.c`
- LoongArch SIMD intrinsics: https://github.com/phorcys/unofficial-loongarch-intrinsics-guide/tree/lg200_wip
- libva: https://github.com/intel/libva
