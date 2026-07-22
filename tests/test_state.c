#include "kilix_state.h"

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

int main(void)
{
    char root_template[] = "/tmp/kilix-state-test-XXXXXX";
    char *root = mkdtemp(root_template);

    if (root == NULL || setenv("XDG_DATA_HOME", root, 1) != 0 ||
        !test_crc_record_and_atomic_replace(root) ||
        !test_raw_compatibility() || !test_symlink_defenses(root) ||
        !test_validation() || !test_explicit_base_directory(root) ||
        !test_absolute_path_compatibility(root) || rmdir(root) != 0 ||
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
