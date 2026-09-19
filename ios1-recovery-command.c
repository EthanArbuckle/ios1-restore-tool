#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "jailbreak-ref/src/idevice.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s COMMAND\n", argv[0]);
        return 2;
    }

    idevice_t device = {0};
    for (int attempt = 0; attempt < 40; attempt++) {
        if (idevice_open(&device) == KERN_SUCCESS &&
            idevice_init_handshake(&device) == KERN_SUCCESS) {
            break;
        }
        idevice_close(&device);
        memset(&device, 0, sizeof(device));
        usleep(250000);
    }
    if (!device.handle) {
        fprintf(stderr, "Unable to open the S5L8900 recovery device\n");
        return 1;
    }

    char command[512];
    int length = snprintf(command, sizeof(command), "%s\n", argv[1]);
    if (length <= 1 || (size_t)length >= sizeof(command) ||
        idevice_send_command(&device, command) != KERN_SUCCESS) {
        fprintf(stderr, "Unable to send recovery command\n");
        idevice_close(&device);
        return 1;
    }

    char response[16384];
    kern_return_t result = idevice_get_response(&device, response, sizeof(response));
    idevice_close(&device);
    if (result != KERN_SUCCESS) {
        fprintf(stderr, "Unable to read recovery response\n");
        return 1;
    }
    fputs(response, stdout);
    return 0;
}
