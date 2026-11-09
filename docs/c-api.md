# Using kilix-state from C

The store lifecycle, the payload codec and migration tables, where state is
kept, and the raw format for existing files. The rules every caller must keep
are in the README ([Contract](../README.md#contract)).

## Use

```c
#include "kilix_state.h"

kilixstate_options options;
kilixstate_store store;
GameSave save;
size_t size;

kilixstate_options_init(&options);
options.app_id = "my-game";
options.filename = "save.state";
options.max_payload = sizeof save;

if (kilixstate_store_init(&store, &options) == KILIXSTATE_OK) {
    if (kilixstate_load(&store, &save, sizeof save, &size) != KILIXSTATE_OK)
        save = default_save();

    /* ... mutate save ... */
    (void)kilixstate_save(&store, &save, sizeof save);
    kilixstate_store_close(&store);
}
```

A successfully initialized `kilixstate_store` owns a directory descriptor.
Treat a live store as non-copyable and call `kilixstate_store_close()` before
reusing its storage.

Game-owned save payloads can use `kilix_state_codec.h` instead of repeating
unchecked byte offsets:

```c
kilixstate_writer writer;
kilixstate_writer_init(&writer, payload, sizeof payload);
kilixstate_write_u32(&writer, SAVE_VERSION);
kilixstate_write_i32(&writer, player_hp);
kilixstate_write_zeroes(&writer, kilixstate_writer_remaining(&writer));
```

Migration tables select a unique leading little-endian version, optionally
require an exact payload size and canonical zero tail, and call a game-owned
decoder with the version already consumed. Decoders should populate temporary
state and publish it only after successful validation. Scalar codec operations
consume the complete value or leave their offset unchanged.

State lives below `$XDG_DATA_HOME/<app_id>` when `XDG_DATA_HOME` is an
absolute path, otherwise below `$HOME/.local/share/<app_id>`. Every path
component is opened without following symlinks; the app directory is private
mode 0700. App IDs and filenames are single conservative path components.

Tests, portable bundles, and embedding hosts can set
`options.base_directory` to an absolute root. The app ID is still appended,
and the override uses the same component-by-component no-symlink traversal;
relative overrides are rejected.

Legacy games and embedding hosts that already expose a full save-file override
can instead set `options.absolute_path`. Its parent is opened without following
symlinks, but existing parent permissions are never changed. Group- or
world-writable parents must use the sticky bit (as `/tmp` does), the target
must still be a regular file owned by the caller, and writes retain the same
private temporary-file and atomic-replace contract.

For an existing byte-for-byte file format, select `KILIXSTATE_FORMAT_RAW`.
Raw storage retains the secure and atomic filesystem behavior but omits the
record header and checksum, which makes it useful for compatibility
migrations. New state should use the default CRC32 format.

