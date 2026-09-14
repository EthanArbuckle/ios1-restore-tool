#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "jailbreak-ref/src/idevice.h"

static int read_file(const char *path, uint8_t **data, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Unable to open %s: %s\n", path, strerror(errno));
        return -1;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (length <= 0) {
        fclose(file);
        return -1;
    }
    *data = malloc((size_t)length);
    if (!*data || fread(*data, 1, (size_t)length, file) != (size_t)length) {
        fclose(file);
        free(*data);
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

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s IMAGE LOAD_ADDRESS IBOOT_COMMAND\n", argv[0]);
        return 2;
    }
    char *end = NULL;
    unsigned long address = strtoul(argv[2], &end, 0);
    if (!end || *end || address > UINT32_MAX) {
        fprintf(stderr, "Invalid load address: %s\n", argv[2]);
        return 2;
    }

    uint8_t *image = NULL;
    size_t image_size = 0;
    if (read_file(argv[1], &image, &image_size) != 0) {
        return 1;
    }

    idevice_t device = {0};
    if (open_recovery_device(&device) != 0) {
        fprintf(stderr, "Unable to open legacy recovery iBoot\n");
        free(image);
        return 1;
    }
    char response[16384];
    if (idevice_get_response(&device, response, sizeof(response)) == KERN_SUCCESS) {
        printf("%s", response);
    }
    printf("Sending %s (%zu bytes) to 0x%08lx\n", argv[1], image_size, address);
    if (idevice_send_file(&device, image, image_size, (uint32_t)address) != KERN_SUCCESS) {
        fprintf(stderr, "Image transfer failed\n");
        idevice_close(&device);
        free(image);
        return 1;
    }

    char command[256];
    snprintf(command, sizeof(command), "setenv filesize 0x%zx\n", image_size);
    if (idevice_send_command(&device, command) != KERN_SUCCESS) {
        fprintf(stderr, "Unable to set image size\n");
        idevice_close(&device);
        free(image);
        return 1;
    }
    if (idevice_get_response(&device, response, sizeof(response)) == KERN_SUCCESS) {
        printf("%s", response);
    }
    snprintf(command, sizeof(command), "%s\n", argv[3]);
    printf("iBoot: %s", command);
    if (idevice_send_command(&device, command) != KERN_SUCCESS) {
        fprintf(stderr, "iBoot command failed\n");
        idevice_close(&device);
        free(image);
        return 1;
    }
    if (idevice_get_response(&device, response, sizeof(response)) == KERN_SUCCESS) {
        printf("%s", response);
    }

    idevice_close(&device);
    free(image);
    return 0;
}
