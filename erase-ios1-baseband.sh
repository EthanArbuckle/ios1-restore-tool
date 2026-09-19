#!/bin/zsh
set -euo pipefail

ROOT=${0:A:h}
exec "$ROOT/restore-ios1.sh" --erase-baseband-only "$@"
