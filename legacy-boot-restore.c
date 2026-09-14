#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "jailbreak-ref/src/idevice.h"

#define KERNEL_LOAD_ADDRESS  0x09000000u
#define RAMDISK_LOAD_ADDRESS 0x09000000u

static int read_file(const char *path, uint8_t **data, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Unable to open %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    *data = malloc((size_t)length);
    if (!*data || fread(*data, 1, (size_t)length, file) != (size_t)length) {
        free(*data);
        *data = NULL;
        fclose(file);
        return -1;
    }
    fclose(file);
    *size = (size_t)length;
    return 0;
}

static int open_recovery_device(idevice_t *device) {
    for (int attempt = 0; attempt < 40; attempt++) {
        if (idevice_open(device) == KERN_SUCCESS) {
            if (idevice_init_handshake(device) == KERN_SUCCESS) {
                return 0;
            }
            idevice_close(device);
            memset(device, 0, sizeof(*device));
        }
        usleep(250000);
    }
    return -1;
}

static int send_command(idevice_t *device, const char *command) {
    printf("iBoot: %s", command);
    if (idevice_send_command(device, command) != KERN_SUCCESS) {
        return -1;
    }
    char response[16384];
    kern_return_t response_result = idevice_get_response(device, response, sizeof(response));
    if (response_result == KERN_SUCCESS) {
        printf("%s", response);
    } else {
        fprintf(stderr, "Unable to read iBoot response after '%s' (0x%x)\n", command, response_result);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s RESTORE_RAMDISK DEVICE_TREE RESTORE_KERNELCACHE\n", argv[0]);
        return 2;
    }

    uint8_t *ramdisk = NULL;
    uint8_t *kernel = NULL;
    uint8_t *device_tree = NULL;
    size_t ramdisk_size = 0;
    size_t kernel_size = 0;
    size_t device_tree_size = 0;
    if (read_file(argv[1], &ramdisk, &ramdisk_size) != 0 ||
        read_file(argv[2], &device_tree, &device_tree_size) != 0 ||
        read_file(argv[3], &kernel, &kernel_size) != 0) {
        free(ramdisk);
        free(device_tree);
        free(kernel);
        return 1;
    }

    idevice_t device = {0};
    if (open_recovery_device(&device) != 0) {
        fprintf(stderr, "Unable to open the S5L8900 recovery device\n");
        free(ramdisk);
        free(device_tree);
        free(kernel);
        return 1;
    }
    char initial_response[16384];
    kern_return_t initial_result = idevice_get_response(&device, initial_response, sizeof(initial_response));
    if (initial_result == KERN_SUCCESS) {
        printf("%s", initial_response);
    } else {
        fprintf(stderr, "Unable to read initial iBoot prompt (0x%x)\n", initial_result);
    }

    printf("Sending restore ramdisk (%zu bytes) to 0x%08x...\n", ramdisk_size, RAMDISK_LOAD_ADDRESS);
    if (idevice_send_file(&device, ramdisk, ramdisk_size, RAMDISK_LOAD_ADDRESS) != KERN_SUCCESS) {
        fprintf(stderr, "Restore ramdisk transfer failed\n");
        goto fail;
    }

    char file_size_command[80];
    snprintf(file_size_command, sizeof(file_size_command), "setenv filesize 0x%zx\n", ramdisk_size);
    if (send_command(&device, file_size_command) != 0 ||
        send_command(&device, "ramdisk\n") != 0) {
        goto fail;
    }

    printf("Sending device tree (%zu bytes) to 0x%08x...\n", device_tree_size, KERNEL_LOAD_ADDRESS);
    if (idevice_send_file(&device, device_tree, device_tree_size, KERNEL_LOAD_ADDRESS) != KERN_SUCCESS) {
        fprintf(stderr, "Device-tree transfer failed\n");
        goto fail;
    }
    snprintf(file_size_command, sizeof(file_size_command), "setenv filesize 0x%zx\n", device_tree_size);
    if (send_command(&device, file_size_command) != 0 ||
        send_command(&device, "devicetree\n") != 0) {
        goto fail;
    }

    printf("Sending restore kernelcache (%zu bytes) to 0x%08x...\n", kernel_size, KERNEL_LOAD_ADDRESS);
    if (idevice_send_file(&device, kernel, kernel_size, KERNEL_LOAD_ADDRESS) != KERN_SUCCESS) {
        fprintf(stderr, "Restore kernelcache transfer failed\n");
        goto fail;
    }
    snprintf(file_size_command, sizeof(file_size_command), "setenv filesize 0x%zx\n", kernel_size);
    if (send_command(&device, file_size_command) != 0) {
        goto fail;
    }

    char boot_args[320];
    snprintf(boot_args, sizeof(boot_args),
        "setenv boot-args \"rd=md0 nand-enable-reformat=1 -progress\"\n");
    if (send_command(&device, "setenv auto-boot false\n") != 0 ||
        send_command(&device, boot_args) != 0) {
        goto fail;
    }

    puts("Booting the stock iOS restore ramdisk...");
    if (send_command(&device, "bootx\n") != 0) {
        goto fail;
    }

    idevice_close(&device);
    free(ramdisk);
    free(device_tree);
    free(kernel);
    return 0;

fail:
    idevice_close(&device);
    free(ramdisk);
    free(device_tree);
    free(kernel);
    return 1;
}
