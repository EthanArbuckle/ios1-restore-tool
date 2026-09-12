#!/bin/zsh
set -euo pipefail

ROOT=${0:A:h:h}
JB_REV=41d0bdf895eae181a8323ccb6464027521ba8336
RESTORE_REV=c25aefd49b3769c2907875e15566d433d59bd979
USBMUXD_REV=3ded00c9985a5108cfc7591a309f9a23d57a8cba

clone_at() {
    local url=$1 rev=$2 destination=$3
    if [[ ! -d "$destination/.git" ]]; then
        git clone "$url" "$destination"
        git -C "$destination" checkout --detach "$rev"
    fi
}

apply_once() {
    local repository=$1 patch=$2 marker=$3
    if rg -q "$marker" "$repository"; then
        return
    fi
    git -C "$repository" apply "$patch"
}

for command in git rg clang make autoconf automake glibtoolize pkg-config; do
    command -v "$command" >/dev/null || {
        print -u2 "Missing build dependency: $command"
        exit 1
    }
done

clone_at https://github.com/EthanArbuckle/iOS1.0-Jailbreak.git "$JB_REV" "$ROOT/jailbreak-ref"
clone_at https://github.com/tihmstar/idevicerestore.git "$RESTORE_REV" "$ROOT/idevicerestore-tihmstar"
clone_at https://github.com/libimobiledevice/usbmuxd.git "$USBMUXD_REV" "$ROOT/usbmuxd-ref"

apply_once "$ROOT/jailbreak-ref" "$ROOT/patches/jailbreak-legacy-response.patch" 'idevice_get_response'
apply_once "$ROOT/jailbreak-ref" "$ROOT/patches/jailbreak-activation.patch" 'lockdownd_set_value_bool'
apply_once "$ROOT/idevicerestore-tihmstar" "$ROOT/patches/idevicerestore-modern-ios1.patch" 'ASR_RECEIVE_TIMEOUT_MS'

make -C "$ROOT" all

if [[ ! -x "$ROOT/usbmuxd-ref/src/usbmuxd" ]]; then
    (
        cd "$ROOT/usbmuxd-ref"
        ./autogen.sh --without-preflight --without-systemd
        make -j"$(sysctl -n hw.logicalcpu)"
    )
fi

(
    cd "$ROOT/idevicerestore-tihmstar"
    if [[ ! -f Makefile ]]; then
        ./autogen.sh
    fi
    make -j"$(sysctl -n hw.logicalcpu)"
)

print "iOS 1 restore dependencies are ready."
