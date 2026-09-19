#!/bin/zsh
set -euo pipefail

ROOT=${0:A:h}
PROGRAM=${0:t}
ASSUME_YES=0
RESUME=0
BASEBAND_DOWNGRADE=0
ERASE_BASEBAND_ONLY=0

usage() {
    if (( ERASE_BASEBAND_ONLY )); then
        print -u2 "Usage: erase-ios1-baseband.sh --yes RESTORE.ipsw"
        print -u2 "  Erase the iPhone baseband firmware header and leave the device in recovery"
        exit 2
    fi
    print -u2 "Usage: $PROGRAM --yes [--resume] [--baseband-downgrade] RESTORE.ipsw"
    print -u2 "  --resume  Device is already running the stock restore ramdisk"
    print -u2 "  --baseband-downgrade  Erase the iPhone baseband firmware header before restore"
    exit 2
}

while (( $# )); do
    case "$1" in
        --yes) ASSUME_YES=1 ;;
        --resume) RESUME=1 ;;
        --baseband-downgrade) BASEBAND_DOWNGRADE=1 ;;
        --erase-baseband-only) BASEBAND_DOWNGRADE=1; ERASE_BASEBAND_ONLY=1 ;;
        -h|--help) usage ;;
        -*) usage ;;
        *) [[ -z ${IPSW:-} ]] || usage; IPSW=$1 ;;
    esac
    shift
done

if (( ! ASSUME_YES )); then
    if (( ERASE_BASEBAND_ONLY )); then
        print -u2 "Refusing a baseband erase without --yes"
    else
        print -u2 "Refusing an erase restore without --yes"
    fi
    exit 2
fi
[[ -n ${IPSW:-} && -f "$IPSW" ]] || usage
(( ! RESUME || ! BASEBAND_DOWNGRADE )) || {
    print -u2 "--resume and --baseband-downgrade cannot be used together"
    exit 2
}

REQUIRED_COMMANDS=(ioreg plutil unzip rg hdiutil openssl)
if (( ! ERASE_BASEBAND_ONLY )); then
    REQUIRED_COMMANDS+=(idevice_id tee)
fi
for command in "${REQUIRED_COMMANDS[@]}"; do
    command -v "$command" >/dev/null || {
        print -u2 "Missing runtime dependency: $command"
        exit 1
    }
done

if (( BASEBAND_DOWNGRADE )); then
    [[ -x "$ROOT/payloads/ios1-baseband-erase" ]] || {
        print -u2 "Missing baseband downgrade payload"
        exit 1
    }
    if (( ! ERASE_BASEBAND_ONLY )); then
        [[ -x "$ROOT/payloads/ios1-baseband-update" ]] || {
            print -u2 "Missing baseband update payload"
            exit 1
        }
    fi
    [[ -f "$ROOT/payloads/baseband-secpack.bin" ]] || {
        print -u2 "Missing baseband secpack"
        exit 1
    }
    [[ -x "$ROOT/scripts/prepare-baseband-ramdisk.sh" ]] || {
        print -u2 "Missing baseband ramdisk preparation tool"
        exit 1
    }
fi

REQUIRED_EXECUTABLES=(
    "$ROOT/build/ios1-enter-recovery"
    "$ROOT/build/legacy-boot-restore"
    "$ROOT/build/legacy-load-image"
    "$ROOT/build/ios1-recovery-command"
)
if (( ! ERASE_BASEBAND_ONLY )); then
    REQUIRED_EXECUTABLES+=(
        "$ROOT/build/ios1-exit-recovery"
        "$ROOT/usbmuxd-ref/src/usbmuxd"
        "$ROOT/idevicerestore-tihmstar/src/idevicerestore"
    )
fi
for executable in "${REQUIRED_EXECUTABLES[@]}"; do
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

[[ "$PRODUCT_TYPE" == N45AP ]] && PRODUCT_TYPE=iPod1,1

[[ "$PRODUCT_VERSION" == 1.* || "$PRODUCT_VERSION" == 1 ]] || {
    print -u2 "Unsupported IPSW version: $PRODUCT_VERSION"
    exit 1
}

