# kilix-state

`kilix-state` is a dependency-free C11/POSIX library for small game and app
saves. It turns the repeated “find an XDG path, mkdir, write a struct” code
into one bounded and crash-safe lifecycle.

The default record format adds a versioned header and CRC32. Writes use a
mode-0600 same-directory temporary file, `fsync()`, atomic `renameat()`, and
a directory `fsync()`. Reads use a stable directory descriptor plus
`O_NOFOLLOW`, accept only regular files owned by the current user, enforce a
configured maximum before reading, and reject truncated or corrupt records.

## Build and test

```sh
make test
make sanitize
```

The build produces static and shared libraries with no dependencies beyond
the C/POSIX system libraries.

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

## Contract

- `max_payload` is mandatory and capped at 16 MiB.
- A too-small load buffer reports the required size without a partial load.
- Store initialization and close are explicit; close is idempotent.
- Removal uses the same stable directory descriptor and persists the directory
  update before reporting success.
- Saving, replacing, or closing a store must not race another operation on
  that store.
- Structured payloads should contain their own schema version and avoid raw
  pointers, padding-dependent comparisons, or host-endian portability
  assumptions.

## License

MIT
