#ifndef KILIX_STATE_H
#define KILIX_STATE_H

/* Crash-safe, bounded, private state files for terminal games and apps. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KILIXSTATE_VERSION_MAJOR 0
#define KILIXSTATE_VERSION_MINOR 1
#define KILIXSTATE_VERSION_PATCH 0

#define KILIXSTATE_DEFAULT_MAX_PAYLOAD (64u * 1024u)
#define KILIXSTATE_MAX_PAYLOAD (16u * 1024u * 1024u)
#define KILIXSTATE_PATH_CAPACITY 4096u
#define KILIXSTATE_FILENAME_CAPACITY 256u

typedef enum kilixstate_format {
    KILIXSTATE_FORMAT_CRC32 = 1,
    KILIXSTATE_FORMAT_RAW = 2
} kilixstate_format;

typedef enum kilixstate_result {
    KILIXSTATE_OK = 0,
    KILIXSTATE_NOT_FOUND = 1,
    KILIXSTATE_INVALID = 2,
    KILIXSTATE_TOO_LARGE = 3,
    KILIXSTATE_BUFFER_TOO_SMALL = 4,
    KILIXSTATE_CORRUPT = 5,
    KILIXSTATE_SECURITY = 6,
    KILIXSTATE_IO_ERROR = 7,
    KILIXSTATE_NOT_INITIALIZED = 8
} kilixstate_result;

typedef struct kilixstate_options {
    const char *app_id;
    const char *filename;
    size_t max_payload;
    kilixstate_format format;
} kilixstate_options;

/* Public for stack/static allocation; treat fields as private. */
typedef struct kilixstate_store {
    char directory_path[KILIXSTATE_PATH_CAPACITY];
    char filename[KILIXSTATE_FILENAME_CAPACITY];
    size_t max_payload;
    int directory_fd;
    uint8_t format;
    bool initialized;
} kilixstate_store;

void kilixstate_options_init(kilixstate_options *options);

/*
 * Resolve and create $XDG_DATA_HOME/app_id (or ~/.local/share/app_id), open
 * it without following symlinks, and normalize the app directory to 0700.
 */
kilixstate_result kilixstate_store_init(kilixstate_store *store,
                                        const kilixstate_options *options);
void kilixstate_store_close(kilixstate_store *store);

/* Writes mode 0600 through a same-directory temporary, fsync, and rename. */
kilixstate_result kilixstate_save(kilixstate_store *store,
                                  const void *payload, size_t payload_size);

/*
 * Loads a regular no-follow file. On BUFFER_TOO_SMALL, *payload_size is the
 * required capacity. On every other failure it is zero.
 */
kilixstate_result kilixstate_load(kilixstate_store *store, void *payload,
                                  size_t payload_capacity,
                                  size_t *payload_size);

/* Full diagnostic path; state I/O still uses the stable directory fd. */
kilixstate_result kilixstate_store_path(const kilixstate_store *store,
                                        char *path, size_t path_size);

uint32_t kilixstate_crc32(const void *bytes, size_t byte_count);
const char *kilixstate_result_name(kilixstate_result result);

#ifdef __cplusplus
}
#endif

#endif
