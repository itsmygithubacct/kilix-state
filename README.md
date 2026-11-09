# kilix-state

`kilix-state` is a dependency-free C11/POSIX library for small game and app
saves. It turns the repeated “find an XDG path, mkdir, write a struct” code
into one bounded and crash-safe lifecycle. Its allocation-free codec layer
also provides endian-stable payload readers/writers and explicit
version-migration dispatch without imposing a game schema.

Ships in Plebian-OS 0.2.3 as a submodule of
[Kilix](https://github.com/itsmygithubacct/kilix#readme)
(`third_party/kilix-state`). Kilix builds the shared library into its private
build directory, and desktop providers save their state through the Python
binding that `kilix_sdk.state` exposes.

## What it does

The default record format adds a versioned header and CRC32. Writes use a
mode-0600 same-directory temporary file, `fsync()`, atomic `renameat()`, and
a directory `fsync()`. Newly created path components are also synchronized
with their parent before initialization succeeds. Reads use a stable directory
descriptor plus `O_NOFOLLOW`, accept only regular files owned by the current
user, enforce a configured maximum before reading, and reject truncated or
corrupt records.

- One store per app ID and filename, below `$XDG_DATA_HOME/<app_id>` (or
  `~/.local/share/<app_id>`), with an explicit `max_payload` of at most 16 MiB.
- Crash-safe saves: private temporary file, `fsync()`, atomic rename,
  directory `fsync()`.
- No symlinks followed on any path component; only regular files owned by
  the caller are read.
- An allocation-free codec (`kilix_state_codec.h`) for endian-stable payloads
  and version-migration dispatch.
- A typed, pure-Python binding in [`python/`](python) over the same shared
  library.

## Build and test


```sh
make test
make sanitize
make benchmark         # manual native measurements
make python-benchmark  # manual binding measurements
```

The build produces static and shared libraries with no dependencies beyond
the C/POSIX system libraries. Benchmark results are informational rather than
pass/fail tests and should be compared under the same build and host workload.

The typed, pure-Python binding is maintained in [`python/`](python). It uses
the same shared library and keeps persistence semantics in the native code:

```sh
make python-check
make python-wheel
```

The wheel is written below `python/dist/`.


The smallest C use is `kilixstate_options_init`, set `app_id`, `filename`
and `max_payload`, then `kilixstate_store_init`, `kilixstate_load` /
`kilixstate_save` and `kilixstate_store_close`; the full example is in
[docs/c-api.md](docs/c-api.md).

## Usage

| Function | What it does |
|---|---|
| `kilixstate_options_init(&options)` | Defaults; then set `app_id`, `filename`, `max_payload`, optionally `base_directory` or `absolute_path` |
| `kilixstate_store_init(&store, &options)` | Resolve and open the state directory; the store owns a directory descriptor |
| `kilixstate_load(&store, buf, size, &got)` | Read and validate one record into `buf` |
| `kilixstate_save(&store, payload, size)` | Atomically replace the record |
| `kilixstate_remove(&store)` | Remove the record and persist the directory update |
| `kilixstate_store_path(&store, ...)` | The resolved file path |
| `kilixstate_store_close(&store)` | Release the store; idempotent |
| `kilixstate_writer_init`, `kilixstate_write_*`, `kilixstate_reader_init`, `kilixstate_read_*` | Bounded little-endian payload codec in `kilix_state_codec.h` |
| `kilixstate_migrate(payload, size, migrations, count, context, &version)` | Dispatch on the leading little-endian version to a game-owned decoder |

| Make target | What it does |
|---|---|
| `make` / `make test` / `make sanitize` | Static and shared libraries; tests; tests under sanitizers |
| `make benchmark`, `make python-benchmark` | Informational native and binding measurements |
| `make python-check`, `make python-wheel` | Binding tests; a wheel below `python/dist/` |
| `make install PREFIX=...` | Install headers and libraries |

## How it works

```
 app ──kilixstate_save()──> header (magic, version, size, CRC32) + payload
                             │
        open dir (O_NOFOLLOW, per component) ── 0700 app directory
                             │
        write .tmp (0600) ── fsync ── renameat ── fsync(dir)
 app <─kilixstate_load()── check owner, regular file, max size, header, CRC
```

The default record wraps the payload in a versioned header and a CRC32, so a
torn or corrupted file is rejected rather than half-read. The store keeps a
directory descriptor opened without following symlinks and does every
operation relative to it. `KILIXSTATE_FORMAT_RAW` keeps the same filesystem
discipline for an existing byte-for-byte format. Details, including the codec
and migration tables: [docs/c-api.md](docs/c-api.md).

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


## Documentation

| Document | Contents |
|---|---|
| [docs/c-api.md](docs/c-api.md) | The C store lifecycle, codec, migrations, state paths, overrides and the raw format |
| [python/README.md](python/README.md) | The Python binding: requirements, library discovery, `Store` API |
| [python/CHANGELOG.md](python/CHANGELOG.md) | Binding changes |

## Related repositories

- [kilix](https://github.com/itsmygithubacct/kilix#readme): pins this library
  and exposes the binding to desktop providers as `kilix_sdk.state`.
- [kilix-95](https://github.com/itsmygithubacct/kilix-95#readme): a desktop
  provider that keeps its state through that binding.
- [kilix-game-sdk](https://github.com/itsmygithubacct/kilix-game-sdk#readme):
  carries its own pinned copy as a submodule, compiled into the games.

## Part of Plebian-OS

kilix-state is one of the repositories behind
[Plebian-OS](https://github.com/itsmygithubacct/plebian-os#readme), a Debian
install whose desktop is [Kilix](https://github.com/itsmygithubacct/kilix#readme)
running the [Kilix 95](https://github.com/itsmygithubacct/kilix-95#readme)
desktop through [Pleb](https://github.com/itsmygithubacct/pleb#readme).
It ships inside Kilix as the `third_party/kilix-state` submodule, which builds
the library that desktop providers save their state through; games compile
their own copy through kilix-game-sdk. The map of every repository and how it
ships is on the
[components page](https://github.com/itsmygithubacct/plebian-os/blob/main/docs/components.md).

## License

MIT
