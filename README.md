# iOS 1 Restore

Restore iPhone OS 1.x to the original iPhone and first-generation iPod touch
from a modern Mac.

Supported devices:

- iPhone1,1
- iPod1,1

The restore process uses libusb for recovery and iOS 1 lockdownd access,
usbmux for `restored` and ASR, and a patched version of
[tihmstar/idevicerestore](https://github.com/tihmstar/idevicerestore).
The low-level USB code comes from
[EthanArbuckle/iOS1.0-Jailbreak](https://github.com/EthanArbuckle/iOS1.0-Jailbreak).

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

When downgrading an original iPhone to an older baseband, run:

```sh
./restore-ios1.sh --yes --baseband-downgrade /path/to/iPhone1,1_Restore.ipsw
```

This erases the installed baseband firmware header, installs the baseband from
the selected IPSW, and then performs the normal operating-system restore.

## Baseband erase

To erase the original iPhone's baseband firmware header without restoring iOS:

```sh
./erase-ios1-baseband.sh --yes /path/to/iPhone1,1_Restore.ipsw
```

The IPSW supplies the signed iPhone restore environment; its operating system is
not installed. The command verifies the erase and leaves the iPhone in recovery
mode. The cellular radio will remain unavailable until a subsequent restore
installs baseband firmware.

The maintenance environment may reformat the iPhone's NAND when its filesystem
format is incompatible with the selected IPSW. Treat the erase-only command as
capable of erasing all data on the device.

## iPod touch setup

To clear the initial Connect to iTunes screen on an iPod touch:

```sh
./activate-ios1.sh
```

This sets `iTunesHasConnected` through lockdownd. It does not activate an
original iPhone; the iPhone requires an activation record or hacktivation.

## Notes

- The iPhone OS 1.0 restore protocol is handled separately from 1.1 and later.
- By default, restoring 1.0 preserves the currently installed baseband firmware.
