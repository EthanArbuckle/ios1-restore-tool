#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define BASEBAND_DEVICE "/dev/tty.baseband"
#define SECPACK_SIZE 0x800
#define ERASE_START 0xA0020000U
#define ERASE_END 0xA0030000U
#define RESPONSE_SIZE 0x10000
#define ERASE_POLLS 120
#define PROCESS_TIMEOUT 180
#define REBOOT_NORMAL 0

typedef uint32_t io_object_t;
typedef io_object_t io_service_t;
typedef io_object_t io_connect_t;
typedef void *cf_dictionary_t;

typedef kern_return_t (*io_master_port_fn)(mach_port_t, mach_port_t *);
typedef cf_dictionary_t (*io_service_matching_fn)(const char *);
typedef io_service_t (*io_service_get_matching_service_fn)(mach_port_t, cf_dictionary_t);
typedef kern_return_t (*io_service_open_fn)(io_service_t, task_port_t, uint32_t, io_connect_t *);
typedef kern_return_t (*io_connect_call_scalar_method_fn)(io_connect_t, uint32_t, const uint64_t *, uint32_t, uint64_t *, uint32_t *);
typedef kern_return_t (*io_service_close_fn)(io_connect_t);

struct iokit_api {
    void *handle;
    io_master_port_fn master_port;
    io_service_matching_fn service_matching;
    io_service_get_matching_service_fn get_matching_service;
    io_service_open_fn service_open;
    io_connect_call_scalar_method_fn call_scalar_method;
    io_service_close_fn service_close;
};

static uint8_t response[RESPONSE_SIZE];
static volatile sig_atomic_t automatic_run;

static void process_timeout(int signal_number)
{
    (void)signal_number;
    if (automatic_run) {
        reboot(REBOOT_NORMAL);
    }

    _exit(124);
}

static void put16le(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32le(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void dump_bytes(const uint8_t *data, size_t size)
{
    size_t shown = size > 64 ? 64 : size;
    size_t i;

    for (i = 0; i < shown; i++) {
        if ((i % 16) == 0) {
            printf("  ");
        }

        printf("%02x%c", data[i], (i % 16) == 15 || i + 1 == shown ? '\n' : ' ');
    }

    if (shown < size) {
        printf("  ... (%lu bytes total)\n", (unsigned long)size);
    }
}

static int write_all(int fd, const uint8_t *data, size_t size)
{
    while (size != 0) {
        ssize_t written = write(fd, data, size);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }

            fprintf(stderr, "write: %s\n", strerror(errno));
            return -1;
        }

        if (written == 0) {
            fprintf(stderr, "write returned zero\n");
            return -1;
        }

        data += written;
        size -= (size_t)written;
    }

    return 0;
}

static ssize_t read_response(int fd)
{
    struct timeval deadline;
    size_t used = 0;

    memset(response, 0, sizeof(response));
    gettimeofday(&deadline, NULL);
    deadline.tv_sec += 1;
    for (;;) {
        fd_set read_set;
        struct timeval now;
        struct timeval timeout;
        int selected;

        gettimeofday(&now, NULL);
        timeout.tv_sec = deadline.tv_sec - now.tv_sec;
        timeout.tv_usec = deadline.tv_usec - now.tv_usec;
        if (timeout.tv_usec < 0) {
            timeout.tv_sec--;
            timeout.tv_usec += 1000000;
        }

        if (timeout.tv_sec < 0 ||
            (timeout.tv_sec == 0 && timeout.tv_usec == 0)) {
            break;
        }

        FD_ZERO(&read_set);
        FD_SET(fd, &read_set);
        selected = select(fd + 1, &read_set, NULL, NULL, &timeout);
        if (selected < 0) {
            if (errno == EINTR) {
                continue;
            }

            fprintf(stderr, "select: %s\n", strerror(errno));
            return -1;
        }

        if (selected == 0) {
            break;
        }

        if (used == sizeof(response)) {
            fprintf(stderr, "baseband response is too large\n");
            return -1;
        }

        ssize_t count = read(fd, response + used, sizeof(response) - used);
        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }

            fprintf(stderr, "read: %s\n", strerror(errno));
            return -1;
        }

        if (count == 0) {
            continue;
        }

        used += (size_t)count;
    }

    return (ssize_t)used;
}

static int send_command(int fd, uint16_t command, const uint8_t *data, uint16_t data_size, const char *name, ssize_t *response_size)
{
    uint8_t header[6];
    uint8_t trailer[4];
    uint32_t checksum = command + data_size;
    uint16_t i;

    for (i = 0; i < data_size; i++) {
        checksum += data[i];
    }

    put16le(header, 2);
    put16le(header + 2, command);
    put16le(header + 4, data_size);
    put16le(trailer, (uint16_t)checksum);
    put16le(trailer + 2, 3);

    printf("%s (0x%04x, %u bytes)\n", name, command, data_size);
    if (write_all(fd, header, sizeof(header)) < 0 ||
        (data_size != 0 && write_all(fd, data, data_size) < 0) ||
        write_all(fd, trailer, sizeof(trailer)) < 0) {
        return -1;
    }

    *response_size = read_response(fd);
    if (*response_size < 0) {
        fprintf(stderr, "%s: unable to read baseband response\n", name);
        return -1;
    }

    if (*response_size == 0) {
        fprintf(stderr, "%s: no response from baseband\n", name);
        return 0;
    }

    dump_bytes(response, (size_t)*response_size);
    return 0;
}

