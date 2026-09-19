#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define PROCESS_TIMEOUT 300
#define REBOOT_NORMAL 0

static void process_timeout(int signal_number)
{
    (void)signal_number;
    reboot(REBOOT_NORMAL);
    _exit(124);
}

static int run_process(char *const arguments[])
{
    pid_t child;
    int status;

    child = fork();
    if (child < 0) {
        fprintf(stderr, "fork: %s\n", strerror(errno));
        return -1;
    }

    if (child == 0) {
        execv(arguments[0], arguments);
        fprintf(stderr, "execv %s: %s\n", arguments[0], strerror(errno));
        _exit(127);
    }

    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            fprintf(stderr, "waitpid: %s\n", strerror(errno));
            return -1;
        }
    }

    if (!WIFEXITED(status)) {
        return -1;
    }

    return WEXITSTATUS(status);
}

static int set_nvram_assignment(const char *assignment)
{
    char *arguments[3];

    arguments[0] = "/usr/sbin/nvram";
    arguments[1] = (char *)assignment;
    arguments[2] = NULL;
    return run_process(arguments);
}

static void finish_run(int succeeded)
{
    (void)set_nvram_assignment("boot-args=");
    (void)set_nvram_assignment("auto-boot=false");
    (void)set_nvram_assignment(succeeded
        ? "ios1-baseband-update-result=success"
        : "ios1-baseband-update-result=failed");
    sync();
    sleep(4);
    reboot(REBOOT_NORMAL);

    for (;;) {
        pause();
    }
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s BASEBAND.fls BASEBAND.eep\n", program);
}

int main(int argc, char **argv)
{
    char *arguments[12];
    int result;

    if (argc != 3) {
        usage(argv[0]);
        return 2;
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    signal(SIGALRM, process_timeout);
    alarm(PROCESS_TIMEOUT);

    (void)set_nvram_assignment("ios1-baseband-update-result=pending");
    arguments[0] = "/usr/local/bin/bbupdater";
    arguments[1] = "-D";
    arguments[2] = "/dev/tty.baseband";
    arguments[3] = "-B";
    arguments[4] = "750000";
    arguments[5] = "-f";
    arguments[6] = argv[1];
    arguments[7] = "-e";
    arguments[8] = argv[2];
    arguments[9] = NULL;
    result = run_process(arguments);

    alarm(0);
    finish_run(result == 0);
    return result;
}
