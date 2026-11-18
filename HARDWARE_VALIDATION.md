# LoongVPU Hardware Validation Checklist

This checklist is for validating `loongvpu-vaapi` on **real LoongVPU hardware**
(2K3000 / 3B6000M SoC, LG200 VPU, HW_ID `0x8001`). Nothing in this project has
run on silicon yet — on a machine without a LoongVPU render node the driver
soft-degrades to "decode disabled" and only the unit tests pass.

Work through the phases in order. Do not skip Phase 0/1: a "load OK" result
without `VCMD pool ready` means the driver is running in software-fallback
mode and no decode is happening.

---

## Phase 0 — Prerequisites

- [ ] Machine is 2K3000 / 3B6000M (LoongArch64) with the integrated VPU enabled.
- [ ] Kernel **VPU module is built and loaded**. The open DKMS driver targets
      Linux 6.6 (AOSC backports to 6.18); this project's dev machine runs
      Linux 7.1, so a matching module build may be required first.
      Check with `lsmod | grep -i vpu` / `dmesg | grep -i vpu`.
- [ ] Userspace VPU package installed (AOSC preview, from the advisory):
      `oma topics --opt-in loongvpu-0.1.0 && oma install loongvpu`
- [ ] Confirm HW_ID is VC8000D (`0x8001`), not G1 (`0x6731`) / G2 (`0x6732`) —
      the register maps in `h264_regs.h` / `hevc_regs.h` are for `0x8001` only.
      Read it via the kernel's ASIC-ID path or `/sys` if exposed.
- [ ] Current user can open the DRM render node (member of `video`/`render` group).
- [ ] This driver built from the current tree: `make clean && make && make test`
      (all 4 suites must pass — they are hardware-free).
- [ ] `LIBVA_DRIVERS_PATH` / `LIBVA_DRIVER_NAME` point at this build (NOT a
      system `loongvpu_drv_video.so`; check `/usr/lib/dri` for a stale copy).

**Known wiring constraint:** `loongvpu_drv_video.c` hardcodes
`LOONGVPU_RENDER_NODE = "/dev/dri/renderD128"`. On the target machine verify
which render node belongs to the VPU (the LG200 GPU may occupy `renderD128`):

```sh
ls -l /dev/dri/
for n in /dev/dri/renderD*; do echo "== $n"; udevadm info --query=all "$n" | grep -i driver; done
```

If the VPU is not `renderD128`, fix the macro (or make it configurable) before
continuing. This alone can explain every failure below.

---

## Phase 1 — Driver load (no decode)

```sh
LIBVA_DRIVERS_PATH=$PWD LIBVA_DRIVER_NAME=loongvpu vainfo
```

- [ ] `libva info: va_openDriver() returns 0`
- [ ] **stderr shows `loongvpu: VCMD pool ready`** — this is the single most
      important gate. If instead you see `VCMD pool unavailable (decode
      disabled)`, `GET_CMDBUF_PARAMETER` failed: the render node is wrong, the
      kernel module is absent/incompatible, or the VPU is not enabled. Stop
      here and fix Phase 0.
- [ ] Profiles listed: H.264 Baseline/Main/High, HEVC Main/Main10 (VLD+Enc),
      AV1 Profile 0 (VLD, advertised but refused at decode — by design).
- [ ] No `Failed to define max_*` / `No valid vtable entry` errors (those were
      fixed in the scaffold; seeing them means a stale build is loaded).

---

## Phase 2 — Buffer plumbing (GEM + MMU)

Run any decode (Phase 3) with stderr captured. Each of these ioctl steps logs a
specific failure string; none should appear:

- [ ] No `vpu_buffer: CREATE_DUMB failed` — GEM dumb allocation works.
- [ ] No `vpu_buffer: GETPADDR failed` — physical address lookup works.
- [ ] No `vpu_buffer: MAP_DUMB failed` / `mmap failed` — CPU mapping works.
- [ ] No `vpu_buffer: MMU_MEM_MAP failed` — **critical**: the VPU MMU accepts
      the buffer mapping. Without this, surface/stream/work addresses are
      meaningless to hardware.
- [ ] No `loongvpu: surface alloc failed at WxH` for a modest test frame.
- [ ] No `loongvpu: work buffers unavailable (no VPU)` — work buffers allocated.

To verify addresses are real (not all-zero), add a temporary debug print (or
gdb break) in `vaEndPicture` after buffers are prepared and check that
`stream_mmu`, `output_mmu`, `rlc_vlc_mmu`, `scale_list_mmu`, and DPB base
addresses are non-zero and distinct. If they are zero, the corresponding
buffer was not MMU-mapped.

---

## Phase 3 — VCMD submission mechanics

- [ ] `vcmd: RESERVE_CMDBUF failed` does not appear — a command buffer slot is
      reserved.
- [ ] `vcmd: LINK_RUN_CMDBUF failed` does not appear — the command stream is
      accepted by hardware (this is where a malformed WREG stream typically
      fails first).
- [ ] `vcmd: WAIT_CMDBUF failed` does not appear and the wait completes
      promptly (no hang). If it hangs: check that the stream ends with
      `OPCODE_END` and that `DEC_E` is set in the same shadow word as other
      bits (a prior bug here was fixed — see `vcmd_h264_build_decode_cmd`).
- [ ] `vcmd: RELEASE_CMDBUF failed` does not appear.
- [ ] Repeating decode many times does not exhaust the cmdbuf pool (slots are
      released each frame).

---

## Phase 4 — H.264 decode, progressive

Use small, controlled streams first; increase complexity only after the prior
step produces a correct image.

