CC ?= clang
BUILD_DIR := build
JB := jailbreak-ref
JB_COMMON := $(JB)/src/common
BREW_PREFIX ?= $(shell brew --prefix)
LIBUSB := $(BREW_PREFIX)/opt/libusb
OPENSSL := $(BREW_PREFIX)/opt/openssl@3
LIBUSBMUXD_CFLAGS := $(shell pkg-config --cflags libusbmuxd-2.0)
LIBUSBMUXD_LIBS := $(shell pkg-config --libs libusbmuxd-2.0)

CPPFLAGS += -I$(OPENSSL)/include -I$(JB)/deps/include -I$(JB)/src -I$(LIBUSB)/include $(LIBUSBMUXD_CFLAGS)
# The iOS 1 mux code is not reliable with optimization enabled.
CFLAGS += -O0 -Wall -Wextra
LDLIBS := -L$(LIBUSB)/lib -lusb-1.0 -L$(OPENSSL)/lib -lssl -lcrypto \
	$(LIBUSBMUXD_LIBS) -framework CoreFoundation -framework IOKit -framework Security
LOCKDOWN_SOURCES := $(JB_COMMON)/lockdownd.c $(JB_COMMON)/mux.c $(JB_COMMON)/plist.c
IBOOT_SOURCES := $(JB)/src/idevice.c
EXPLOIT_SOURCES := $(JB)/src/s5l8900_exploit.c
IBOOTIM_SOURCES := $(JB)/src/ibootim.c

.PHONY: all clean dependencies ios1-device-tools

all: $(BUILD_DIR)/ios1-enter-recovery $(BUILD_DIR)/ios1-exit-recovery $(BUILD_DIR)/ios1-activate $(BUILD_DIR)/legacy-load-image \
	$(BUILD_DIR)/legacy-boot-restore $(BUILD_DIR)/ios1-recovery-command

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/ios1-enter-recovery: ios1-enter-recovery.c $(LOCKDOWN_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/ios1-exit-recovery: ios1-exit-recovery.c $(IBOOT_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/ios1-activate: ios1-activate.c $(LOCKDOWN_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/legacy-load-image: legacy-load-image.c $(IBOOT_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/ios1-recovery-command: ios1-recovery-command.c $(IBOOT_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/legacy-boot-restore: legacy-boot-restore.c $(IBOOT_SOURCES) $(EXPLOIT_SOURCES) $(IBOOTIM_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -L$(JB)/deps/lib -lpng -lz $(LDLIBS) -o $@

dependencies:
	./scripts/bootstrap.sh

IOS1_DEVICE_CC ?= clang
IOS1_SYSROOT ?=
IOS1_CRT ?=
IOS1_LD_DIR ?=
IOS1_LIBGCC ?= $(IOS1_SYSROOT)/usr/lib/gcc/arm-apple-darwin9/4.0.1/libgcc.a
IOS1_HOST_TOOLCHAIN_LIB ?= $(abspath $(dir $(shell xcrun --find clang))/../lib)

ios1-device-tools: $(BUILD_DIR)/ios1-baseband-erase $(BUILD_DIR)/ios1-baseband-update

$(BUILD_DIR)/ios1-baseband-erase: ios1-baseband-erase.c | $(BUILD_DIR)
	@test -d "$(IOS1_SYSROOT)" || { echo "Set IOS1_SYSROOT to an iPhone OS 1 SDK" >&2; exit 1; }
	@test -f "$(IOS1_CRT)" || { echo "Set IOS1_CRT to an armv6 crt1.o" >&2; exit 1; }
	@test -x "$(IOS1_LD_DIR)/ld" || { echo "Set IOS1_LD_DIR to the ios1-ld directory" >&2; exit 1; }
	DYLD_LIBRARY_PATH="$(IOS1_HOST_TOOLCHAIN_LIB)" \
	$(IOS1_DEVICE_CC) -arch armv6 -mthumb -isysroot "$(IOS1_SYSROOT)" \
		-B"$(IOS1_LD_DIR)" -miphoneos-version-min=1.0 -nostdlib \
		-Wall -Wextra -Os -L"$(IOS1_SYSROOT)/usr/lib" -lSystem \
		"$(IOS1_CRT)" "$<" "$(IOS1_LIBGCC)" -o "$@"

$(BUILD_DIR)/ios1-baseband-update: ios1-baseband-update.c | $(BUILD_DIR)
	@test -d "$(IOS1_SYSROOT)" || { echo "Set IOS1_SYSROOT to an iPhone OS 1 SDK" >&2; exit 1; }
	@test -f "$(IOS1_CRT)" || { echo "Set IOS1_CRT to an armv6 crt1.o" >&2; exit 1; }
	@test -x "$(IOS1_LD_DIR)/ld" || { echo "Set IOS1_LD_DIR to the ios1-ld directory" >&2; exit 1; }
	DYLD_LIBRARY_PATH="$(IOS1_HOST_TOOLCHAIN_LIB)" \
	$(IOS1_DEVICE_CC) -arch armv6 -mthumb -isysroot "$(IOS1_SYSROOT)" \
		-B"$(IOS1_LD_DIR)" -miphoneos-version-min=1.0 -nostdlib \
		-Wall -Wextra -Os -L"$(IOS1_SYSROOT)/usr/lib" -lSystem \
		"$(IOS1_CRT)" "$<" "$(IOS1_LIBGCC)" -o "$@"

clean:
	rm -rf $(BUILD_DIR)
