#!/bin/zsh
set -euo pipefail

ROOT=${0:A:h}
PROGRAM=${0:t}
ASSUME_YES=0
RESUME=0

usage() {
    print -u2 "Usage: $PROGRAM --yes [--resume] RESTORE.ipsw"
    print -u2 "  --resume  Device is already running the stock restore ramdisk"
    exit 2
}

while (( $# )); do
    case "$1" in
        --yes) ASSUME_YES=1 ;;
        --resume) RESUME=1 ;;
        -h|--help) usage ;;
        -*) usage ;;
        *) [[ -z ${IPSW:-} ]] || usage; IPSW=$1 ;;
    esac
    shift
done

(( ASSUME_YES )) || { print -u2 "Refusing an erase restore without --yes"; exit 2; }
[[ -n ${IPSW:-} && -f "$IPSW" ]] || usage

for command in ioreg plutil unzip rg idevice_id; do
    command -v "$command" >/dev/null || {
        print -u2 "Missing runtime dependency: $command"
        exit 1
    }
done

for executable in "$ROOT/build/ios1-enter-recovery" \
    "$ROOT/build/legacy-load-image" "$ROOT/build/legacy-boot-restore" \
    "$ROOT/usbmuxd-ref/src/usbmuxd" "$ROOT/idevicerestore-tihmstar/src/idevicerestore"; do
    [[ -x "$executable" ]] || {
        print -u2 "Missing $executable; run ./scripts/bootstrap.sh first"
        exit 1
    }
done

WORK=$(mktemp -d "${TMPDIR:-/tmp}/ios1-restore.XXXXXX")
MUX_PID=
cleanup() {
    if [[ -n ${MUX_PID:-} ]]; then
        kill "$MUX_PID" 2>/dev/null || true
        wait "$MUX_PID" 2>/dev/null || true
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

unzip -p "$IPSW" Restore.plist > "$WORK/Restore.plist"
plist_value() { plutil -extract "$1" raw -o - "$WORK/Restore.plist"; }

PRODUCT_TYPE=$(plist_value ProductType)
PRODUCT_VERSION=$(plist_value ProductVersion)
BUILD_VERSION=$(plist_value ProductBuildVersion)
RAMDISK_NAME=$(plist_value RestoreRamDisks.User)
KERNEL_NAME=$(plist_value RestoreKernelCaches.Release)

[[ "$PRODUCT_VERSION" == 1.* || "$PRODUCT_VERSION" == 1 ]] || {
    print -u2 "Unsupported IPSW version: $PRODUCT_VERSION"
    exit 1
}

case "$PRODUCT_TYPE" in
    iPhone1,1) BOARD=m68ap; NORMAL_PID=4752; RECOVERY_PID=4736 ;;
    iPod1,1) BOARD=n45ap; NORMAL_PID=4753; RECOVERY_PID=4736 ;;
    *) print -u2 "Unsupported iOS 1 product: $PRODUCT_TYPE"; exit 1 ;;
esac

RAMDISK="$WORK/$RAMDISK_NAME"
KERNEL="$WORK/$KERNEL_NAME"
DEVICETREE="$WORK/DeviceTree.$BOARD.img2"
IBEC="$WORK/iBEC.$BOARD.RELEASE.dfu"

print "Preparing $PRODUCT_TYPE iOS $PRODUCT_VERSION ($BUILD_VERSION)"
unzip -p "$IPSW" "$RAMDISK_NAME" > "$RAMDISK"
unzip -p "$IPSW" "$KERNEL_NAME" > "$KERNEL"
unzip -p "$IPSW" "Firmware/all_flash/all_flash.$BOARD.production/DeviceTree.$BOARD.img2" > "$DEVICETREE"
if unzip -Z1 "$IPSW" | rg -qx "Firmware/dfu/iBEC.$BOARD.RELEASE.dfu"; then
    unzip -p "$IPSW" "Firmware/dfu/iBEC.$BOARD.RELEASE.dfu" > "$IBEC"
fi

has_pid() {
    ioreg -p IOUSB -l -w 0 | rg -q "\"idProduct\" = $1( |$)"
}

wait_for_pid() {
    local pid=$1 label=$2
    for _ in {1..90}; do
        has_pid "$pid" && return 0
        sleep 1
    done
    print -u2 "Timed out waiting for $label USB mode"
    return 1
}

enter_recovery() {
    for attempt in 1 2; do
        "$ROOT/build/ios1-enter-recovery" && return 0
        sleep 1
    done
    return 1
}

if (( ! RESUME )); then
    if has_pid "$NORMAL_PID"; then
        print "Requesting recovery mode over the iOS 1 USB/lockdownd protocol..."
        enter_recovery
        wait_for_pid "$RECOVERY_PID" recovery
    elif ! has_pid "$RECOVERY_PID"; then
        print -u2 "No matching $PRODUCT_TYPE found in normal or recovery mode"
        exit 1
    fi

    if [[ -s "$IBEC" ]]; then
        print "Loading the signed $BOARD iBEC..."
        "$ROOT/build/legacy-load-image" "$IBEC" 0x09000000 go
        sleep 2
    fi

    print "Booting the stock restore ramdisk..."
    "$ROOT/build/legacy-boot-restore" "$RAMDISK" "$DEVICETREE" "$KERNEL"
    wait_for_pid "$NORMAL_PID" restored
fi

print "Erasing and restoring $PRODUCT_TYPE..."
USE_SYSTEM_MUX=0
for _ in {1..10}; do
    if idevice_id -l 2>/dev/null | rg -q .; then
        USE_SYSTEM_MUX=1
        break
    fi
    sleep 1
done

if (( USE_SYSTEM_MUX )); then
    "$ROOT/idevicerestore-tihmstar/src/idevicerestore" -R -e -y -P "$IPSW"
else
    MUX_ADDRESS=127.0.0.1:27016
    MUX_LOG="$WORK/usbmuxd.log"

    for attempt in {1..3}; do
        "$ROOT/usbmuxd-ref/src/usbmuxd" -f -v -p -S "$MUX_ADDRESS" \
            -P "$WORK/usbmuxd.pid" > "$MUX_LOG" 2>&1 &
        MUX_PID=$!
        for _ in {1..10}; do
            if USBMUXD_SOCKET_ADDRESS=$MUX_ADDRESS idevice_id -l 2>/dev/null | rg -q .; then
                break 2
            fi
            sleep 1
        done
        kill "$MUX_PID" 2>/dev/null || true
        wait "$MUX_PID" 2>/dev/null || true
        MUX_PID=
    done

    [[ -n ${MUX_PID:-} ]] || {
        print -u2 "Unable to claim the iOS 1 usbmux interface"
        tail -40 "$MUX_LOG" >&2
        exit 1
    }

    USBMUXD_SOCKET_ADDRESS=$MUX_ADDRESS \
        "$ROOT/idevicerestore-tihmstar/src/idevicerestore" -R -e -y -P "$IPSW"
fi

print "Restore complete: $PRODUCT_TYPE iOS $PRODUCT_VERSION ($BUILD_VERSION)"