- [ ] **4a.** 640×480 I-frame only, no reference frames → a correct still image.
      This validates: DEC_MODE=0, dimensions, stream base, output base,
      CABAC/scaling work buffer contents, DEC_E, VCMD submission.
- [ ] **4b.** 640×480 with P-frames (short-term refs) → correct motion. Adds
      DPB base addresses, `VALID`/`LTERM` masks, REF POC, ref lists.
- [ ] **4c.** 640×480 with B-frames → correct order and no artifacts. Adds
      POC table (`work_buffer.c`) and B-list registers.
- [ ] **4d.** Enable scaling matrices in the stream → no visible difference
      vs. software decode (`h264PrepareScaleList` path, swreg packing).
- [ ] **4e.** 1080p, then 4K → correct output; note AOSC warns 4K performance
      is poor. Watch for `surface alloc failed` at higher resolutions.
- [ ] **4f.** Decode→display path: `mpv --hwdec=vaapi --vo=vaapi` (preferred
      early target). Chromium is known NOT to drive this VPU via VA-API per
      the AOSC advisory — do not treat Chromium failure as a driver bug.

Artifacts to look for: green/purple blocks, shifted chroma, missing macroblocks,
or a hang usually mean a wrong register value or a missing work-buffer field —
capture the generated command stream (see Debug Aids) and compare WREG values
with the VC8000D SDK's `H264RunAsic()` for the same SPS/PPS.

---

## Phase 5 — HEVC decode, progressive

- [ ] **5a.** 640×480 I-frame (e.g. IDR) → correct still image. Validates
      DEC_MODE=12, HEVC buffer aliases (stream 169/168, output Y 65/64,
      C 99/98), SPS/PPS field mapping.
- [ ] **5b.** 640×480 with P-frames and RPS → correct motion. Validates
      15-slot DPB, `RPS_ST_CURR_BEFORE/AFTER/LT_CURR` → L0/L1 construction,
      and `CUR_POC_00..15` POC differences.
- [ ] **5c.** B-frames → correct reference ordering (L1 = after+before+LT).
- [ ] **5d.** Stream with scaling lists → verify `HWIF_SCALING_LIST_E` and the
      1 KiB scale-list work buffer packing (DC@0, 4x4@16, 8x8@112,
      16x16@496, 32x32@880) produce correct output.
- [ ] **5e.** 10-bit Main10 stream → correct output (bit-depth fields set from
      `bit_depth_luma/chroma_minus8`; check output pixel format).
- [ ] **5f.** 1080p → correct performance/output.

Remaining known gaps that may show up here: tiles/SAO details, direct-mode MV,
and other less-common registers not programmed by the core builder. Record
any stream that fails with its SPS/PPS for later register work.

---

## Phase 6 — AV1 stub & regression

- [ ] `VAProfileAV1Profile0` is advertised but `vaEndPicture` returns
      `VA_STATUS_ERROR_UNSUPPORTED_PROFILE` with the log
      `loongvpu: AV1 decode is not supported by this driver...`. This is
      expected — no open AV1 VPU register map exists.
- [ ] H.264 still decodes correctly after the AV1 stub change (no regression in
      dispatcher routing).
- [ ] `make test` still passes all 4 suites on the target machine.
- [ ] `vaTerminate` / app exit is clean — no leaked GEM handles or stuck cmdbuf
      slots (watch `vpu_buffer: MMU_MEM_UNMAP failed` on teardown).

---

## Failure diagnosis quick table

| Symptom / log | Likely cause | First action |
|---|---|---|
| `cannot open /dev/dri/renderD128` | Wrong render node or permissions | Phase 0 render-node check; fix macro |
| `VCMD pool unavailable (decode disabled)` | GETPADDR/VCMD ioctl rejected; wrong node; module absent | Confirm HW_ID and kernel module; Phase 0 |
| `CREATE_DUMB failed` | Render node not a DRM/GEM device | Confirm VPU node |
| `MMU_MEM_MAP failed` | VPU MMU unavailable/incompatible | Kernel module version; check dmesg |
| `RESERVE/LINK_RUN_CMDBUF failed` | Malformed command stream or no VPU | Dump WREG stream; check END/DEC_E |
| `WAIT_CMDBUF` hangs | Stream never completes | Check END opcode, IRQ/arbiter config |
| Garbled image, correct size | Wrong register value / missing work-buffer data | Compare shadow regs with SDK |
| Only I-frames decode; P/B fail | DPB/RPS/POC path wrong | Phase 4b/4c and Phase 5b/5c |
| 4K slow or fails | Known AOSC limitation | Stay ≤1080p for correctness testing |
| AV1 refused | By design | Use software dav1d |

---

## Debug aids

The driver does not currently dump generated register streams. For hardware
debugging, add a temporary env-gated dump in `vcmd_h264_build_decode_cmd` /
`vcmd_hevc_build_decode_cmd` that prints each `(swreg, value)` WREG pair, then
diff it against the VC8000D SDK sequence for the same SPS/PPS. This is the
fastest way to localize a wrong register. Keep it behind an env var so normal
runs stay quiet, and remove or guard it before shipping.

---

## Sign-off criteria

- [ ] Phase 1–6 complete on real 2K3000/3B6000M hardware, HW_ID `0x8001`.
- [ ] At least one correct H.264 I/P/B and one correct HEVC I/P/B stream at
      640×480 and 1080p, verified visually against software decode.
- [ ] No `VCMD`, `vpu_buffer`, or MMU failures in stderr during a decode run.
- [ ] `make test` green and `vainfo` profile list as expected.
- [ ] Any failing stream recorded with its SPS/PPS and the dumped WREG stream
      for follow-up register work.
