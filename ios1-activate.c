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

    char error[128] = {0};
    if (lockdownd_set_value_bool(&client.session, "iTunesHasConnected", 1, error, sizeof(error))) {
        lockdownd_client_cleanup(&client);
        puts("Successfully set iTunesHasConnected=true");
        return 0;
    }

    char session_id[64] = {0};
    if (lockdownd_client_start_paired_session(&client, session_id, sizeof(session_id), error, sizeof(error)) != 0) {
        fprintf(stderr, "Unable to establish paired session: %s\n", error[0] ? error : "unknown error");
        lockdownd_client_cleanup(&client);
        return 1;
    }

    if (!lockdownd_set_value_bool(&client.session, "iTunesHasConnected", 1, error, sizeof(error))) {
        fprintf(stderr, "Unable to set iTunesHasConnected: %s\n", error[0] ? error : "no response");
        lockdownd_client_cleanup(&client);
        return 1;
    }

    lockdownd_client_cleanup(&client);
    puts("Successfully set iTunesHasConnected=true");
    return 0;
}
