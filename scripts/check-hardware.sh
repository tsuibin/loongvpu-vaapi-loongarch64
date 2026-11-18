#!/usr/bin/env bash
#
# check-hardware.sh - one-shot LoongVPU enable + validation.
#
# Authenticates ONCE (sudo -v caches the credential for this session), then:
#   1. enables the AOSC LoongVPU preview topic
#   2. installs the VPU driver/userspace package
#   3. loads the kernel module
#   4. verifies the render node, VCMD pool, profiles and unit tests
#
# Run from a terminal (you will be asked for your password once):
#   bash scripts/check-hardware.sh
#
# Non-destructive options: pass --dry-run to only preview what oma would do,
# or --no-install to skip enable/install (verify whatever is already present).

set -u
cd "$(dirname "$0")/.." || exit 1

DRY_RUN=0
NO_INSTALL=0
for arg in "$@"; do
    case "$arg" in
        --dry-run)    DRY_RUN=1 ;;
        --no-install) NO_INSTALL=1 ;;
        -h|--help)    sed -n '2,20p' "$0"; exit 0 ;;
    esac
done

TOPIC="loongvpu-1.0.0"
PKG="loongvpu"
MODULES="ls2k3000_vpu loongvpu hantro"   # 0.1 DKMS name first, then fallbacks

PASS=0; FAIL=0
ok()   { echo "  [OK]   $*"; PASS=$((PASS+1)); }
bad()  { echo "  [FAIL] $*"; FAIL=$((FAIL+1)); }
info() { echo "  [info] $*"; }
sec()  { echo; echo "== $* =="; }

# --- single authentication ------------------------------------------------
sudo_v() {
    if sudo -n true 2>/dev/null; then return 0; fi
    echo "One password prompt for sudo (cached for this session):"
    sudo -v || { echo "sudo authentication failed"; exit 1; }
}

# --- privileged command wrapper -------------------------------------------
root() {
    if [ "$DRY_RUN" -eq 1 ]; then
        echo "  [dry-run] sudo $*"
    else
        sudo "$@"
    fi
}

sec "0. Environment"
echo "  kernel : $(uname -r)"
echo "  user   : $(id -un)"
echo "  display: ${DISPLAY:-<none>}"
command -v oma >/dev/null && ok "oma found" || bad "oma not found"
[ -f /dev/dri/renderD128 ] && info "renderD128 exists (may be GPU, not VPU)"

if [ "$NO_INSTALL" -eq 0 ]; then
    sec "1. Authenticate (once)"
    if [ "$DRY_RUN" -eq 0 ]; then sudo_v && ok "sudo authenticated"; fi

    sec "2. Enable LoongVPU preview topic"
    # --dry-run on oma topics avoids modifying sources during a dry run
    if [ "$DRY_RUN" -eq 1 ]; then
        root oma topics --dry-run --opt-in "$TOPIC" && ok "topic opt-in (dry-run)" || bad "topic opt-in preview failed"
    else
        root oma topics --opt-in "$TOPIC" && ok "topic $TOPIC enabled" || bad "topic enable failed"
    fi

    sec "3. Install $PKG package"
    root oma install -y "$PKG" && ok "package install step completed" || bad "package install failed"

    sec "4. Load kernel module"
    loaded=0
    for m in $MODULES; do
        if root modprobe "$m" 2>/dev/null; then
            ok "modprobe $m"; loaded=1; break
        else
            info "modprobe $m unavailable, trying next"
        fi
    done
    [ "$loaded" -eq 1 ] || bad "no VPU kernel module could be loaded"
    lsmod | grep -iE 'vpu|hantro|ls2k3000' | sed 's/^/    /' || true
else
    sec "1-4. Skipped (--no-install)"
fi

sec "5. Render node discovery"
make probe 2>&1 | sed 's/^/    /'
if [ "${PIPESTATUS[0]}" -eq 0 ]; then ok "VPU render node found"; else bad "no VPU render node"; fi

sec "6. libva driver load"
VOUT=$(LIBVA_DRIVERS_PATH="$PWD" LIBVA_DRIVER_NAME=loongvpu vainfo 2>&1)
echo "$VOUT" | grep -E 'loongvpu:|va_openDriver|Profile' | sed 's/^/    /'
echo "$VOUT" | grep -q 'va_openDriver() returns 0' && ok "libva loaded" || bad "libva did not load"
echo "$VOUT" | grep -q 'VPU render node =' && ok "VPU node selected" || bad "no VPU node in driver log"
echo "$VOUT" | grep -q 'VCMD pool ready' && ok "VCMD pool ready (DECODE LIVE)" || \
    { echo "$VOUT" | grep -q 'VCMD pool unavailable' && bad "VCMD pool unavailable (decode disabled)" || bad "no VCMD status in log"; }

sec "7. Unit tests (hardware-free)"
make test 2>&1 | grep -E 'PASSED|FAIL' | sed 's/^/    /'
[ "${PIPESTATUS[0]}" -eq 0 ] && ok "make test" || bad "make test"

sec "Summary"
echo "  PASS=$PASS  FAIL=$FAIL"
if [ "$FAIL" -eq 0 ] && echo "$VOUT" | grep -q 'VCMD pool ready'; then
    echo "  RESULT: HARDWARE DECODE READY — proceed to decode validation (checklist Phase 4/5)."
else
    echo "  RESULT: NOT READY. See FAIL lines above; consult HARDWARE_VALIDATION.md."
fi
exit "$FAIL"
