#!/bin/zsh
set -euo pipefail

ROOT=${0:A:h:h}

if (( $# != 3 )); then
    print -u2 "Usage: ${0:t} erase|update RESTORE_RAMDISK OUTPUT_RAW_RAMDISK"
    exit 2
fi

MODE=$1
INPUT=$2
OUTPUT=$3
ERASER="$ROOT/payloads/ios1-baseband-erase"
UPDATER="$ROOT/payloads/ios1-baseband-update"
SECPACK="$ROOT/payloads/baseband-secpack.bin"

[[ -f "$INPUT" ]] || { print -u2 "Missing restore ramdisk: $INPUT"; exit 1; }
[[ ! -e "$OUTPUT" ]] || { print -u2 "Output already exists: $OUTPUT"; exit 1; }

case "$MODE" in
    erase)
        [[ -x "$ERASER" ]] || { print -u2 "Missing device payload: $ERASER"; exit 1; }
        [[ -f "$SECPACK" ]] || { print -u2 "Missing baseband secpack: $SECPACK"; exit 1; }
        ;;
    update)
        [[ -x "$UPDATER" ]] || { print -u2 "Missing device payload: $UPDATER"; exit 1; }
        ;;
    *)
        print -u2 "Unknown baseband ramdisk mode: $MODE"
        exit 2
        ;;
esac

[[ "$(dd if="$INPUT" bs=1 count=4 2>/dev/null)" == 8900 ]] || {
    print -u2 "Restore ramdisk does not have an 8900 container"
    exit 1
}

DATA_SIZE=$(od -An -tu4 -j 12 -N 4 "$INPUT" | tr -d ' ')
IMAGE_TYPE=$(od -An -tu1 -j 7 -N 1 "$INPUT" | tr -d ' ')
FILE_SIZE=$(stat -f %z "$INPUT")
(( DATA_SIZE > 0 && DATA_SIZE <= FILE_SIZE - 2048 )) || {
    print -u2 "Invalid restore ramdisk payload size"
    exit 1
}
(( DATA_SIZE % 2048 == 0 )) || {
    print -u2 "Unaligned restore ramdisk payload"
    exit 1
}

case "$IMAGE_TYPE" in
    3)
        dd if="$INPUT" bs=2048 skip=1 count=$(( DATA_SIZE / 2048 )) 2>/dev/null |
            openssl enc -d -aes-128-cbc -nopad \
                -K 188458A6D15034DFE386F23B61D43774 \
                -iv 00000000000000000000000000000000 > "$OUTPUT"
        ;;
    4)
        dd if="$INPUT" of="$OUTPUT" bs=2048 skip=1 \
            count=$(( DATA_SIZE / 2048 )) 2>/dev/null
        ;;
    *)
        print -u2 "Unsupported restore ramdisk image type: $IMAGE_TYPE"
        exit 1
        ;;
esac

MOUNT_POINT=$(mktemp -d "${TMPDIR:-/tmp}/ios1-baseband-ramdisk.XXXXXX")
DISK_DEVICE=
cleanup() {
    if [[ -n ${DISK_DEVICE:-} ]]; then
        hdiutil detach "$DISK_DEVICE" >/dev/null 2>&1 || true
    fi
    rmdir "$MOUNT_POINT" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

ATTACH_OUTPUT=$(hdiutil attach -readwrite -nobrowse \
    -imagekey diskimage-class=CRawDiskImage -mountpoint "$MOUNT_POINT" "$OUTPUT")
DISK_DEVICE=$(print -r -- "$ATTACH_OUTPUT" | awk 'NR == 1 { print $1 }')
[[ -n "$DISK_DEVICE" && -d "$MOUNT_POINT" ]] || {
    print -u2 "Unable to mount temporary restore ramdisk"
    exit 1
}

PLIST="$MOUNT_POINT/System/Library/LaunchDaemons/com.apple.restored_external.plist"
[[ -f "$PLIST" ]] || {
    print -u2 "Restore ramdisk does not contain restored_external"
    exit 1
}

mkdir -p "$MOUNT_POINT/usr/local/bin" "$MOUNT_POINT/usr/local/share" \
    "$MOUNT_POINT/dev" "$MOUNT_POINT/mnt1" "$MOUNT_POINT/mnt2"

/usr/libexec/PlistBuddy -c "Delete :ProgramArguments" "$PLIST"
/usr/libexec/PlistBuddy -c "Add :ProgramArguments array" "$PLIST"

if [[ "$MODE" == erase ]]; then
    cp "$ERASER" "$MOUNT_POINT/usr/local/bin/ios1-baseband-erase"
    cp "$SECPACK" "$MOUNT_POINT/usr/local/share/baseband-secpack.bin"
    chmod 755 "$MOUNT_POINT/usr/local/bin/ios1-baseband-erase"
    chmod 644 "$MOUNT_POINT/usr/local/share/baseband-secpack.bin"

    /usr/libexec/PlistBuddy -c \
        "Add :ProgramArguments:0 string /usr/local/bin/ios1-baseband-erase" "$PLIST"
    /usr/libexec/PlistBuddy -c \
        "Add :ProgramArguments:1 string --automatic" "$PLIST"
else
    FIRMWARE_DIR="$MOUNT_POINT/usr/local/standalone/firmware"
    FLS_FILES=("$FIRMWARE_DIR"/ICE*.fls(N))
    EEP_FILES=("$FIRMWARE_DIR"/ICE*.eep(N))
    (( ${#FLS_FILES} == 1 && ${#EEP_FILES} == 1 )) || {
        print -u2 "Restore ramdisk does not contain one baseband FLS/EEP pair"
        exit 1
    }

    FLS_NAME=${FLS_FILES[1]:t}
    EEP_NAME=${EEP_FILES[1]:t}
    [[ ${FLS_NAME:r} == ${EEP_NAME:r} ]] || {
        print -u2 "Restore ramdisk baseband FLS/EEP names do not match"
        exit 1
    }

    cp "$UPDATER" "$MOUNT_POINT/usr/local/bin/ios1-baseband-update"
    chmod 755 "$MOUNT_POINT/usr/local/bin/ios1-baseband-update"

    /usr/libexec/PlistBuddy -c \
        "Add :ProgramArguments:0 string /usr/local/bin/ios1-baseband-update" "$PLIST"
    /usr/libexec/PlistBuddy -c \
        "Add :ProgramArguments:1 string /usr/local/standalone/firmware/$FLS_NAME" "$PLIST"
    /usr/libexec/PlistBuddy -c \
        "Add :ProgramArguments:2 string /usr/local/standalone/firmware/$EEP_NAME" "$PLIST"
fi

sync
hdiutil detach "$DISK_DEVICE" >/dev/null
DISK_DEVICE=
trap - EXIT INT TERM
rmdir "$MOUNT_POINT"

print "$OUTPUT"
