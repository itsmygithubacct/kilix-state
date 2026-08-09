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

static const uint32_t crc32_table[256] = {
    UINT32_C(0x00000000), UINT32_C(0x77073096), UINT32_C(0xee0e612c), UINT32_C(0x990951ba),
    UINT32_C(0x076dc419), UINT32_C(0x706af48f), UINT32_C(0xe963a535), UINT32_C(0x9e6495a3),
    UINT32_C(0x0edb8832), UINT32_C(0x79dcb8a4), UINT32_C(0xe0d5e91e), UINT32_C(0x97d2d988),
    UINT32_C(0x09b64c2b), UINT32_C(0x7eb17cbd), UINT32_C(0xe7b82d07), UINT32_C(0x90bf1d91),
    UINT32_C(0x1db71064), UINT32_C(0x6ab020f2), UINT32_C(0xf3b97148), UINT32_C(0x84be41de),
    UINT32_C(0x1adad47d), UINT32_C(0x6ddde4eb), UINT32_C(0xf4d4b551), UINT32_C(0x83d385c7),
    UINT32_C(0x136c9856), UINT32_C(0x646ba8c0), UINT32_C(0xfd62f97a), UINT32_C(0x8a65c9ec),
    UINT32_C(0x14015c4f), UINT32_C(0x63066cd9), UINT32_C(0xfa0f3d63), UINT32_C(0x8d080df5),
    UINT32_C(0x3b6e20c8), UINT32_C(0x4c69105e), UINT32_C(0xd56041e4), UINT32_C(0xa2677172),
    UINT32_C(0x3c03e4d1), UINT32_C(0x4b04d447), UINT32_C(0xd20d85fd), UINT32_C(0xa50ab56b),
    UINT32_C(0x35b5a8fa), UINT32_C(0x42b2986c), UINT32_C(0xdbbbc9d6), UINT32_C(0xacbcf940),
    UINT32_C(0x32d86ce3), UINT32_C(0x45df5c75), UINT32_C(0xdcd60dcf), UINT32_C(0xabd13d59),
    UINT32_C(0x26d930ac), UINT32_C(0x51de003a), UINT32_C(0xc8d75180), UINT32_C(0xbfd06116),
    UINT32_C(0x21b4f4b5), UINT32_C(0x56b3c423), UINT32_C(0xcfba9599), UINT32_C(0xb8bda50f),
    UINT32_C(0x2802b89e), UINT32_C(0x5f058808), UINT32_C(0xc60cd9b2), UINT32_C(0xb10be924),
    UINT32_C(0x2f6f7c87), UINT32_C(0x58684c11), UINT32_C(0xc1611dab), UINT32_C(0xb6662d3d),
    UINT32_C(0x76dc4190), UINT32_C(0x01db7106), UINT32_C(0x98d220bc), UINT32_C(0xefd5102a),
    UINT32_C(0x71b18589), UINT32_C(0x06b6b51f), UINT32_C(0x9fbfe4a5), UINT32_C(0xe8b8d433),
    UINT32_C(0x7807c9a2), UINT32_C(0x0f00f934), UINT32_C(0x9609a88e), UINT32_C(0xe10e9818),
    UINT32_C(0x7f6a0dbb), UINT32_C(0x086d3d2d), UINT32_C(0x91646c97), UINT32_C(0xe6635c01),
    UINT32_C(0x6b6b51f4), UINT32_C(0x1c6c6162), UINT32_C(0x856530d8), UINT32_C(0xf262004e),
    UINT32_C(0x6c0695ed), UINT32_C(0x1b01a57b), UINT32_C(0x8208f4c1), UINT32_C(0xf50fc457),
    UINT32_C(0x65b0d9c6), UINT32_C(0x12b7e950), UINT32_C(0x8bbeb8ea), UINT32_C(0xfcb9887c),
    UINT32_C(0x62dd1ddf), UINT32_C(0x15da2d49), UINT32_C(0x8cd37cf3), UINT32_C(0xfbd44c65),
    UINT32_C(0x4db26158), UINT32_C(0x3ab551ce), UINT32_C(0xa3bc0074), UINT32_C(0xd4bb30e2),
    UINT32_C(0x4adfa541), UINT32_C(0x3dd895d7), UINT32_C(0xa4d1c46d), UINT32_C(0xd3d6f4fb),
    UINT32_C(0x4369e96a), UINT32_C(0x346ed9fc), UINT32_C(0xad678846), UINT32_C(0xda60b8d0),
    UINT32_C(0x44042d73), UINT32_C(0x33031de5), UINT32_C(0xaa0a4c5f), UINT32_C(0xdd0d7cc9),
    UINT32_C(0x5005713c), UINT32_C(0x270241aa), UINT32_C(0xbe0b1010), UINT32_C(0xc90c2086),
    UINT32_C(0x5768b525), UINT32_C(0x206f85b3), UINT32_C(0xb966d409), UINT32_C(0xce61e49f),
    UINT32_C(0x5edef90e), UINT32_C(0x29d9c998), UINT32_C(0xb0d09822), UINT32_C(0xc7d7a8b4),
    UINT32_C(0x59b33d17), UINT32_C(0x2eb40d81), UINT32_C(0xb7bd5c3b), UINT32_C(0xc0ba6cad),
    UINT32_C(0xedb88320), UINT32_C(0x9abfb3b6), UINT32_C(0x03b6e20c), UINT32_C(0x74b1d29a),
    UINT32_C(0xead54739), UINT32_C(0x9dd277af), UINT32_C(0x04db2615), UINT32_C(0x73dc1683),
    UINT32_C(0xe3630b12), UINT32_C(0x94643b84), UINT32_C(0x0d6d6a3e), UINT32_C(0x7a6a5aa8),
    UINT32_C(0xe40ecf0b), UINT32_C(0x9309ff9d), UINT32_C(0x0a00ae27), UINT32_C(0x7d079eb1),
    UINT32_C(0xf00f9344), UINT32_C(0x8708a3d2), UINT32_C(0x1e01f268), UINT32_C(0x6906c2fe),
    UINT32_C(0xf762575d), UINT32_C(0x806567cb), UINT32_C(0x196c3671), UINT32_C(0x6e6b06e7),
    UINT32_C(0xfed41b76), UINT32_C(0x89d32be0), UINT32_C(0x10da7a5a), UINT32_C(0x67dd4acc),
    UINT32_C(0xf9b9df6f), UINT32_C(0x8ebeeff9), UINT32_C(0x17b7be43), UINT32_C(0x60b08ed5),
    UINT32_C(0xd6d6a3e8), UINT32_C(0xa1d1937e), UINT32_C(0x38d8c2c4), UINT32_C(0x4fdff252),
    UINT32_C(0xd1bb67f1), UINT32_C(0xa6bc5767), UINT32_C(0x3fb506dd), UINT32_C(0x48b2364b),
    UINT32_C(0xd80d2bda), UINT32_C(0xaf0a1b4c), UINT32_C(0x36034af6), UINT32_C(0x41047a60),
    UINT32_C(0xdf60efc3), UINT32_C(0xa867df55), UINT32_C(0x316e8eef), UINT32_C(0x4669be79),
    UINT32_C(0xcb61b38c), UINT32_C(0xbc66831a), UINT32_C(0x256fd2a0), UINT32_C(0x5268e236),
    UINT32_C(0xcc0c7795), UINT32_C(0xbb0b4703), UINT32_C(0x220216b9), UINT32_C(0x5505262f),
    UINT32_C(0xc5ba3bbe), UINT32_C(0xb2bd0b28), UINT32_C(0x2bb45a92), UINT32_C(0x5cb36a04),
    UINT32_C(0xc2d7ffa7), UINT32_C(0xb5d0cf31), UINT32_C(0x2cd99e8b), UINT32_C(0x5bdeae1d),
    UINT32_C(0x9b64c2b0), UINT32_C(0xec63f226), UINT32_C(0x756aa39c), UINT32_C(0x026d930a),
    UINT32_C(0x9c0906a9), UINT32_C(0xeb0e363f), UINT32_C(0x72076785), UINT32_C(0x05005713),
    UINT32_C(0x95bf4a82), UINT32_C(0xe2b87a14), UINT32_C(0x7bb12bae), UINT32_C(0x0cb61b38),
    UINT32_C(0x92d28e9b), UINT32_C(0xe5d5be0d), UINT32_C(0x7cdcefb7), UINT32_C(0x0bdbdf21),
    UINT32_C(0x86d3d2d4), UINT32_C(0xf1d4e242), UINT32_C(0x68ddb3f8), UINT32_C(0x1fda836e),
    UINT32_C(0x81be16cd), UINT32_C(0xf6b9265b), UINT32_C(0x6fb077e1), UINT32_C(0x18b74777),
    UINT32_C(0x88085ae6), UINT32_C(0xff0f6a70), UINT32_C(0x66063bca), UINT32_C(0x11010b5c),
    UINT32_C(0x8f659eff), UINT32_C(0xf862ae69), UINT32_C(0x616bffd3), UINT32_C(0x166ccf45),
    UINT32_C(0xa00ae278), UINT32_C(0xd70dd2ee), UINT32_C(0x4e048354), UINT32_C(0x3903b3c2),
    UINT32_C(0xa7672661), UINT32_C(0xd06016f7), UINT32_C(0x4969474d), UINT32_C(0x3e6e77db),
    UINT32_C(0xaed16a4a), UINT32_C(0xd9d65adc), UINT32_C(0x40df0b66), UINT32_C(0x37d83bf0),
    UINT32_C(0xa9bcae53), UINT32_C(0xdebb9ec5), UINT32_C(0x47b2cf7f), UINT32_C(0x30b5ffe9),
    UINT32_C(0xbdbdf21c), UINT32_C(0xcabac28a), UINT32_C(0x53b39330), UINT32_C(0x24b4a3a6),
    UINT32_C(0xbad03605), UINT32_C(0xcdd70693), UINT32_C(0x54de5729), UINT32_C(0x23d967bf),
    UINT32_C(0xb3667a2e), UINT32_C(0xc4614ab8), UINT32_C(0x5d681b02), UINT32_C(0x2a6f2b94),
    UINT32_C(0xb40bbe37), UINT32_C(0xc30c8ea1), UINT32_C(0x5a05df1b), UINT32_C(0x2d02ef8d)
};

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