case "$PRODUCT_TYPE" in
    iPhone1,1) BOARD=m68ap; NORMAL_PID=4752; RECOVERY_PID=4736 ;;
    iPod1,1) BOARD=n45ap; NORMAL_PID=4753; RECOVERY_PID=4736 ;;
    *) print -u2 "Unsupported iOS 1 product: $PRODUCT_TYPE"; exit 1 ;;
esac

if (( BASEBAND_DOWNGRADE )) && [[ "$PRODUCT_TYPE" != iPhone1,1 ]]; then
    print -u2 "Baseband erase is only supported for iPhone1,1"
    exit 1
fi

RAMDISK="$WORK/$RAMDISK_NAME"
KERNEL="$WORK/$KERNEL_NAME"
DEVICETREE="$WORK/DeviceTree.$BOARD.img2"
IBEC="$WORK/iBEC.$BOARD.RELEASE.dfu"
IBSS="$WORK/iBSS.$BOARD.RELEASE.dfu"

print "Preparing $PRODUCT_TYPE iOS $PRODUCT_VERSION ($BUILD_VERSION)"
unzip -p "$IPSW" "$RAMDISK_NAME" > "$RAMDISK"
unzip -p "$IPSW" "$KERNEL_NAME" > "$KERNEL"
unzip -p "$IPSW" "Firmware/all_flash/all_flash.$BOARD.production/DeviceTree.$BOARD.img2" > "$DEVICETREE"
if unzip -Z1 "$IPSW" | rg -qx "Firmware/dfu/iBEC.$BOARD.RELEASE.dfu"; then
    unzip -p "$IPSW" "Firmware/dfu/iBEC.$BOARD.RELEASE.dfu" > "$IBEC"
fi
if unzip -Z1 "$IPSW" | rg -qx "Firmware/dfu/iBSS.$BOARD.RELEASE.dfu"; then
    unzip -p "$IPSW" "Firmware/dfu/iBSS.$BOARD.RELEASE.dfu" > "$IBSS"
fi

has_pid() {
    ioreg -p IOUSB -l -w 0 | rg -q "\"idProduct\" = $1( |$)"
}

wait_for_pid() {
    local pid=$1 label=$2 timeout=${3:-90}
    for (( attempt = 0; attempt < timeout; attempt++ )); do
        has_pid "$pid" && return 0
        sleep 1
    done
    print -u2 "Timed out waiting for $label USB mode"
    return 1
}

wait_for_pid_to_leave() {
    local pid=$1 label=$2 timeout=${3:-30}
    for (( attempt = 0; attempt < timeout * 4; attempt++ )); do
        ! has_pid "$pid" && return 0
        sleep 0.25
    done
    print -u2 "Timed out waiting for $label USB mode to disconnect"
    return 1
}

enter_recovery() {
    for attempt in {1..5}; do
        "$ROOT/build/ios1-enter-recovery" && return 0
        sleep 1
    done
    return 1
}

load_target_recovery_image() {
    if [[ -s "$IBEC" ]]; then
        print "Loading the signed $BOARD iBEC..."
        "$ROOT/build/legacy-load-image" "$IBEC" 0x09000000 go
    elif [[ -s "$IBSS" ]]; then
        print "Loading the signed $BOARD iBSS..."
        "$ROOT/build/legacy-load-image" "$IBSS" 0x09000000 go
    else
        print -u2 "IPSW does not contain a signed $BOARD recovery image"
        return 1
    fi
    sleep 2
}

start_restore_mux() {
    MUX_ADDRESS=127.0.0.1:27016
    MUX_LOG="$WORK/usbmuxd.log"
    print "Starting the iOS 1 USB mux before the restore ramdisk boots..."
    "$ROOT/usbmuxd-ref/src/usbmuxd" -f -v -p -S "$MUX_ADDRESS" \
        -P "$WORK/usbmuxd.pid" > "$MUX_LOG" 2>&1 &
    MUX_PID=$!
}

