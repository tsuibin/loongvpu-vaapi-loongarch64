# Enable CMA for LoongVPU — GRUB boot cmdline instructions

## Why this is needed

The VPU kernel module (`ls2k3000_vpu`) allocates a **contiguous memory pool**
at init via `alloc_pages_exact()` (order-10 = 4 MiB contiguous blocks). On this
machine that fails at boot/runtime:

```
modprobe: page allocation failure: order:10, mode:0x100(__GFP_ZERO)
[<...>] alloc_pages_pool+0xec/0x200 [ls2k3000_vpu]
[<...>] hantro_mem_pool_init+0x24/0x50 [ls2k3000_vpu]
Alloc reserve memory pool failed.
```

Root cause: the kernel has `CONFIG_CMA=y` but **no CMA region is reserved**
(`CONFIG_CMA_SIZE_MBYTES=0`, `CmaTotal: 0 kB`). Contiguous memory must be
reserved at boot — a running system cannot satisfy order-10 allocations
reliably. Adding `cma=256M` to the kernel cmdline creates the pool the VPU
needs. This requires a **reboot**.

## Environment on this machine (verified)

- UEFI boot, GRUB2
- `/etc/default/grub` sets `GRUB_CMDLINE_LINUX_DEFAULT="quiet rd.auto rd.auto=1 splash"`,
  `GRUB_CMDLINE_LINUX=""`
- `grub.cfg` at `/boot/grub/grub.cfg`, regenerated with `update-grub`
- 4 installed kernels get initrd/GRUB entries (7.1.13 & 7.1.7, 16k & 4k)

## Procedure (requires root — run in your own terminal)

### 1. Back up the GRUB defaults

```bash
sudo cp /etc/default/grub /etc/default/grub.bak-loongvpu
```

### 2. Add `cma=256M` to the kernel cmdline

Edit `/etc/default/grub` with your editor (`sudo nano /etc/default/grub`).
Find the line:

```
GRUB_CMDLINE_LINUX_DEFAULT="quiet rd.auto rd.auto=1 splash"
```

Change it to append `cma=256M` **inside the quotes**:

```
GRUB_CMDLINE_LINUX_DEFAULT="quiet rd.auto rd.auto=1 splash cma=256M"
```

Alternatively, put it in `GRUB_CMDLINE_LINUX=""` → `GRUB_CMDLINE_LINUX="cma=256M"`
so it also applies to recovery boots. Either works; the `_DEFAULT` form is
the minimal change.

Non-interactive one-liner (review the diff before trusting it):

```bash
sudo sed -i 's/^\(GRUB_CMDLINE_LINUX_DEFAULT="[^"]*\)"/\1 cma=256M"/' /etc/default/grub
grep GRUB_CMDLINE_LINUX_DEFAULT /etc/default/grub   # verify exactly one cma=256M
```

### 3. Regenerate GRUB config

```bash
sudo update-grub
```

Watch for "找到内核 ..." lines (it will regenerate all 4 kernels' entries).

### 4. Verify the cmdline is present in grub.cfg

```bash
grep -o 'cma=256M' /boot/grub/grub.cfg | head
```

You should see matches. If none, the edit didn't take — redo step 2.

### 5. Reboot

```bash
sudo reboot
```

### 6. Verify CMA is now reserved

```bash
cat /proc/cmdline | tr ' ' '\n' | grep cma       # -> cma=256M
grep -i cma /proc/meminfo                         # -> CmaTotal: ~262144 kB
```

`CmaTotal` must be non-zero. If it is 0, the cmdline didn't apply — check that
you booted the entry you regenerated (GRUB_DEFAULT=saved may have booted a
different kernel) and that `cma=` isn't overridden elsewhere.

### 7. Re-run the hardware check

```bash
cd ~/loongvpu-vaapi
bash scripts/check-hardware.sh --no-install
```

`--no-install` skips the topic/package steps (already done) and re-runs module
load + probe + libva + tests. Success looks like:

```
loongvpu: VPU render node = /dev/dri/renderD<N>
loongvpu: VCMD pool ready
RESULT: HARDWARE DECODE READY
```

If the module still fails: run `sudo dmesg | grep -iE 'hantro|alloc_pages_pool|cma'`
and check whether the order-10 allocation now succeeds. If the pool init passes
but no render node appears, the Hantro driver may only expose `card1` (minor 1)
and not a `renderD*` node — in that case force it with
`LOONGVPU_RENDER_NODE=/dev/dri/card1` and re-run; if the ioctl segfaults, the
memory pool still isn't healthy and CMA size may need increasing (try `cma=512M`).

## Sizing notes

- `cma=256M` is a conservative starting point. The VPU pool failure was a single
  order-10 (4 MiB) block, but the decoder needs working buffers (DPB, RLC/VLC,
  scaling) whose total can grow with resolution. 256 MiB is ample for 1080p;
  for 4K validation consider 512 MiB.
- `cma=` creates a **global** CMA area shared by all CMA users, so it does not
  steal memory permanently — it is reserved from available RAM and returned
  when unused. With 16 GiB total, 256–512 MiB is negligible.

## Rollback

```bash
sudo cp /etc/default/grub.bak-loongvpu /etc/default/grub
sudo update-grub
sudo reboot
```

## Why I did not apply this myself

This changes `/etc/default/grub` and the boot configuration (system state) and
requires a reboot — outside the user-level, no-root preference, and it needs
your explicit authorization. All steps above are for you to run.
