#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "jailbreak-ref/src/idevice.h"
#include "jailbreak-ref/src/ibootim.h"
#include "jailbreak-ref/src/s5l8900_exploit.h"

#define KERNEL_LOAD_ADDRESS  0x09000000u
#define RAMDISK_LOAD_ADDRESS 0x09000000u
#define RAW_RAMDISK_LOAD_ADDRESS 0x09CC2000u

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
    int raw_ramdisk = 0;
    int first_path = 1;
    int logo_path = -1;
    int png_path = -1;
    if (argc == 7 && strcmp(argv[1], "--raw") == 0) {
        raw_ramdisk = 1;
        first_path = 2;
        logo_path = 3;
        png_path = 4;
    } else if (argc != 4) {
        fprintf(stderr,
            "Usage: %s RESTORE_RAMDISK DEVICE_TREE RESTORE_KERNELCACHE\n"
            "       %s --raw RAW_RAMDISK APPLE_LOGO PNG DEVICE_TREE RESTORE_KERNELCACHE\n",
            argv[0], argv[0]);
        return 2;
    }

    uint8_t *ramdisk = NULL;
    uint8_t *logo = NULL;
    uint8_t *png = NULL;
    uint8_t *kernel = NULL;
    uint8_t *device_tree = NULL;
    size_t ramdisk_size = 0;
    size_t logo_size = 0;
    size_t png_size = 0;
    size_t kernel_size = 0;
    size_t device_tree_size = 0;
    int device_tree_path = raw_ramdisk ? 5 : 2;
    int kernel_path = raw_ramdisk ? 6 : 3;
    if (read_file(argv[first_path], &ramdisk, &ramdisk_size) != 0 ||
        (raw_ramdisk && read_file(argv[logo_path], &logo, &logo_size) != 0) ||
        (raw_ramdisk && read_file(argv[png_path], &png, &png_size) != 0) ||
        read_file(argv[device_tree_path], &device_tree, &device_tree_size) != 0 ||
        read_file(argv[kernel_path], &kernel, &kernel_size) != 0) {
        free(ramdisk);
        free(logo);
        free(png);
        free(device_tree);
        free(kernel);
        return 1;
    }

    idevice_t device = {0};
    if (open_recovery_device(&device) != 0) {
        fprintf(stderr, "Unable to open the S5L8900 recovery device\n");
        free(ramdisk);
        free(logo);
        free(png);
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

    exploit_image_t exploit_logo = {0};
    uint8_t *raw_logo = NULL;
    if (raw_ramdisk) {
        payload_t logo_template = { .data = logo, .size = logo_size };
        payload_t logo_png = { .data = png, .size = png_size };
        size_t raw_logo_size = 0;
        if (ibootim_png_to_raw(&logo_png, &logo_template,
                &raw_logo, &raw_logo_size) != KERN_SUCCESS) {
            fprintf(stderr, "Unable to prepare recovery image payload\n");
            goto fail;
        }
        if (exploit_image_create(raw_logo, raw_logo_size,
                &exploit_logo) != KERN_SUCCESS) {
            fprintf(stderr, "Unable to prepare recovery exploit image\n");
            goto fail;
        }

        puts("Triggering the recovery signature bypass...");
        if (idevice_send_file(&device, exploit_logo.buf, exploit_logo.size,
                KERNEL_LOAD_ADDRESS) != KERN_SUCCESS ||
            send_command(&device, "setpicture 0\n") != 0 ||
            send_command(&device, "bgcolor 0 0 0\n") != 0) {
            fprintf(stderr, "Unable to initialize baseband maintenance boot\n");
            goto fail;
        }
    }

    uint32_t ramdisk_address = raw_ramdisk ? RAW_RAMDISK_LOAD_ADDRESS : RAMDISK_LOAD_ADDRESS;
    printf("Sending %srestore ramdisk (%zu bytes) to 0x%08x...\n",
        raw_ramdisk ? "raw " : "", ramdisk_size, ramdisk_address);
    if (idevice_send_file(&device, ramdisk, ramdisk_size, ramdisk_address) != KERN_SUCCESS) {
        fprintf(stderr, "Restore ramdisk transfer failed\n");
        goto fail;
    }

    char file_size_command[80];
    if (!raw_ramdisk) {
        snprintf(file_size_command, sizeof(file_size_command), "setenv filesize 0x%zx\n", ramdisk_size);
        if (send_command(&device, file_size_command) != 0 ||
            send_command(&device, "ramdisk\n") != 0) {
            goto fail;
        }
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
    if (raw_ramdisk) {
        snprintf(boot_args, sizeof(boot_args),
            "setenv boot-args \"rd=md0 pmd0=0x%08x.0x%zx%s\"\n",
            RAW_RAMDISK_LOAD_ADDRESS, ramdisk_size,
            getenv("IOS1_SERIAL_DEBUG") ? " -v serial=3" : "");
    } else if (getenv("IOS1_SERIAL_DEBUG")) {
        snprintf(boot_args, sizeof(boot_args),
            "setenv boot-args \"rd=md0 nand-enable-reformat=1 -progress -v serial=3\"\n");
    } else {
        snprintf(boot_args, sizeof(boot_args),
            "setenv boot-args \"rd=md0 nand-enable-reformat=1 -progress\"\n");
    }
    if (raw_ramdisk) {
        if (send_command(&device, "setenv auto-boot true\n") != 0 ||
            send_command(&device, "setenv boot-args \"\"\n") != 0 ||
            send_command(&device, "saveenv\n") != 0 ||
            send_command(&device, boot_args) != 0) {
            goto fail;
        }
    } else {
        if (send_command(&device, "setenv auto-boot false\n") != 0 ||
            send_command(&device, boot_args) != 0) {
            goto fail;
        }
    }

    puts(raw_ramdisk ? "Booting the baseband maintenance ramdisk..." :
                       "Booting the stock iOS restore ramdisk...");
    const char *boot_command = raw_ramdisk && !getenv("IOS1_RAW_BOOTX") ? "fsboot\n" : "bootx\n";
    if (send_command(&device, boot_command) != 0) {
        goto fail;
    }

    usleep(2000000);

    idevice_close(&device);
    exploit_image_free(&exploit_logo);
    free(raw_logo);
    free(ramdisk);
    free(logo);
    free(png);
    free(device_tree);
    free(kernel);
    return 0;

fail:
    idevice_close(&device);
    exploit_image_free(&exploit_logo);
    free(raw_logo);
    free(ramdisk);
    free(logo);
    free(png);
    free(device_tree);
    free(kernel);
    return 1;
}