static bool valid_filename(const char *filename)
{
    size_t length;

    if (filename == NULL || filename[0] == '\0') return false;
    length = strlen(filename);
    if (length >= KILIXSTATE_FILENAME_CAPACITY || length > NAME_MAX ||
        (length == 1u && filename[0] == '.') ||
        (length == 2u && filename[0] == '.' && filename[1] == '.'))
        return false;
    if (filename[0] != '.' &&
        !valid_component(filename, KILIXSTATE_FILENAME_CAPACITY))
        return false;
    for (size_t index = filename[0] == '.' ? 1u : length;
         index < length; ++index) {
        const unsigned char byte = (unsigned char)filename[index];
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
        bool synchronize_entry = false;
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
            if (mkdirat(directory_fd, component, 0700) == 0)
                synchronize_entry = true;
            else if (errno == EEXIST)
                synchronize_entry = true;
            else {
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
        if (synchronize_entry &&
            (fsync(next_fd) != 0 || fsync(directory_fd) != 0)) {
            const int failure = errno;
            (void)close(next_fd);
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
    options->base_directory = NULL;
    options->absolute_path = NULL;
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
    const char *filename;
    bool explicit_path;
    struct stat status;
    int length;
    int directory_fd;

    if (store == NULL) return KILIXSTATE_INVALID;
    (void)memset(store, 0, sizeof *store);
    store->directory_fd = -1;
    if (options == NULL ||
        options->max_payload == 0u ||
        options->max_payload > KILIXSTATE_MAX_PAYLOAD ||
        (options->format != KILIXSTATE_FORMAT_CRC32 &&
         options->format != KILIXSTATE_FORMAT_RAW)) return KILIXSTATE_INVALID;

    explicit_path = options->absolute_path != NULL &&
                    options->absolute_path[0] != '\0';
    if (explicit_path) {
        const char *slash;
        size_t directory_length;

        if (options->absolute_path[0] != '/') return KILIXSTATE_INVALID;
        slash = strrchr(options->absolute_path, '/');
        filename = slash != NULL ? slash + 1 : NULL;
        if (!valid_filename(filename)) return KILIXSTATE_INVALID;
        directory_length = (size_t)(slash - options->absolute_path);
        if (directory_length == 0u) directory_length = 1u;
        if (directory_length >= sizeof directory) return KILIXSTATE_INVALID;
        (void)memcpy(directory, options->absolute_path, directory_length);
        directory[directory_length] = '\0';
        length = (int)directory_length;
    } else {
        if (!valid_component(options->app_id,
                             KILIXSTATE_FILENAME_CAPACITY) ||
            !valid_component(options->filename,
                             KILIXSTATE_FILENAME_CAPACITY))
            return KILIXSTATE_INVALID;
        filename = options->filename;

        if (options->base_directory != NULL &&
            options->base_directory[0] != '\0') {
            if (options->base_directory[0] != '/')
                return KILIXSTATE_INVALID;
            length = snprintf(base, sizeof base, "%s",
                              options->base_directory);
        } else if ((xdg_data_home = getenv("XDG_DATA_HOME")) != NULL &&
                   xdg_data_home[0] != '\0') {
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
    }

    directory_fd = open_directory_path(directory);
    if (directory_fd < 0) return result_from_errno(errno);
    if (fstat(directory_fd, &status) != 0) {
        const int failure = errno;
        (void)close(directory_fd);
        return result_from_errno(failure);
    }
    if (!S_ISDIR(status.st_mode) ||
        (!explicit_path && status.st_uid != geteuid()) ||
        (explicit_path && (status.st_mode & (S_IWGRP | S_IWOTH)) != 0 &&
         (status.st_mode & S_ISVTX) == 0)) {
        (void)close(directory_fd);
        return KILIXSTATE_SECURITY;
    }
    if (!explicit_path &&
        (status.st_mode & (mode_t)07777) != (mode_t)0700) {
        if (fchmod(directory_fd, 0700) != 0 || fsync(directory_fd) != 0) {
            const int failure = errno;
            (void)close(directory_fd);
            return result_from_errno(failure);
        }
    }
    (void)memcpy(store->directory_path, directory, (size_t)length + 1u);
    (void)snprintf(store->filename, sizeof store->filename, "%s",
                   filename);
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
    for (size_t index = 0u; index < byte_count; ++index)
        crc = (crc >> 8) ^
              crc32_table[(crc ^ source[index]) & UINT32_C(0xff)];
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

typedef enum read_all_result {
    READ_ALL_OK,
    READ_ALL_EOF,
    READ_ALL_ERROR
} read_all_result;

static read_all_result read_all(int fd, void *bytes, size_t byte_count)
{
    unsigned char *destination = bytes;
    size_t offset = 0u;

    while (offset < byte_count) {
        const ssize_t count = read(fd, destination + offset,
                                   byte_count - offset);
        if (count > 0) offset += (size_t)count;
        else if (count == 0) return READ_ALL_EOF;
        else if (errno == EINTR) continue;
        else return READ_ALL_ERROR;
    }
    return READ_ALL_OK;
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

kilixstate_result kilixstate_remove(kilixstate_store *store)
{
    if (store == NULL || !store->initialized || store->directory_fd < 0)
        return KILIXSTATE_NOT_INITIALIZED;
    if (unlinkat(store->directory_fd, store->filename, 0) != 0) {
        if (errno == ENOENT) return KILIXSTATE_NOT_FOUND;
        return result_from_errno(errno);
    }
    if (fsync(store->directory_fd) != 0) return result_from_errno(errno);
    return KILIXSTATE_OK;
}

kilixstate_result kilixstate_load(kilixstate_store *store, void *payload,
                                  size_t payload_capacity,
                                  size_t *payload_size)
{
    unsigned char header[RECORD_HEADER_SIZE] = {0};
    struct stat status;
    size_t required;
    uint8_t format;
    int fd;

    if (payload_size != NULL) *payload_size = 0u;
    if (store == NULL || !store->initialized || store->directory_fd < 0)
        return KILIXSTATE_NOT_INITIALIZED;
    if (payload_size == NULL || (payload == NULL && payload_capacity != 0u))
        return KILIXSTATE_INVALID;
    format = store->format;
    if (format != KILIXSTATE_FORMAT_CRC32 &&
        format != KILIXSTATE_FORMAT_RAW)
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

    if (format == KILIXSTATE_FORMAT_CRC32) {
        read_all_result read_result;

        if (status.st_size < (off_t)RECORD_HEADER_SIZE) {
            (void)close(fd);
            return KILIXSTATE_CORRUPT;
        }
        read_result = read_all(fd, header, sizeof header);
        if (read_result != READ_ALL_OK) {
            const int failure = errno;
            (void)close(fd);
            return read_result == READ_ALL_EOF ? KILIXSTATE_CORRUPT :
                                                result_from_errno(failure);
        }
        if (memcmp(header, record_magic, sizeof record_magic) != 0 ||
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
    {
        const read_all_result read_result = read_all(fd, payload, required);
        if (read_result != READ_ALL_OK) {
            const int failure = errno;
            (void)close(fd);
            return read_result == READ_ALL_EOF ? KILIXSTATE_CORRUPT :
                                                result_from_errno(failure);
        }
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
    if (format == KILIXSTATE_FORMAT_CRC32 &&
        kilixstate_crc32(payload, required) != get_u32(header + 12u))
        return KILIXSTATE_CORRUPT;
    *payload_size = required;
    return KILIXSTATE_OK;
}

kilixstate_result kilixstate_store_path(const kilixstate_store *store,
                                        char *path, size_t path_size)
{
    const char *separator;
    int length;

    if (store == NULL || !store->initialized)
        return KILIXSTATE_NOT_INITIALIZED;
    if (path == NULL || path_size == 0u) return KILIXSTATE_INVALID;
    separator = strcmp(store->directory_path, "/") == 0 ? "" : "/";
    length = snprintf(path, path_size, "%s%s%s", store->directory_path,
                      separator, store->filename);
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
