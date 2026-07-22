#include "kilix_state.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef NAME_MAX
#define NAME_MAX 255
#endif

#define RECORD_HEADER_SIZE 16u

static const unsigned char record_magic[4] = {'K', 'S', 'T', '1'};

static bool valid_component(const char *component, size_t capacity)
{
    size_t length;

    if (component == NULL || component[0] == '\0') return false;
    length = strlen(component);
    if (length >= capacity || length > NAME_MAX ||
        ((component[0] < 'A' || component[0] > 'Z') &&
         (component[0] < 'a' || component[0] > 'z') &&
         (component[0] < '0' || component[0] > '9'))) return false;
    for (size_t index = 1u; index < length; ++index) {
        const unsigned char byte = (unsigned char)component[index];
        if ((byte < (unsigned char)'A' || byte > (unsigned char)'Z') &&
            (byte < (unsigned char)'a' || byte > (unsigned char)'z') &&
            (byte < (unsigned char)'0' || byte > (unsigned char)'9') &&
            byte != (unsigned char)'-' && byte != (unsigned char)'_' &&
            byte != (unsigned char)'.') return false;
    }
    return true;
}

static int open_directory_path(const char *path)
{
    char component[NAME_MAX + 1u];
    const char *cursor;
    int directory_fd;

    if (path == NULL || path[0] != '/') {
        errno = EINVAL;
        return -1;
    }
    directory_fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd < 0) return -1;
    cursor = path + 1;
    while (*cursor != '\0') {
        const char *end;
        size_t length;
        int next_fd;

        while (*cursor == '/') ++cursor;
        if (*cursor == '\0') break;
        end = strchr(cursor, '/');
        length = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
        if (length == 0u || length > NAME_MAX ||
            (length == 1u && cursor[0] == '.') ||
            (length == 2u && cursor[0] == '.' && cursor[1] == '.')) {
            (void)close(directory_fd);
            errno = EINVAL;
            return -1;
        }
        (void)memcpy(component, cursor, length);
        component[length] = '\0';
        next_fd = openat(directory_fd, component,
                         O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (next_fd < 0 && errno == ENOENT) {
            if (mkdirat(directory_fd, component, 0700) != 0 &&
                errno != EEXIST) {
                const int failure = errno;
                (void)close(directory_fd);
                errno = failure;
                return -1;
            }
            next_fd = openat(directory_fd, component,
                             O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        }
        if (next_fd < 0) {
            const int failure = errno;
            (void)close(directory_fd);
            errno = failure;
            return -1;
        }
        (void)close(directory_fd);
        directory_fd = next_fd;
        cursor = end != NULL ? end + 1 : cursor + length;
    }
    return directory_fd;
}

static kilixstate_result result_from_errno(int failure)
{
    if (failure == ELOOP || failure == ENOTDIR) return KILIXSTATE_SECURITY;
    if (failure == ENAMETOOLONG || failure == EINVAL)
        return KILIXSTATE_INVALID;
    return KILIXSTATE_IO_ERROR;
}

void kilixstate_options_init(kilixstate_options *options)
{
    if (options == NULL) return;
    options->app_id = NULL;
    options->filename = NULL;
    options->max_payload = KILIXSTATE_DEFAULT_MAX_PAYLOAD;
    options->format = KILIXSTATE_FORMAT_CRC32;
}

kilixstate_result kilixstate_store_init(kilixstate_store *store,
                                        const kilixstate_options *options)
{
    const char *xdg_data_home;
    const char *home;
    char base[KILIXSTATE_PATH_CAPACITY];
    char directory[KILIXSTATE_PATH_CAPACITY];
    struct stat status;
    int length;
    int directory_fd;

    if (store == NULL) return KILIXSTATE_INVALID;
    (void)memset(store, 0, sizeof *store);
    store->directory_fd = -1;
    if (options == NULL ||
        !valid_component(options->app_id, KILIXSTATE_FILENAME_CAPACITY) ||
        !valid_component(options->filename, KILIXSTATE_FILENAME_CAPACITY) ||
        options->max_payload == 0u ||
        options->max_payload > KILIXSTATE_MAX_PAYLOAD ||
        (options->format != KILIXSTATE_FORMAT_CRC32 &&
         options->format != KILIXSTATE_FORMAT_RAW)) return KILIXSTATE_INVALID;

    xdg_data_home = getenv("XDG_DATA_HOME");
    if (xdg_data_home != NULL && xdg_data_home[0] != '\0') {
        if (xdg_data_home[0] != '/') return KILIXSTATE_INVALID;
        length = snprintf(base, sizeof base, "%s", xdg_data_home);
    } else {
        home = getenv("HOME");
        if (home == NULL || home[0] != '/') return KILIXSTATE_INVALID;
        length = snprintf(base, sizeof base, "%s/.local/share", home);
    }
    if (length < 0 || (size_t)length >= sizeof base)
        return KILIXSTATE_INVALID;
    while (length > 1 && base[(size_t)length - 1u] == '/')
        base[--length] = '\0';
    length = snprintf(directory, sizeof directory, "%s/%s", base,
                      options->app_id);
    if (length < 0 || (size_t)length >= sizeof directory)
        return KILIXSTATE_INVALID;

    directory_fd = open_directory_path(directory);
    if (directory_fd < 0) return result_from_errno(errno);
    if (fstat(directory_fd, &status) != 0) {
        const int failure = errno;
        (void)close(directory_fd);
        return result_from_errno(failure);
    }
    if (!S_ISDIR(status.st_mode) || status.st_uid != geteuid()) {
        (void)close(directory_fd);
        return KILIXSTATE_SECURITY;
    }
    if (fchmod(directory_fd, 0700) != 0) {
        const int failure = errno;
        (void)close(directory_fd);
        return result_from_errno(failure);
    }
    (void)memcpy(store->directory_path, directory, (size_t)length + 1u);
    (void)snprintf(store->filename, sizeof store->filename, "%s",
                   options->filename);
    store->max_payload = options->max_payload;
    store->directory_fd = directory_fd;
    store->format = (uint8_t)options->format;
    store->initialized = true;
    return KILIXSTATE_OK;
}

void kilixstate_store_close(kilixstate_store *store)
{
    if (store == NULL) return;
    if (store->initialized && store->directory_fd >= 0)
        (void)close(store->directory_fd);
    store->directory_fd = -1;
    store->initialized = false;
}

uint32_t kilixstate_crc32(const void *bytes, size_t byte_count)
{
    const unsigned char *source = bytes;
    uint32_t crc = UINT32_MAX;

    if (bytes == NULL && byte_count != 0u) return 0u;
    for (size_t index = 0u; index < byte_count; ++index) {
        crc ^= source[index];
        for (unsigned int bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u &
                  (uint32_t)-(int32_t)(crc & 1u));
    }
    return crc ^ UINT32_MAX;
}

static void put_u32(unsigned char *bytes, uint32_t value)
{
    bytes[0] = (unsigned char)(value & 0xffu);
    bytes[1] = (unsigned char)((value >> 8) & 0xffu);
    bytes[2] = (unsigned char)((value >> 16) & 0xffu);
    bytes[3] = (unsigned char)((value >> 24) & 0xffu);
}

static uint32_t get_u32(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static bool write_all(int fd, const void *bytes, size_t byte_count)
{
    const unsigned char *source = bytes;
    size_t offset = 0u;

    while (offset < byte_count) {
        const ssize_t count = write(fd, source + offset, byte_count - offset);
        if (count > 0) offset += (size_t)count;
        else if (count < 0 && errno == EINTR) continue;
        else return false;
    }
    return true;
}

static bool read_all(int fd, void *bytes, size_t byte_count)
{
    unsigned char *destination = bytes;
    size_t offset = 0u;

    while (offset < byte_count) {
        const ssize_t count = read(fd, destination + offset,
                                   byte_count - offset);
        if (count > 0) offset += (size_t)count;
        else if (count < 0 && errno == EINTR) continue;
        else return false;
    }
    return true;
}

static uint64_t temporary_nonce(void)
{
    static uint64_t counter = 0u;
    uint64_t value = 0u;
    int random_fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);

    if (random_fd >= 0) {
        ssize_t count;
        do {
            count = read(random_fd, &value, sizeof value);
        } while (count < 0 && errno == EINTR);
        (void)close(random_fd);
        if (count == (ssize_t)sizeof value && value != 0u) return value;
    }
    {
        struct timespec now = {0, 0};
        (void)clock_gettime(CLOCK_MONOTONIC, &now);
        value = (uint64_t)now.tv_sec ^ ((uint64_t)now.tv_nsec << 21) ^
                ((uint64_t)(unsigned long)getpid() << 32) ^
                __sync_add_and_fetch(&counter, 1u);
    }
    return value;
}

static int open_temporary(kilixstate_store *store, char *name,
                          size_t name_size)
{
    for (unsigned int attempt = 0u; attempt < 128u; ++attempt) {
        const uint64_t nonce = temporary_nonce() ^ (uint64_t)attempt;
        const int length = snprintf(name, name_size,
                                    ".kilixstate.%016llx.tmp",
                                    (unsigned long long)nonce);
        int fd;

        if (length < 0 || (size_t)length >= name_size) {
            errno = ENAMETOOLONG;
            return -1;
        }
        fd = openat(store->directory_fd, name,
                    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                    0600);
        if (fd >= 0) return fd;
        if (errno != EEXIST) return -1;
    }
    errno = EEXIST;
    return -1;
}

kilixstate_result kilixstate_save(kilixstate_store *store,
                                  const void *payload, size_t payload_size)
{
    unsigned char header[RECORD_HEADER_SIZE];
    char temporary[KILIXSTATE_FILENAME_CAPACITY + 32u];
    int temporary_fd;
    int failure = 0;

    if (store == NULL || !store->initialized || store->directory_fd < 0)
        return KILIXSTATE_NOT_INITIALIZED;
    if (payload == NULL && payload_size != 0u) return KILIXSTATE_INVALID;
    if (payload_size > store->max_payload || payload_size > UINT32_MAX)
        return KILIXSTATE_TOO_LARGE;
    temporary_fd = open_temporary(store, temporary, sizeof temporary);
    if (temporary_fd < 0) return result_from_errno(errno);

    if (fchmod(temporary_fd, 0600) != 0) failure = errno;
    if (failure == 0 && store->format == KILIXSTATE_FORMAT_CRC32) {
        (void)memcpy(header, record_magic, sizeof record_magic);
        put_u32(header + 4u, 1u);
        put_u32(header + 8u, (uint32_t)payload_size);
        put_u32(header + 12u, kilixstate_crc32(payload, payload_size));
        if (!write_all(temporary_fd, header, sizeof header)) failure = errno;
    }
    if (failure == 0 && !write_all(temporary_fd, payload, payload_size))
        failure = errno;
    if (failure == 0 && fsync(temporary_fd) != 0) failure = errno;
    if (close(temporary_fd) != 0 && failure == 0) failure = errno;
    if (failure == 0 &&
        renameat(store->directory_fd, temporary, store->directory_fd,
                 store->filename) != 0) failure = errno;
    if (failure == 0 && fsync(store->directory_fd) != 0) failure = errno;
    if (failure != 0) {
        (void)unlinkat(store->directory_fd, temporary, 0);
        return result_from_errno(failure);
    }
    return KILIXSTATE_OK;
}

kilixstate_result kilixstate_load(kilixstate_store *store, void *payload,
                                  size_t payload_capacity,
                                  size_t *payload_size)
{
    unsigned char header[RECORD_HEADER_SIZE];
    struct stat status;
    size_t required;
    int fd;

    if (payload_size != NULL) *payload_size = 0u;
    if (store == NULL || !store->initialized || store->directory_fd < 0)
        return KILIXSTATE_NOT_INITIALIZED;
    if (payload_size == NULL || (payload == NULL && payload_capacity != 0u))
        return KILIXSTATE_INVALID;
    fd = openat(store->directory_fd, store->filename,
                O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        if (errno == ENOENT) return KILIXSTATE_NOT_FOUND;
        return result_from_errno(errno);
    }
    if (fstat(fd, &status) != 0) {
        const int failure = errno;
        (void)close(fd);
        return result_from_errno(failure);
    }
    if (!S_ISREG(status.st_mode) || status.st_uid != geteuid() ||
        status.st_size < 0) {
        (void)close(fd);
        return KILIXSTATE_SECURITY;
    }
    if ((uintmax_t)status.st_size >
        (uintmax_t)store->max_payload + RECORD_HEADER_SIZE) {
        (void)close(fd);
        return KILIXSTATE_TOO_LARGE;
    }

    if (store->format == KILIXSTATE_FORMAT_CRC32) {
        if (status.st_size < (off_t)RECORD_HEADER_SIZE ||
            !read_all(fd, header, sizeof header) ||
            memcmp(header, record_magic, sizeof record_magic) != 0 ||
            get_u32(header + 4u) != 1u) {
            (void)close(fd);
            return KILIXSTATE_CORRUPT;
        }
        required = (size_t)get_u32(header + 8u);
        if (required > store->max_payload ||
            (uintmax_t)status.st_size !=
                (uintmax_t)required + RECORD_HEADER_SIZE) {
            (void)close(fd);
            return KILIXSTATE_CORRUPT;
        }
    } else {
        required = (size_t)status.st_size;
        if (required > store->max_payload) {
            (void)close(fd);
            return KILIXSTATE_TOO_LARGE;
        }
    }
    if (required > payload_capacity) {
        *payload_size = required;
        (void)close(fd);
        return KILIXSTATE_BUFFER_TOO_SMALL;
    }
    if (!read_all(fd, payload, required)) {
        (void)close(fd);
        return KILIXSTATE_CORRUPT;
    }
    {
        unsigned char extra;
        ssize_t extra_count;

        do {
            extra_count = read(fd, &extra, 1u);
        } while (extra_count < 0 && errno == EINTR);
        if (extra_count != 0) {
            (void)close(fd);
            return extra_count > 0 ? KILIXSTATE_CORRUPT :
                                     KILIXSTATE_IO_ERROR;
        }
    }
    if (close(fd) != 0) return KILIXSTATE_IO_ERROR;
    if (store->format == KILIXSTATE_FORMAT_CRC32 &&
        kilixstate_crc32(payload, required) != get_u32(header + 12u))
        return KILIXSTATE_CORRUPT;
    *payload_size = required;
    return KILIXSTATE_OK;
}

kilixstate_result kilixstate_store_path(const kilixstate_store *store,
                                        char *path, size_t path_size)
{
    int length;

    if (store == NULL || !store->initialized)
        return KILIXSTATE_NOT_INITIALIZED;
    if (path == NULL || path_size == 0u) return KILIXSTATE_INVALID;
    length = snprintf(path, path_size, "%s/%s", store->directory_path,
                      store->filename);
    if (length < 0 || (size_t)length >= path_size)
        return KILIXSTATE_BUFFER_TOO_SMALL;
    return KILIXSTATE_OK;
}

const char *kilixstate_result_name(kilixstate_result result)
{
    switch (result) {
    case KILIXSTATE_OK: return "ok";
    case KILIXSTATE_NOT_FOUND: return "not found";
    case KILIXSTATE_INVALID: return "invalid argument";
    case KILIXSTATE_TOO_LARGE: return "state too large";
    case KILIXSTATE_BUFFER_TOO_SMALL: return "buffer too small";
    case KILIXSTATE_CORRUPT: return "corrupt state";
    case KILIXSTATE_SECURITY: return "unsafe state path";
    case KILIXSTATE_IO_ERROR: return "I/O error";
    case KILIXSTATE_NOT_INITIALIZED: return "store not initialized";
    }
    return "unknown state result";
}
