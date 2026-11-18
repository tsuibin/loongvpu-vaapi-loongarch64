# Browser hardware decode via LoongVPU / VA-API

How Chromium and Firefox reach the LoongVPU hardware decoder on
2K3000 / 3B6000M (LG200) machines, what is actually accelerated, and how to
verify it. All commands and results below were checked on 2026-11-18 with
kernel `7.1.7-aosc-main-16k`, Chromium `148.0.7778.180` and Firefox `160.0`.
A second pass re-ran the verification steps in live X11 browser sessions; see
[§9 Verification record](#9-verification-record-2026-11-18) for the evidence.

## 中文速览

- 浏览器不直接操作 VPU。调用链是 `<video>` → 浏览器解码进程 → FFmpeg/VA-API
  → `libva` → `*_drv_video.so` → `/dev/dri/renderD*` → DRM 驱动 → VPU。
- 本机默认加载的是闭源 `loonggpu_drv_video.so`（`loongvpu` 包），ffmpeg/mpv
  硬解已实测可用；本项目的开源 `loongvpu_drv_video.so` 目前**找不到 VPU
  render node**，浏览器暂时用不了它。
- Chromium 148 一般免 flag；Firefox 160 需要在 `about:config` 里强制开启。
- 浏览器走的是 VA-API surface → DMABUF → EGL 合成器的零拷贝路径，**不需要**
  `hwdownload`。所以「ffmpeg 取帧回内存失败」不影响浏览器播放。

---

## 1. The call chain

```
web page <video>  (H.264 / HEVC / AV1)
        │
        ▼
Chromium GPU process / Firefox RDD process
        │  FFmpeg VA-API hwaccel ( VaapiVideoDecoder / FFmpegVideo )
        ▼
libva  ──dlopen──▶  /usr/lib/dri/*_drv_video.so
                        │  DRM ioctl
                        ▼
                 /dev/dri/renderD128
                        │
                        ▼
        loonggpu DRM + ls2k3000_vpu / hantro
                        │
                        ▼
        VPU decoder 00:06.3 / encoder 00:06.4
```

Decoded frames stay as VA-API surfaces and are handed to the compositor via
DMABUF/EGL (zero copy). They are **not** copied back to system memory. That is
why `ffmpeg -vf hwdownload` fails on this driver (`Function not implemented`)
while `mpv --vo=vaapi` and browser playback work fine. Do not use `hwdownload`
failure as evidence that hardware decode is broken.

## 2. Which VA-API driver is in play

Two different drivers exist on this machine; browsers will pick whichever
`libva` resolves, which matters a lot here.

| Driver | Source | Selected by | Status on this machine |
|---|---|---|---|
| `loonggpu_drv_video.so` | closed, `loongvpu` package | default (`libva` auto-probe) | **works**, verified with `vainfo`, ffmpeg, mpv |
| `loongvpu_drv_video.so` | this project | `LIBVA_DRIVER_NAME=loongvpu` + `LIBVA_DRIVERS_PATH=$PWD` | not hardware-validated; `make probe` reports `No LoongVPU render node found` |

Check which one a process will load:

```sh
vainfo 2>&1 | grep 'Trying to open'
# libva info: Trying to open /usr/lib/dri/loonggpu_drv_video.so
```

The open scaffold currently expects a dedicated VPU DRM render node that
answers `DRM_IOCTL_HANTRO_GET_VCMDSUP`. This machine only exposes
`/dev/dri/renderD128` (the LG200 GPU node), so a browser launched with
`LIBVA_DRIVER_NAME=loongvpu` will not get hardware decode. See
[`HARDWARE_VALIDATION.md`](../HARDWARE_VALIDATION.md) Phase 0/1 before trying.

## 3. What will actually be hardware decoded

`vainfo` on the closed driver reports:

| Codec | VA profile | HW decode | Browser consequence |
|---|---|---|---|
| H.264 | Baseline / Main / High / High10 | yes | hardware path on both browsers |
| HEVC | Main / Main10 | yes | hardware path; Chromium reports HEVC Main/Main10 decode from the driver |
| AV1 | Profile 0 | yes (closed driver) | hardware path; the open scaffold refuses it by design |
| VP8 / VP9 | — | **no** | always software decoded |
| JPEG | Baseline | yes | not used by `<video>` playback |

YouTube and similar sites prefer VP9 or AV1. If you specifically want the VPU
path, force H.264 with an extension such as `h264ify` / `enhanced-h264ify`,
otherwise a "hardware decoder: false" result may simply mean the site served
VP9/AV1 while only H.264/HEVC are wired up in the browser.

## 4. Prerequisites on this machine (verified)

```sh
uname -r                              # 7.1.7-aosc-main-16k
lsmod | grep -i vpu                   # ls2k3000_vpu
lspci -nnk -s 00:06.3                 # VPU decoder, driver in use: hantro
grep -i cma /proc/meminfo             # CmaTotal: 262144 kB (256M)
cat /proc/cmdline                     # ... cma=256M
ls -l /dev/dri/renderD128             # crw-rw-rw- root render
vainfo | grep -E 'VLD|VAEntrypoint'   # H.264 / HEVC / AV1 profiles
```

`cma=256M` is already in `GRUB_CMDLINE_LINUX_DEFAULT`; without it the VPU
cannot allocate its contiguous memory pool and every decode path is dead.
See [`CMA_BOOT_SETUP.md`](CMA_BOOT_SETUP.md) for background.

Validate the VA-API stack outside the browser first — this isolates driver
problems from browser problems:

```sh
mpv --no-config --vo=vaapi --hwdec=vaapi --ao=null /path/to/h264.mp4
# expect: "Using hardware decoding (vaapi)", VO: [vaapi] ... vaapi[nv12]

ffmpeg -hide_banner -loglevel verbose \
  -hwaccel vaapi -hwaccel_device /dev/dri/renderD128 \
  -vaapi_device /dev/dri/renderD128 \
  -i /path/to/h264.mp4 -an -f null -
# expect: "Reinit context to ..., pix_fmt: vaapi", 0 decode errors
```

## 5. Chromium 148

Since Chromium 143, Linux VA-API decode works out of the box in the normal
desktop session. On this machine Chromium 148.0.7778.180 was **confirmed to
use `VaapiVideoDecoder` with no flags at all** (see §9) — start with a clean
profile and no `~/.config/chromium-flags.conf`.

If a broken profile leaves `chrome://gpu` reporting software-only, clear GPU
state first: `rm -rf ~/.config/chromium/Default/GPUCache ~/.config/chromium/Default/ShaderCache`.
Only then add a flags file (one `--enable-features` line; multiple features
are comma-separated):

```sh
# ~/.config/chromium-flags.conf  (fallback only)
--enable-features=VaapiVideoDecoder
```

Feature names actually present in this build (checked against
`/usr/lib/chromium/chromium`): `VaapiVideoDecoder`, `VaapiIgnoreDriverChecks`,
`AcceleratedVideoDecodeLinuxGL`. Older guides mentioning
`VaapiVideoDecodeLinuxGL`, `UseChromeOSDirectVideoDecoder` or
`PlatformHEVCDecoderSupport` are for other builds — those strings do **not**
exist in this binary, so do not copy those flags here. Additional fallbacks:

- `--enable-features=VaapiVideoDecoder,VaapiIgnoreDriverChecks` — bypass the
  driver allowlist.
- `--enable-features=AcceleratedVideoDecodeLinuxGL` — alternative GL-based
  decode path.
- `LIBVA_DRI3_DISABLE=1 chromium` — old X11 GPUs where DRI3 breaks VA-API.
- `--use-gl=egl` — if ANGLE cannot initialize a GL display.

**About the AOSC advisory:** it says Chromium fails to use this VPU via
VA-API, but that is **outdated for the current stack**. With the closed
`loonggpu_drv_video.so` and Chromium 148, H.264 playback was verified to use
`VaapiVideoDecoder` (`kIsPlatformVideoDecoder: true`). If a future Chromium
regresses, validate with `mpv`/`ffmpeg` first: if those work, treat it as a
Chromium issue rather than a broken VPU driver.

Verify in the real X11 session (headless `chrome://gpu` is not representative —
ANGLE Vulkan initialization fails there and the GPU process exits):

1. `chrome://gpu` → "Video Decode: Hardware accelerated".
2. DevTools (`F12`) → **Media** tab → select the player → "Hardware decoder:
   true" and a `VaapiVideoDecoder` / VA-API decoder name.
3. `chrome://media-internals` → `kVideoDecoderName` should be
   `VaapiVideoDecoder`, not `FFmpegVideoDecoder` / `VpxVideoDecoder` /
   `Dav1dVideoDecoder`.

## 6. Firefox 160

Firefox uses FFmpeg in the RDD process, with VA-API as the hwaccel backend.
Hardware decode is auto-enabled for Intel (115+) and AMD (136+); Loongson is
not on those allowlists, so force it:

`about:config` → set:

| Pref | Value | Why |
|---|---|---|
| `media.hardware-video-decoding.force-enabled` | `true` | required on non-Intel/AMD GPUs (Firefox 137+ name); confirmed present in Firefox 160 |
| `gfx.webrender.software` | `false` | otherwise GPU rendering is disabled |
| `layers.acceleration.force-enabled` | `true` | if compositing falls back to basic |

Restart Firefox. `gfx.webrender.enabled` and `media.ffmpeg.vaapi.enabled` are
**not present** in this Firefox 160 build (checked in `libxul.so` and
`omni.ja`) — do not waste time setting them; WebRender is on by default and
the hardware-decode pref above is the one that matters.

Optional: select the render node explicitly if auto-detection picks wrong:

```sh
MOZ_DRM_DEVICE=/dev/dri/renderD128 firefox
```

Verify:

1. `about:support` → **Media** → `HARDWARE_VIDEO_DECODING` present, and the
   codec table shows H.264/HEVC as `Hardware` / `Supported`.
2. `about:support` → Graphics → Compositing is WebRender, GPU process running.
3. Live log while playing a video (run in the real X11 session — headless
   Firefox uses SWGL and is not representative):

   ```sh
   MOZ_LOG="FFmpegVideo:5" firefox /path/to/h264.mp4 2>&1 | grep -iE 'vaapi|IsHardwareAccelerated'
   ```

   Expected lines, all observed on this machine:

   ```
   FFMPEG: Initialising VA-API FFmpeg decoder
   FFMPEG: Using preferred hardware codec h264
   FFMPEG: VA-API FFmpeg init successful
   FFMPEG: FFmpegVideoDecoder, init, IsHardwareAccelerated=true
   FFMPEG: Requesting pixel format VAAPI_VLD
   FFMPEG: VA-API frame pts=...
   ```

   Error codes `FEATURE_HARDWARE_VIDEO_DECODING_DISABLE` or
   `FEATURE_FAILURE_VIDEO_DECODING_TEST_FAILED` in `about:support` mean the
   force-enabled pref above was not applied.

## 7. Troubleshooting

| Symptom | Likely cause | Action |
|---|---|---|
| `vainfo` picks the wrong driver | `LIBVA_DRIVER_NAME` set in environment | unset it for the closed driver, or point it at this repo for the scaffold |
| `No LoongVPU render node found` | kernel VPU module absent / no VPU DRM node | load module, re-check `make probe`; see HARDWARE_VALIDATION Phase 0 |
| `CmaTotal: 0` | boot param missing | add `cma=256M` to GRUB cmdline and reboot |
| Chromium "Video Decode: Software only" | flags / driver allowlist / AOSC platform limitation | try flags in §5, then validate with mpv/ffmpeg |
| Firefox decode disabled | non-Intel/AMD GPU not allowlisted | `media.hardware-video-decoding.force-enabled=true` |
| Playback uses software on VP9/AV1 sites | codec unsupported by driver | force H.264 via h264ify extension |
| `hwdownload` fails with -38 | driver cannot export surfaces to system memory | expected; irrelevant to browsers, use VAAPI render path |
| `chrome://gpu` blank in headless | ANGLE Vulkan init failure in headless mode | check in the real X11 desktop session |

## 8. Known limitations

- The closed driver works for playback but cannot export decoded frames to
  system memory, so FFmpeg filters/transcoding that need CPU-side frames fail.
- The open scaffold in this repo is **not** hardware-validated and currently
  sees no VPU render node; it is not a browser-ready driver yet.
- AOSC notes 4K decode performance is poor; stay at 1080p for correctness work.
- Full browser playback involves the GPU/compositor stack as well as VPU
  decode; a working `vainfo`/mpv result is necessary but not always sufficient.

## 9. Verification record (2026-11-18)

Re-checked in live sessions on kernel `7.1.7-aosc-main-16k` using the closed
`loonggpu_drv_video.so` and a generated 1280x720 H.264 clip.

| Step | Command / method | Result |
|---|---|---|
| Prerequisites (§4) | `uname -r`, `lsmod`, `lspci -nnk -s 00:06.3`, `/proc/meminfo`, `/proc/cmdline`, `ls -l /dev/dri/renderD128` | all pass: `ls2k3000_vpu` loaded, `hantro` bound, `CmaTotal=262144 kB`, `cma=256M`, `renderD128` mode 666 |
| Driver selection (§2) | `vainfo 2>&1 \| grep 'Trying to open'` | `/usr/lib/dri/loonggpu_drv_video.so` |
| CLI decode (§4) | `mpv --no-config --vo=vaapi --hwdec=vaapi --ao=null h264.mp4` | `Using hardware decoding (vaapi)`, `VO: [vaapi] ... vaapi[nv12]`, exit 0 |
| CLI decode (§4) | `ffmpeg -hwaccel vaapi -hwaccel_device /dev/dri/renderD128 ... -f null -` | `pix_fmt: vaapi`, 240 frames decoded, 0 decode errors |
| Open scaffold (§2) | `make probe` | `No LoongVPU render node found` — not browser-ready |
| Chromium GPU status (§5) | CDP read of `chrome://gpu` shadow DOM in X11 | `Video Decode: Hardware accelerated`; decode list includes h264 baseline/main/high, HEVC main/main10, AV1 profile main; GL is ANGLE OpenGL on LoongGPU LG200 |
| Chromium real decode (§5) | CDP: play H.264, then read `chrome://media-internals` | `kVideoDecoderName: "VaapiVideoDecoder"`, `kIsPlatformVideoDecoder: true`, `pipeline_state: kPlaying`, no flags used |
| Chromium feature names (§5) | `grep -ao` on `/usr/lib/chromium/chromium` | `VaapiVideoDecoder`, `VaapiIgnoreDriverChecks`, `AcceleratedVideoDecodeLinuxGL` present; `VaapiVideoDecodeLinuxGL`, `UseChromeOSDirectVideoDecoder`, `PlatformHEVCDecoderSupport` absent |
| Firefox prefs (§6) | search `libxul.so` + `omni.ja` | `media.hardware-video-decoding.force-enabled`, `gfx.webrender.software`, `layers.acceleration.force-enabled`, `MOZ_DRM_DEVICE` present; `gfx.webrender.enabled`, `media.ffmpeg.vaapi.enabled` absent |
| Firefox real decode (§6) | `MOZ_LOG="FFmpegVideo:5" firefox --profile <pref-set> h264.mp4` | `Initialising VA-API FFmpeg decoder`, `VA-API FFmpeg init successful`, `IsHardwareAccelerated=true`, `Requesting pixel format VAAPI_VLD`, continuous `VA-API frame` lines |

Headless browser checks are not representative: headless Chromium fails ANGLE
Vulkan initialization and headless Firefox uses SWGL with WebGL context loss.
Use a real desktop session for the browser verification steps.

## References

- AOSC advisory: https://aosc.io/news/2025-12-31-loonggpu-and-loongvpu-preview
- Chromium VA-API documentation: https://github.com/chromium/chromium/blob/main/docs/gpu/vaapi.md
- ArchWiki — Chromium hardware video acceleration: https://wiki.archlinux.org/title/Hardware_video_acceleration
- ArchWiki — Firefox hardware video acceleration: https://wiki.archlinux.org/title/Firefox#Hardware_video_acceleration
