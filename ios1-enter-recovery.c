#include <stdio.h>
#include <string.h>

#include "jailbreak-ref/src/common/lockdownd.h"

int main(void) {
    lockdownd_client_t client;
    memset(&client, 0, sizeof(client));

    if (!lockdownd_client_open(&client)) {
        fprintf(stderr, "Failed to connect to iOS 1 lockdownd over raw USB mux\n");
        return 1;
    }

    char product_type[64] = {0};
    char product_version[32] = {0};
    lockdownd_get_value_string(&client.session, "ProductType", product_type, sizeof(product_type));
    lockdownd_get_value_string(&client.session, "ProductVersion", product_version, sizeof(product_version));
    printf("Connected: %s, iOS %s\n", product_type[0] ? product_type : "unknown device",
        product_version[0] ? product_version : "unknown");

    char session_id[64] = {0};
    char error[128] = {0};
    if (lockdownd_client_start_paired_session(&client, session_id, sizeof(session_id), error, sizeof(error)) != 0) {
        fprintf(stderr, "Failed to start lockdownd session: %s\n", error[0] ? error : "unknown error");
        lockdownd_client_cleanup(&client);
        return 1;
    }

    CFDictionaryRef response = NULL;
    if (!lockdownd_send_enter_recovery(&client.session, session_id, &response)) {
        fprintf(stderr, "EnterRecovery failed\n");
        lockdownd_client_cleanup(&client);
        return 1;
    }

    if (response) CFRelease(response);
    lockdownd_client_cleanup(&client);
    puts("EnterRecovery accepted");
    return 0;
}
