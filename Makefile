CC ?= clang
BUILD_DIR := build
JB := jailbreak-ref
JB_COMMON := $(JB)/src/common
BREW_PREFIX ?= $(shell brew --prefix)
LIBUSB := $(BREW_PREFIX)/opt/libusb
OPENSSL := $(BREW_PREFIX)/opt/openssl@3

CPPFLAGS += -I$(OPENSSL)/include -I$(JB)/deps/include -I$(JB)/src -I$(LIBUSB)/include
# The iOS 1 mux code is not reliable with optimization enabled.
CFLAGS += -O0 -Wall -Wextra
LDLIBS := -L$(LIBUSB)/lib -lusb-1.0 -L$(OPENSSL)/lib -lssl -lcrypto \
	-framework CoreFoundation -framework IOKit -framework Security
LOCKDOWN_SOURCES := $(JB_COMMON)/lockdownd.c $(JB_COMMON)/mux.c $(JB_COMMON)/plist.c
IBOOT_SOURCES := $(JB)/src/idevice.c

.PHONY: all clean dependencies

all: $(BUILD_DIR)/ios1-enter-recovery $(BUILD_DIR)/ios1-activate $(BUILD_DIR)/legacy-load-image \
	$(BUILD_DIR)/legacy-boot-restore

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/ios1-enter-recovery: ios1-enter-recovery.c $(LOCKDOWN_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/ios1-activate: ios1-activate.c $(LOCKDOWN_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/legacy-load-image: legacy-load-image.c $(IBOOT_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/legacy-boot-restore: legacy-boot-restore.c $(IBOOT_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

dependencies:
	./scripts/bootstrap.sh

clean:
	rm -rf $(BUILD_DIR)
