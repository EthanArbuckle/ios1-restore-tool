# iOS 1 Restore

Restore iPhone OS 1.x from a modern Mac.

Supported devices:

- iPhone1,1
- iPod1,1

## Requirements

- macOS
- Xcode command-line tools
- Homebrew

Install the build dependencies:

```sh
brew install autoconf automake libimobiledevice libirecovery libtool libusb openssl@3 pkg-config ripgrep
```

## Build

```sh
./scripts/bootstrap.sh
```

## Restore

### Normal restore

Connect the device in normal or recovery mode and run:

```sh
./restore-ios1.sh --yes /path/to/Restore.ipsw
```

### Baseband downgrade

For an original iPhone already running 1.x, connect it in normal or recovery
mode and run:

```sh
./restore-ios1.sh --yes --baseband-downgrade /path/to/iPhone1,1_Restore.ipsw
```

For a phone running 2.x or 3.x, enter hardware DFU first and use the same
command. This path has been tested from 2.2.1 and 3.1.3 to 1.0. DFU also works
from 1.x, but performs an extra restore pass.

### Baseband erase only

To erase the original iPhone's baseband firmware header without restoring iOS:

```sh
./erase-ios1-baseband.sh --yes /path/to/iPhone1,1_Restore.ipsw
```

This leaves the phone in recovery with no working cellular radio. It may also
reformat the phone's NAND and erase all data.

## iPod touch setup

To clear the initial Connect to iTunes screen on an iPod touch:

```sh
./activate-ios1.sh
```

This does not activate an original iPhone.

## Notes

- By default, restoring 1.0 preserves the currently installed baseband firmware.
- Filesystem verification may remain silent for several minutes after the
  progress reaches 100%. Do not interrupt the restore.

If the restore ramdisk is not discovered, disable Apple's device updater before
retrying:

```sh
launchctl disable gui/$(id -u)/com.apple.mobiledeviceupdater
launchctl bootout gui/$(id -u)/com.apple.mobiledeviceupdater 2>/dev/null || true
pkill -x MobileDeviceUpdater 2>/dev/null || true
```

Re-enable it after restoring, then log out or restart macOS:

```sh
launchctl enable gui/$(id -u)/com.apple.mobiledeviceupdater
```

## Credits

Uses a patched version of
[tihmstar's idevicerestore](https://github.com/tihmstar/idevicerestore) and
recovery and lockdownd code from
[iOS1.0-Jailbreak](https://github.com/EthanArbuckle/iOS1.0-Jailbreak).