static int load_iokit(struct iokit_api *api)
{
#define LOAD(member, symbol)                                                   \
    do {                                                                       \
        *(void **)(&api->member) = dlsym(api->handle, symbol);                 \
        if (api->member == NULL) {                                             \
            fprintf(stderr, "Missing IOKit symbol %s\n", symbol);            \
            return -1;                                                         \
        }                                                                      \
    } while (0)

    memset(api, 0, sizeof(*api));
    api->handle = dlopen(
        "/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit",
        RTLD_LAZY | RTLD_LOCAL);
    if (api->handle == NULL) {
        api->handle = dlopen("/System/Library/Frameworks/IOKit.framework/IOKit",
                             RTLD_LAZY | RTLD_LOCAL);
    }

    if (api->handle == NULL) {
        fprintf(stderr, "Unable to load IOKit: %s\n", dlerror());
        return -1;
    }

    LOAD(master_port, "IOMasterPort");
    LOAD(service_matching, "IOServiceMatching");
    LOAD(get_matching_service, "IOServiceGetMatchingService");
    LOAD(service_open, "IOServiceOpen");
    LOAD(call_scalar_method, "IOConnectCallScalarMethod");
    LOAD(service_close, "IOServiceClose");
    return 0;
#undef LOAD
}

static int restart_baseband(void)
{
    struct iokit_api api;
    mach_port_t master = MACH_PORT_NULL;
    cf_dictionary_t matching;
    io_service_t service;
    io_connect_t connection = 0;
    kern_return_t result;

    if (load_iokit(&api) < 0) {
        return -1;
    }

    result = api.master_port(MACH_PORT_NULL, &master);
    if (result != KERN_SUCCESS) {
        fprintf(stderr, "IOMasterPort failed: 0x%x\n", result);
        return -1;
    }

    matching = api.service_matching("AppleBaseband");
    if (matching == NULL) {
        fprintf(stderr, "AppleBaseband matching failed\n");
        return -1;
    }

    service = api.get_matching_service(MACH_PORT_NULL, matching);
    if (service == 0) {
        fprintf(stderr, "AppleBaseband service not found\n");
        return -1;
    }

    result = api.service_open(service, mach_task_self(), 0, &connection);
    if (result != KERN_SUCCESS) {
        fprintf(stderr, "IOServiceOpen failed: 0x%x\n", result);
        return -1;
    }

    result = api.call_scalar_method(connection, 0, NULL, 0, NULL, NULL);
    api.service_close(connection);
    dlclose(api.handle);
    if (result != KERN_SUCCESS) {
        fprintf(stderr, "Baseband restart failed: 0x%x\n", result);
        return -1;
    }

    printf("Baseband restarted\n");
    return 0;
}

