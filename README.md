# iOS 1 Restore

Restore iPhone OS 1.x to the original iPhone and first-generation iPod touch
from a modern Mac.

Supported devices:

- iPhone1,1
- iPod1,1

The restore process uses libusb for recovery and iOS 1 lockdownd access, a
local usbmuxd instance for `restored` and ASR, and a patched version of
[tihmstar/idevicerestore](https://github.com/tihmstar/idevicerestore).
The low-level USB code comes from
[EthanArbuckle/iOS1.0-Jailbreak](https://github.com/EthanArbuckle/iOS1.0-Jailbreak).

Tested configurations:

- iPhone1,1 — iPhone OS 1.0 (1A543a)
- iPod1,1 — iPhone OS 1.1 (3A101a)

## Requirements

- macOS
- Xcode command-line tools
- Homebrew

Install the build dependencies:

```sh
brew install autoconf automake libimobiledevice libtool libusb openssl@3 pkg-config ripgrep
```

## Build

```sh
./scripts/bootstrap.sh
```

This downloads pinned revisions of the required projects, applies the patches
in `patches/`, and builds the restore tools.

## Restore

Connect the device in normal or recovery mode and run:

```sh
./restore-ios1.sh --yes /path/to/Restore.ipsw
```

If the device is already running the restore ramdisk:

```sh
./restore-ios1.sh --yes --resume /path/to/Restore.ipsw
```

The restore erases the device. IPSW files are not included.

## iPod touch setup

To clear the initial Connect to iTunes screen on an iPod touch:

```sh
./activate-ios1.sh
```

This sets `iTunesHasConnected` through lockdownd. It does not activate an
original iPhone; the iPhone requires an activation record or hacktivation.

## Notes

- The iPhone OS 1.0 restore protocol is handled separately from 1.1 and later.
- Restoring 1.0 preserves the currently installed baseband firmware.