stop_restore_mux() {
    if [[ -n ${MUX_PID:-} ]]; then
        kill "$MUX_PID" 2>/dev/null || true
        wait "$MUX_PID" 2>/dev/null || true
        MUX_PID=
    fi
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

    if (( BASEBAND_DOWNGRADE )); then
        print -u2 "WARNING: The baseband maintenance environment may reformat the iPhone NAND."
        print -u2 "All data on the device may be erased."

        load_target_recovery_image

        BASEBAND_RAMDISK="$WORK/baseband-ramdisk.raw"
        print "Preparing the baseband downgrade ramdisk..."
        "$ROOT/scripts/prepare-baseband-ramdisk.sh" erase "$RAMDISK" \
            "$BASEBAND_RAMDISK" >/dev/null

        "$ROOT/build/ios1-recovery-command" \
            "setenv ios1-baseband-erase-result pending" >/dev/null
        "$ROOT/build/ios1-recovery-command" saveenv >/dev/null
        print "Erasing the baseband firmware header..."
        IOS1_RAW_BOOTX=1 "$ROOT/build/legacy-boot-restore" --raw "$BASEBAND_RAMDISK" \
            "$ROOT/jailbreak-ref/src/bootlogo/template.img2" \
            "$ROOT/jailbreak-ref/src/bootlogo/boot-logo.png" \
            "$DEVICETREE" "$KERNEL"
        ERASE_SUCCEEDED=0
        ERASE_ENV=
        for _ in {1..240}; do
            if has_pid "$RECOVERY_PID"; then
                ERASE_ENV=$({ "$ROOT/build/ios1-recovery-command" printenv; } 2>/dev/null || true)
                if print -r -- "$ERASE_ENV" | rg -q \
                    "ios1-baseband-erase-result = 'success'"; then
                    ERASE_SUCCEEDED=1
                    break
                fi
            fi
            sleep 1
        done
        if (( ! ERASE_SUCCEEDED )); then
            if has_pid "$NORMAL_PID"; then
                print -u2 "Baseband firmware header erase failed; device returned to iOS"
            else
                print -u2 "Baseband firmware header erase did not complete"
            fi
            exit 1
        fi
        print "Baseband firmware header erased successfully."
        if (( ERASE_BASEBAND_ONLY )); then
            print "Baseband erase complete; the device is in recovery mode."
            exit 0
        fi

        load_target_recovery_image

        BASEBAND_UPDATE_RAMDISK="$WORK/baseband-update-ramdisk.raw"
        print "Preparing the target baseband update ramdisk..."
        "$ROOT/scripts/prepare-baseband-ramdisk.sh" update "$RAMDISK" \
            "$BASEBAND_UPDATE_RAMDISK" >/dev/null

        "$ROOT/build/ios1-recovery-command" \
            "setenv ios1-baseband-update-result pending" >/dev/null
        "$ROOT/build/ios1-recovery-command" saveenv >/dev/null
        print "Installing the target baseband firmware..."
        IOS1_RAW_BOOTX=1 "$ROOT/build/legacy-boot-restore" --raw \
            "$BASEBAND_UPDATE_RAMDISK" \
            "$ROOT/jailbreak-ref/src/bootlogo/template.img2" \
            "$ROOT/jailbreak-ref/src/bootlogo/boot-logo.png" \
            "$DEVICETREE" "$KERNEL"
        UPDATE_SUCCEEDED=0
        UPDATE_ENV=
        for _ in {1..360}; do
            if has_pid "$RECOVERY_PID"; then
                UPDATE_ENV=$({ "$ROOT/build/ios1-recovery-command" printenv; } 2>/dev/null || true)
                if print -r -- "$UPDATE_ENV" | rg -q \
                    "ios1-baseband-update-result = 'success'"; then
                    UPDATE_SUCCEEDED=1
                    break
                fi
            fi
            sleep 1
        done
        if (( ! UPDATE_SUCCEEDED )); then
            if has_pid "$NORMAL_PID"; then
                print -u2 "Target baseband update failed; device returned to iOS"
            else
                print -u2 "Target baseband update did not complete"
            fi
            exit 1
        fi
        print "Target baseband firmware installed successfully."
    fi

    load_target_recovery_image

    start_restore_mux
    print "Booting the stock restore ramdisk..."
    "$ROOT/build/legacy-boot-restore" "$RAMDISK" "$DEVICETREE" "$KERNEL"
    wait_for_pid "$NORMAL_PID" restored
else
    start_restore_mux
fi

print "Erasing and restoring $PRODUCT_TYPE..."
RESTORE_LOG="$WORK/idevicerestore.log"
RESTORE_STATUS=0
RESTORE_OPTIONS=(-R -e -y -P)
if (( ! BASEBAND_DOWNGRADE )) &&
    [[ "$PRODUCT_TYPE" == iPhone1,1 && "$PRODUCT_VERSION" == 1.0* ]]; then
    RESTORE_OPTIONS+=(-x)
fi
CUSTOM_MUX_READY=0
USE_SYSTEM_MUX=0
for mux_attempt in 1 2 3; do
    for _ in {1..30}; do
        if USBMUXD_SOCKET_ADDRESS=$MUX_ADDRESS idevice_id -l 2>/dev/null | rg -q .; then
            CUSTOM_MUX_READY=1
            break 2
        fi
        sleep 0.5
    done
    if (( mux_attempt < 3 )); then
        print "Retrying iOS 1 USB mux attachment..."
        stop_restore_mux
        start_restore_mux
    fi
done

if (( ! CUSTOM_MUX_READY )); then
    print -u2 "The private iOS 1 USB mux did not discover the restore ramdisk"
    tail -40 "$MUX_LOG" >&2
    stop_restore_mux
    for _ in {1..20}; do
        if idevice_id -l 2>/dev/null | rg -q .; then
            USE_SYSTEM_MUX=1
            break
        fi
        sleep 0.5
    done
    (( USE_SYSTEM_MUX )) || {
        print -u2 "Neither USB mux discovered the restore ramdisk"
        exit 1
    }
    print "Continuing through the system USB mux..."
fi

set +e
if (( USE_SYSTEM_MUX )); then
    "$ROOT/idevicerestore-tihmstar/src/idevicerestore" "${RESTORE_OPTIONS[@]}" "$IPSW" \
        2>&1 | tee "$RESTORE_LOG"
else
    USBMUXD_SOCKET_ADDRESS=$MUX_ADDRESS \
        "$ROOT/idevicerestore-tihmstar/src/idevicerestore" "${RESTORE_OPTIONS[@]}" "$IPSW" \
        2>&1 | tee "$RESTORE_LOG"
fi
RESTORE_STATUS=${pipestatus[1]}
set -e

if (( RESTORE_STATUS != 0 )); then
    if [[ "$PRODUCT_TYPE" == iPhone1,1 && "$PRODUCT_VERSION" == 1.0* &&
        ${IOS1_PARTITION_RETRY:-0} == 0 ]] &&
        rg -Uq 'Creating partition map \(0\)(?s:.*?)done\. \(status 2\)' "$RESTORE_LOG"; then
        print "Partition map initialization requires a second pass."
        cleanup
        print "Waiting for the device to return to recovery mode..."
        if has_pid "$NORMAL_PID"; then
            wait_for_pid_to_leave "$NORMAL_PID" restored 120
        fi
        wait_for_pid "$RECOVERY_PID" recovery 120
        print "Retrying the restore..."
        trap - EXIT INT TERM
        IOS1_PARTITION_RETRY=1 exec "$0" --yes "$IPSW"
    fi
    exit "$RESTORE_STATUS"
fi

if (( BASEBAND_DOWNGRADE )) &&
    rg -Fq "a later version is already installed, can't rollback" "$RESTORE_LOG"; then
    print -u2 "Baseband downgrade failed: the stock updater still rejected rollback"
    exit 1
fi

if (( ! BASEBAND_DOWNGRADE )) && [[ "$PRODUCT_TYPE" == iPhone1,1 ]] &&
    rg -Fq "a later version is already installed, can't rollback" "$RESTORE_LOG"; then
    print "The installed baseband is newer; booting the restored OS..."
    wait_for_pid "$RECOVERY_PID" recovery 120
    "$ROOT/build/ios1-exit-recovery"
    wait_for_pid "$NORMAL_PID" restored 120
fi

print "Restore complete: $PRODUCT_TYPE iOS $PRODUCT_VERSION ($BUILD_VERSION)"