static int open_baseband(void)
{
    struct termios options;
    unsigned long zero = 0;
    unsigned long value = 0x126;
    int fd = open(BASEBAND_DEVICE, O_RDWR | O_NOCTTY | 0x20000);

    if (fd < 0) {
        fprintf(stderr, "Unable to open %s: %s\n", BASEBAND_DEVICE,
                strerror(errno));
        return -1;
    }

    ioctl(fd, 0x2000740D);
    fcntl(fd, F_SETFL, 0);
    if (tcgetattr(fd, &options) < 0) {
        fprintf(stderr, "tcgetattr: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    ioctl(fd, 0x8004540A, &zero);
    cfsetspeed(&options, B115200);
    cfmakeraw(&options);
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 5;
    options.c_iflag = (options.c_iflag | 5) & 0xFFFFF0CD;
    options.c_oflag &= 0xFFFFFFFE;
    options.c_cflag = (options.c_cflag | 0x3CB00) & 0xFFFFEFFF;
    options.c_lflag &= 0xFFFFFA77;
    options.c_cflag |= CS8;
    options.c_cflag &= ~PARENB;
    options.c_lflag &= ~(ECHO | ICANON);
    if (tcsetattr(fd, TCSANOW, &options) < 0) {
        fprintf(stderr, "tcsetattr: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    ioctl(fd, 0x20007479);
    ioctl(fd, 0x20007478);
    ioctl(fd, 0x8004746D, &value);
    return fd;
}

static int enter_command_mode(int fd)
{
    static const uint8_t prompt[] = {0x60, 0x0D};
    int attempt;

    for (attempt = 1; attempt <= 30; attempt++) {
        ssize_t size;
        if (write_all(fd, prompt, sizeof(prompt)) < 0) {
            return -1;
        }

        size = read_response(fd);
        if (size < 0) {
            return -1;
        }

        if (size != 0) {
            printf("Command-mode response %d:\n", attempt);
            dump_bytes(response, (size_t)size);
            if (response[0] == 0x0B) {
                return 0;
            }
        }
    }

    fprintf(stderr, "Baseband did not enter command mode\n");
    return -1;
}

static int read_secpack(const char *path, uint8_t secpack[SECPACK_SIZE])
{
    FILE *file;
    size_t count;
    int extra;

    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return -1;
    }

    count = fread(secpack, 1, SECPACK_SIZE, file);
    if (count != SECPACK_SIZE) {
        fclose(file);
        fprintf(stderr, "Unable to read %s\n", path);
        return -1;
    }

    extra = fgetc(file);
    if (extra != EOF || ferror(file)) {
        fclose(file);
        fprintf(stderr, "%s must be exactly %u bytes\n", path, SECPACK_SIZE);
        return -1;
    }

    fclose(file);
    return 0;
}

static int erase_firmware_sector(int fd, const uint8_t secpack[SECPACK_SIZE])
{
    uint8_t data[8];
    ssize_t size;
    int poll;

    if (send_command(fd, 0x0801, NULL, 0, "Read flash ID", &size) < 0) {
        return -1;
    }

    memset(data, 0, 2);
    if (send_command(fd, 0x0084, data, 2, "Enter CFI stage 1", &size) < 0 ||
        send_command(fd, 0x0085, NULL, 0, "Enter CFI stage 2", &size) < 0 ||
        send_command(fd, 0x0204, secpack, SECPACK_SIZE, "Authenticate secpack",
                     &size) < 0) {
        return -1;
    }

    put32le(data, ERASE_START);
    if (send_command(fd, 0x0802, data, 4, "Set firmware address", &size) < 0) {
        return -1;
    }

    put16le(data, 0x20);
    if (send_command(fd, 0x0803, data, 2, "Read firmware header", &size) < 0) {
        return -1;
    }

    put32le(data, ERASE_START);
    put32le(data + 4, ERASE_END);
    printf("Erasing firmware sector 0x%08x-0x%08x\n", ERASE_START, ERASE_END);
    if (send_command(fd, 0x0805, data, 8, "Start erase", &size) < 0) {
        return -1;
    }

    memset(data, 0, 2);
    for (poll = 1; poll <= ERASE_POLLS; poll++) {
        if (send_command(fd, 0x0806, data, 2, "Poll erase status", &size) < 0) {
            return -1;
        }

        if (size >= 7 && response[6] == 1) {
            printf("Firmware sector erase complete\n");
            return 0;
        }
    }

    fprintf(stderr, "Timed out waiting for firmware sector erase\n");
    return -1;
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s --yes SECPACK\n", program);
}

static int set_nvram_assignment(const char *assignment)
{
    char *arguments[3];
    pid_t child;
    int status;

    arguments[0] = "/usr/sbin/nvram";
    arguments[1] = (char *)assignment;
    arguments[2] = NULL;

    child = fork();
    if (child < 0) {
        fprintf(stderr, "fork: %s\n", strerror(errno));
        return -1;
    }

    if (child == 0) {
        execv(arguments[0], arguments);
        _exit(127);
    }

    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            fprintf(stderr, "waitpid: %s\n", strerror(errno));
            return -1;
        }
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "Unable to update NVRAM\n");
        return -1;
    }

    return 0;
}

static void finish_automatic_run(int succeeded)
{
    (void)set_nvram_assignment("boot-args=");
    (void)set_nvram_assignment(succeeded ? "auto-boot=false" : "auto-boot=true");
    (void)set_nvram_assignment(succeeded
        ? "ios1-baseband-erase-result=success"
        : "ios1-baseband-erase-result=failed");
    sync();
    sleep(1);
    reboot(REBOOT_NORMAL);
    for (;;) {
        pause();
    }
}

int main(int argc, char **argv)
{
    uint8_t secpack[SECPACK_SIZE];
    const char *secpack_path;
    ssize_t size;
    int automatic = 0;
    int fd = -1;
    int result = 1;

    if (argc == 2 && strcmp(argv[1], "--automatic") == 0) {
        automatic = 1;
        automatic_run = 1;
        secpack_path = "/usr/local/share/baseband-secpack.bin";
    } else if (argc == 3 && strcmp(argv[1], "--yes") == 0) {
        secpack_path = argv[2];
    } else {
        usage(argv[0]);
        return 2;
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    signal(SIGALRM, process_timeout);
    alarm(PROCESS_TIMEOUT);

    if (read_secpack(secpack_path, secpack) < 0) {
        goto finish;
    }

    printf("This will erase the baseband firmware header sector.\n");
    if (restart_baseband() < 0) {
        fprintf(stderr, "Continuing after baseband restart error\n");
    }

    fd = open_baseband();
    if (fd < 0) {
        goto finish;
    }

    /* The first query wakes the flash interface and may not return data. */
    (void)send_command(fd, 0x0801, NULL, 0, "Initial flash query", &size);
    if (enter_command_mode(fd) == 0 && erase_firmware_sector(fd, secpack) == 0) {
        result = 0;
    }

finish:
    if (fd >= 0) {
        close(fd);
    }

    alarm(0);
    memset(secpack, 0, sizeof(secpack));
    if (automatic) {
        finish_automatic_run(result == 0);
    }

    return result;
}
