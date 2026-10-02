/* Deterministic fault injection and overlapping initializer scheduling.
 * All fsync calls are mocked; only private temporary directories are created.
 */
#include "kilix_state.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        (void)fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return EXIT_FAILURE; \
    } \
} while (0)

static struct stat parent_identity;
static kilixstate_options options;
static unsigned int parent_syncs;
static unsigned int nested_syncs;
static bool fail_parent;
static bool overlap;
static bool in_nested;
static kilixstate_result nested_result;

int __wrap_fsync(int fd);
int __wrap_fsync(int fd)
{
    struct stat status;
    struct stat child;
    if (fstat(fd, &status) != 0) return -1;
    if (status.st_dev != parent_identity.st_dev ||
        status.st_ino != parent_identity.st_ino ||
        fstatat(fd, "app", &child, 0) != 0)
        return 0;
    ++parent_syncs;
    if (in_nested) ++nested_syncs;
    if (fail_parent) {
        fail_parent = false;
        errno = EIO;
        return -1;
    }
    if (overlap) {
        kilixstate_store second;
        /* The first initializer has created app but has not yet completed
         * its parent sync. Run a second initializer at precisely that point. */
        overlap = false;
        in_nested = true;
        nested_result = kilixstate_store_init(&second, &options);
        if (nested_result == KILIXSTATE_OK) kilixstate_store_close(&second);
        in_nested = false;
    }
    return 0;
}

int main(void)
{
    char root_template[KILIXSTATE_PATH_CAPACITY];
    char child[KILIXSTATE_PATH_CAPACITY];
    const char *temporary = getenv("TMPDIR");
    kilixstate_store store;
    (void)snprintf(root_template, sizeof root_template, "%s/state-sync-XXXXXX",
                   temporary ? temporary : "/tmp");
    char *root = mkdtemp(root_template);
    CHECK(root != NULL);
    CHECK(stat(root, &parent_identity) == 0);
    CHECK(snprintf(child, sizeof child, "%s/app", root) < (int)sizeof child);
    kilixstate_options_init(&options);
    options.base_directory = root;
    options.app_id = "app";
    options.filename = "state";

    fail_parent = true;
    CHECK(kilixstate_store_init(&store, &options) == KILIXSTATE_IO_ERROR);
    CHECK(!store.initialized);
    CHECK(parent_syncs == 1u);
    CHECK(access(child, F_OK) == 0);
    parent_syncs = 0u;
    CHECK(kilixstate_store_init(&store, &options) == KILIXSTATE_OK);
    CHECK(parent_syncs > 0u);
    kilixstate_store_close(&store);

    /* An existing path must also propagate a sync failure. */
    fail_parent = true;
    CHECK(kilixstate_store_init(&store, &options) == KILIXSTATE_IO_ERROR);
    CHECK(!store.initialized);
    CHECK(rmdir(child) == 0);

    overlap = true;
    nested_result = KILIXSTATE_NOT_INITIALIZED;
    CHECK(kilixstate_store_init(&store, &options) == KILIXSTATE_OK);
    CHECK(nested_result == KILIXSTATE_OK);
    CHECK(nested_syncs > 0u);
    kilixstate_store_close(&store);
    CHECK(rmdir(child) == 0);
    CHECK(rmdir(root) == 0);
    (void)puts("ok: directory sync retry and overlapping initialization");
    return EXIT_SUCCESS;
}
