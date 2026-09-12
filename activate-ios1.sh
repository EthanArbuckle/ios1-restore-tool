#!/bin/zsh
set -u

ROOT=${0:A:h}
[[ -x "$ROOT/build/ios1-activate" ]] || {
    print -u2 "Missing activation helper; run ./scripts/bootstrap.sh first"
    exit 1
}

for attempt in 1 2; do
    "$ROOT/build/ios1-activate" && exit 0
done

print -u2 "The iOS 1 device did not accept the iTunes connection flag."
exit 1
