#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "jailbreak-ref/src/idevice.h"

static int open_recovery_device(idevice_t *device) {
    for (int attempt = 0; attempt < 40; attempt++) {
        if (idevice_open(device) == KERN_SUCCESS &&
            idevice_init_handshake(device) == KERN_SUCCESS) {
            return 0;
        }
        idevice_close(device);
        memset(device, 0, sizeof(*device));
        usleep(250000);
    }
    return -1;
}

static int send_command(idevice_t *device, const char *command) {
    if (idevice_send_command(device, command) != KERN_SUCCESS) {
        return -1;
    }

    char response[4096];
    return idevice_get_response(device, response, sizeof(response)) == KERN_SUCCESS ? 0 : -1;
}

int main(void) {
    idevice_t device = {0};
    if (open_recovery_device(&device) != 0) {
        fprintf(stderr, "Unable to open the S5L8900 recovery device\n");
        return 1;
    }

    char response[4096];
    (void)idevice_get_response(&device, response, sizeof(response));

    if (send_command(&device, "setenv auto-boot true\n") != 0 ||
        send_command(&device, "saveenv\n") != 0 ||
        idevice_send_command(&device, "reboot\n") != KERN_SUCCESS) {
        fprintf(stderr, "Unable to leave recovery mode\n");
        idevice_close(&device);
        return 1;
    }

    usleep(2000000);
    idevice_close(&device);
    return 0;
}
