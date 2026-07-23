#include "kilix_state.h"
#include "kilix_state_codec.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n",             \
                          __FILE__, __LINE__, #condition);                     \
            return false;                                                     \
        }                                                                     \
    } while (false)

static bool no_temporary_files(const char *directory)
{
    DIR *stream = opendir(directory);
    struct dirent *entry;
    bool clean = true;

    if (stream == NULL) return false;
    while ((entry = readdir(stream)) != NULL) {
        if (strstr(entry->d_name, ".tmp.") != NULL) {
            clean = false;
            break;
        }
    }
    (void)closedir(stream);
    return clean;
}

static bool test_crc_record_and_atomic_replace(const char *root)
{
    static const unsigned char state[] = {1u, 2u, 3u, 4u, 5u};
    kilixstate_options options;
    kilixstate_store store;
    unsigned char loaded[32] = {0};
    size_t loaded_size = 99u;
    char path[KILIXSTATE_PATH_CAPACITY];
    struct stat status;
    int fd;

    CHECK(kilixstate_crc32("123456789", 9u) == 0xcbf43926u);
    kilixstate_options_init(&options);
    options.app_id = "test-game";
    options.filename = "save.state";
    options.max_payload = 32u;
    CHECK(kilixstate_store_init(&store, &options) == KILIXSTATE_OK);
    CHECK(kilixstate_load(&store, loaded, sizeof loaded, &loaded_size) ==
          KILIXSTATE_NOT_FOUND);
    CHECK(loaded_size == 0u);
    CHECK(kilixstate_save(&store, state, sizeof state) == KILIXSTATE_OK);
    CHECK(kilixstate_store_path(&store, path, sizeof path) == KILIXSTATE_OK);
    CHECK(strncmp(path, root, strlen(root)) == 0);
    CHECK(stat(path, &status) == 0);
    CHECK((status.st_mode & 0777) == 0600);
    CHECK(stat(store.directory_path, &status) == 0);
    CHECK((status.st_mode & 0777) == 0700);
    CHECK(no_temporary_files(store.directory_path));

    CHECK(kilixstate_load(&store, loaded, 2u, &loaded_size) ==
          KILIXSTATE_BUFFER_TOO_SMALL);
    CHECK(loaded_size == sizeof state);
    CHECK(kilixstate_load(&store, loaded, sizeof loaded, &loaded_size) ==
          KILIXSTATE_OK);
    CHECK(loaded_size == sizeof state);
    CHECK(memcmp(loaded, state, sizeof state) == 0);
    CHECK(kilixstate_save(&store, loaded, 33u) == KILIXSTATE_TOO_LARGE);

    /* Flip one payload byte; the record remains bounded but fails CRC. */
    fd = open(path, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    CHECK(fd >= 0);
    CHECK(lseek(fd, -1, SEEK_END) >= 0);
    loaded[0] = 0x55u;
    CHECK(write(fd, loaded, 1u) == 1);
    CHECK(close(fd) == 0);
    loaded_size = 99u;
    CHECK(kilixstate_load(&store, loaded, sizeof loaded, &loaded_size) ==
          KILIXSTATE_CORRUPT);
    CHECK(loaded_size == 0u);

    /* A later save atomically replaces the corrupt inode. */
    CHECK(kilixstate_save(&store, state, sizeof state) == KILIXSTATE_OK);
    CHECK(kilixstate_load(&store, loaded, sizeof loaded, &loaded_size) ==
          KILIXSTATE_OK);
    CHECK(memcmp(loaded, state, sizeof state) == 0);
    CHECK(kilixstate_remove(&store) == KILIXSTATE_OK);
    CHECK(kilixstate_load(&store, loaded, sizeof loaded, &loaded_size) ==
          KILIXSTATE_NOT_FOUND);
    CHECK(kilixstate_remove(&store) == KILIXSTATE_NOT_FOUND);
    kilixstate_store_close(&store);
    kilixstate_store_close(&store);
    CHECK(rmdir(store.directory_path) == 0);
    return true;
}

static bool test_raw_compatibility(void)
{
    static const char state[] = "plain legacy state";
    kilixstate_options options;
    kilixstate_store store;
    char loaded[64] = "";
    char path[KILIXSTATE_PATH_CAPACITY];
    size_t loaded_size = 0u;
    struct stat status;

    kilixstate_options_init(&options);
    options.app_id = "raw-game";
    options.filename = "settings.dat";
    options.format = KILIXSTATE_FORMAT_RAW;
    CHECK(kilixstate_store_init(&store, &options) == KILIXSTATE_OK);
    CHECK(kilixstate_save(&store, state, sizeof state) == KILIXSTATE_OK);
    CHECK(kilixstate_store_path(&store, path, sizeof path) == KILIXSTATE_OK);
    CHECK(stat(path, &status) == 0);
    CHECK(status.st_size == (off_t)sizeof state);
    CHECK(kilixstate_load(&store, loaded, sizeof loaded, &loaded_size) ==
          KILIXSTATE_OK);
    CHECK(loaded_size == sizeof state);
    CHECK(memcmp(loaded, state, sizeof state) == 0);
    kilixstate_store_close(&store);
    CHECK(unlink(path) == 0);
    CHECK(rmdir(store.directory_path) == 0);
    return true;
}

static bool test_symlink_defenses(const char *root)
{
    kilixstate_options options;
    kilixstate_store store;
    char link_path[KILIXSTATE_PATH_CAPACITY];
    char target_path[KILIXSTATE_PATH_CAPACITY];
    char app_path[KILIXSTATE_PATH_CAPACITY];
    char state_path[KILIXSTATE_PATH_CAPACITY];
    char byte = 'x';
    size_t size = 0u;
    struct stat status;
    int fd;

    (void)snprintf(target_path, sizeof target_path, "%s/target", root);
    (void)snprintf(link_path, sizeof link_path, "%s/badapp", root);
    CHECK(mkdir(target_path, 0700) == 0);
    CHECK(symlink(target_path, link_path) == 0);
    kilixstate_options_init(&options);
    options.app_id = "badapp";
    options.filename = "save.state";
    CHECK(kilixstate_store_init(&store, &options) == KILIXSTATE_SECURITY);
    CHECK(unlink(link_path) == 0);
    CHECK(rmdir(target_path) == 0);

    options.app_id = "link-game";
    CHECK(kilixstate_store_init(&store, &options) == KILIXSTATE_OK);
    CHECK(kilixstate_store_path(&store, state_path, sizeof state_path) ==
          KILIXSTATE_OK);
    (void)snprintf(target_path, sizeof target_path, "%s/target-file", root);
    fd = open(target_path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    CHECK(fd >= 0);
    CHECK(write(fd, &byte, 1u) == 1);
    CHECK(close(fd) == 0);
    CHECK(symlink(target_path, state_path) == 0);
    CHECK(kilixstate_load(&store, &byte, 1u, &size) == KILIXSTATE_SECURITY);
    CHECK(kilixstate_save(&store, &byte, 1u) == KILIXSTATE_OK);
    CHECK(lstat(state_path, &status) == 0);
    CHECK(S_ISREG(status.st_mode));
    fd = open(target_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(fd >= 0);
    CHECK(read(fd, &byte, 1u) == 1 && byte == 'x');
    CHECK(close(fd) == 0);
    (void)snprintf(app_path, sizeof app_path, "%s", store.directory_path);
    kilixstate_store_close(&store);
    CHECK(unlink(state_path) == 0);
    CHECK(unlink(target_path) == 0);
    CHECK(rmdir(app_path) == 0);
    return true;
}

static bool test_validation(void)
{
    kilixstate_options options;
    kilixstate_store store;
    size_t size = 0u;

    kilixstate_options_init(&options);
    options.app_id = "../escape";
    options.filename = "state";
    CHECK(kilixstate_store_init(&store, &options) == KILIXSTATE_INVALID);
    options.app_id = "valid";
    options.filename = ".hidden";
    CHECK(kilixstate_store_init(&store, &options) == KILIXSTATE_INVALID);
    options.filename = "state";
    options.max_payload = KILIXSTATE_MAX_PAYLOAD + 1u;
    CHECK(kilixstate_store_init(&store, &options) == KILIXSTATE_INVALID);
    CHECK(kilixstate_load(&store, NULL, 0u, &size) ==
          KILIXSTATE_NOT_INITIALIZED);
    CHECK(strcmp(kilixstate_result_name(KILIXSTATE_CORRUPT),
                 "corrupt state") == 0);
    return true;
}

static bool test_explicit_base_directory(const char *root)
{
    static const char state[] = "isolated state";
    kilixstate_options options;
    kilixstate_store store;
    char loaded[32] = "";
    char path[KILIXSTATE_PATH_CAPACITY];
    size_t loaded_size = 0u;

    CHECK(setenv("XDG_DATA_HOME", "relative/ignored", 1) == 0);
    kilixstate_options_init(&options);
    options.app_id = "override-game";
    options.filename = "profile.dat";
    options.base_directory = root;
    options.format = KILIXSTATE_FORMAT_RAW;
    CHECK(kilixstate_store_init(&store, &options) == KILIXSTATE_OK);
    CHECK(kilixstate_save(&store, state, sizeof state) == KILIXSTATE_OK);
    CHECK(kilixstate_load(&store, loaded, sizeof loaded, &loaded_size) ==
          KILIXSTATE_OK);
    CHECK(loaded_size == sizeof state);
    CHECK(memcmp(loaded, state, sizeof state) == 0);
    CHECK(kilixstate_store_path(&store, path, sizeof path) == KILIXSTATE_OK);
    CHECK(strncmp(path, root, strlen(root)) == 0);
    kilixstate_store_close(&store);
    CHECK(unlink(path) == 0);
    CHECK(rmdir(store.directory_path) == 0);

    options.base_directory = "relative";
    CHECK(kilixstate_store_init(&store, &options) == KILIXSTATE_INVALID);
    return true;
}

static bool test_absolute_path_compatibility(const char *root)
{
    static const unsigned char state[] = {9u, 8u, 7u};
    kilixstate_options options;
    kilixstate_store store;
    unsigned char loaded[8] = {0};
    char path[KILIXSTATE_PATH_CAPACITY];
    size_t loaded_size = 0u;

    CHECK(snprintf(path, sizeof path, "%s/.legacy-save", root) > 0);
    kilixstate_options_init(&options);
    options.absolute_path = path;
    options.max_payload = sizeof loaded;
    options.format = KILIXSTATE_FORMAT_RAW;
    CHECK(kilixstate_store_init(&store, &options) == KILIXSTATE_OK);
    CHECK(strcmp(store.directory_path, root) == 0);
    CHECK(strcmp(store.filename, ".legacy-save") == 0);
    CHECK(kilixstate_save(&store, state, sizeof state) == KILIXSTATE_OK);
    CHECK(kilixstate_load(&store, loaded, sizeof loaded, &loaded_size) ==
          KILIXSTATE_OK);
    CHECK(loaded_size == sizeof state);
    CHECK(memcmp(loaded, state, sizeof state) == 0);
    kilixstate_store_close(&store);
    CHECK(unlink(path) == 0);

    options.absolute_path = "relative/save";
    CHECK(kilixstate_store_init(&store, &options) == KILIXSTATE_INVALID);
    return true;
}

typedef struct migration_result {
    int32_t score;
    bool enabled;
} migration_result;

static bool decode_v1(kilixstate_reader *reader, void *context)
{
    migration_result *result = context;
    return result && kilixstate_read_i32(reader, &result->score) &&
           kilixstate_read_bool(reader, &result->enabled);
}

static bool test_codec(void)
{
    uint8_t payload[32];
    uint8_t copied[3] = {0};
    static const uint8_t source[3] = {7u, 8u, 9u};
    kilixstate_writer writer;
    kilixstate_reader reader;
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    int32_t i32;
    bool boolean;

    kilixstate_writer_init(&writer, payload, sizeof payload);
    CHECK(kilixstate_write_u8(&writer, UINT8_C(0xa5)));
    CHECK(kilixstate_write_u16(&writer, UINT16_C(0x1234)));
    CHECK(kilixstate_write_u32(&writer, UINT32_C(0x89abcdef)));
    CHECK(kilixstate_write_u64(&writer, UINT64_C(0x0123456789abcdef)));
    CHECK(kilixstate_write_i32(&writer, INT32_C(-1234567)));
    CHECK(kilixstate_write_bool(&writer, true));
    CHECK(kilixstate_write_bytes(&writer, source, sizeof source));
    CHECK(kilixstate_write_zeroes(
        &writer, kilixstate_writer_remaining(&writer)));
    CHECK(kilixstate_writer_size(&writer) == sizeof payload);
    CHECK(kilixstate_writer_result(&writer) == KILIXSTATE_CODEC_OK);
    CHECK(payload[1] == UINT8_C(0x34) && payload[2] == UINT8_C(0x12));

    kilixstate_reader_init(&reader, payload, sizeof payload);
    CHECK(kilixstate_read_u8(&reader, &u8) && u8 == UINT8_C(0xa5));
    CHECK(kilixstate_read_u16(&reader, &u16) && u16 == UINT16_C(0x1234));
    CHECK(kilixstate_read_u32(&reader, &u32) &&
          u32 == UINT32_C(0x89abcdef));
    CHECK(kilixstate_read_u64(&reader, &u64) &&
          u64 == UINT64_C(0x0123456789abcdef));
    CHECK(kilixstate_read_i32(&reader, &i32) && i32 == INT32_C(-1234567));
    CHECK(kilixstate_read_bool(&reader, &boolean) && boolean);
    CHECK(kilixstate_read_bytes(&reader, copied, sizeof copied));
    CHECK(memcmp(copied, source, sizeof source) == 0);
    CHECK(kilixstate_reader_require_zero_tail(&reader));
    CHECK(kilixstate_reader_remaining(&reader) == 0u);

    kilixstate_writer_init(&writer, payload, 1u);
    CHECK(!kilixstate_write_u32(&writer, UINT32_C(1)));
    CHECK(kilixstate_writer_result(&writer) == KILIXSTATE_CODEC_NO_SPACE);
    kilixstate_reader_init(&reader, payload, 1u);
    CHECK(!kilixstate_read_u32(&reader, &u32));
    CHECK(kilixstate_reader_result(&reader) == KILIXSTATE_CODEC_TRUNCATED);
    payload[0] = UINT8_C(2);
    kilixstate_reader_init(&reader, payload, 1u);
    CHECK(!kilixstate_read_bool(&reader, &boolean));
    CHECK(kilixstate_reader_result(&reader) ==
          KILIXSTATE_CODEC_NONCANONICAL);
    return true;
}

static bool test_migrations(void)
{
    uint8_t payload[16];
    kilixstate_writer writer;
    migration_result decoded = {0};
    uint32_t version = 0u;
    kilixstate_migration migrations[] = {
        {1u, sizeof payload, true, decode_v1}
    };

    kilixstate_writer_init(&writer, payload, sizeof payload);
    CHECK(kilixstate_write_u32(&writer, UINT32_C(1)));
    CHECK(kilixstate_write_i32(&writer, INT32_C(-17)));
    CHECK(kilixstate_write_bool(&writer, true));
    CHECK(kilixstate_write_zeroes(
        &writer, kilixstate_writer_remaining(&writer)));
    CHECK(kilixstate_migrate(payload, sizeof payload, migrations, 1u,
                             &decoded, &version) == KILIXSTATE_CODEC_OK);
    CHECK(version == 1u && decoded.score == -17 && decoded.enabled);

    payload[15] = UINT8_C(1);
    CHECK(kilixstate_migrate(payload, sizeof payload, migrations, 1u,
                             &decoded, NULL) ==
          KILIXSTATE_CODEC_NONCANONICAL);
    payload[15] = 0u;
    payload[0] = UINT8_C(2);
    CHECK(kilixstate_migrate(payload, sizeof payload, migrations, 1u,
                             &decoded, NULL) ==
          KILIXSTATE_CODEC_UNKNOWN_VERSION);
    payload[0] = UINT8_C(1);
    CHECK(kilixstate_migrate(payload, sizeof payload - 1u, migrations, 1u,
                             &decoded, NULL) ==
          KILIXSTATE_CODEC_SIZE_MISMATCH);
    migrations[0].payload_size = 0u;
    migrations[0].require_zero_tail = false;
    {
        kilixstate_migration duplicate[] = {
            migrations[0], migrations[0]
        };
        CHECK(kilixstate_migrate(payload, sizeof payload, duplicate, 2u,
                                 &decoded, NULL) ==
              KILIXSTATE_CODEC_DUPLICATE_VERSION);
    }
    CHECK(strcmp(kilixstate_codec_result_name(
                     KILIXSTATE_CODEC_UNKNOWN_VERSION),
                 "unknown version") == 0);
    return true;
}

int main(void)
{
    char root_template[] = "/tmp/kilix-state-test-XXXXXX";
    char *root = mkdtemp(root_template);

    if (root == NULL || setenv("XDG_DATA_HOME", root, 1) != 0 ||
        !test_crc_record_and_atomic_replace(root) ||
        !test_raw_compatibility() || !test_symlink_defenses(root) ||
        !test_validation() || !test_explicit_base_directory(root) ||
        !test_absolute_path_compatibility(root) || !test_codec() ||
        !test_migrations() || rmdir(root) != 0 ||
        setenv("XDG_DATA_HOME", "relative/path", 1) != 0)
        return EXIT_FAILURE;
    {
        kilixstate_options options;
        kilixstate_store store;
        kilixstate_options_init(&options);
        options.app_id = "game";
        options.filename = "state";
        if (kilixstate_store_init(&store, &options) != KILIXSTATE_INVALID)
            return EXIT_FAILURE;
    }
    (void)puts("ok: kilix-state");
    return 0;
}
