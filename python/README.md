# kilix-state-py

`kilix-state-py` is a typed, dependency-free Python binding for
[`kilix-state`](https://github.com/itsmygithubacct/kilix-state), the bounded,
crash-safe state library shared by Kilix games and apps. It exposes the native
0.4 record and path-security contract through a small Pythonic `Store` API.

The wheel contains only Python. The native library remains the source of truth
for XDG path resolution, no-follow traversal, ownership checks, record bounds,
CRC validation, atomic replacement, and durable directory updates.

## Requirements

- Python 3.10 or newer
- `libkilix-state` 0.4.x built as a shared library
- A POSIX system supported by the native library

For sibling source checkouts:

```bash
cd kilix-state-py
make check
```

The loader finds `../kilix-state/build/libkilix-state.so` automatically. An
installed or unusual build can be selected explicitly:

```bash
export KILIX_STATE_LIBRARY=/path/to/libkilix-state.so
python3 your_program.py
```

Discovery checks, in order: an explicit `KilixStateLibrary(path)`, the
`KILIX_STATE_LIBRARY` environment override, a package-local `_libs` directory,
conventional sibling checkouts, the platform library registry, and dynamic
loader defaults. An explicit or environment path is authoritative.

Install the binding without bundling the native library:

```bash
python3 -m pip install .
```

## Quick start

```python
import json

from kilix_state import StateNotFoundError, Store

with Store(
    "kilix-95",
    "desktop.json",
    max_payload=256 * 1024,
) as state:
    try:
        settings = json.loads(state.load())
    except StateNotFoundError:
        settings = {"full_experience": False}

    settings["full_experience"] = True
    state.save(json.dumps(settings, sort_keys=True).encode("utf-8"))
```

By default, the record lives at
`$XDG_DATA_HOME/<app_id>/<filename>`, falling back to
`$HOME/.local/share/<app_id>/<filename>`. The app directory is private mode
0700 and records are mode 0600.

`load_or()` handles only an absent record; corruption and security failures
remain visible:

```python
payload = state.load_or(b"{}")
```

## Explicit and legacy paths

Tests, bundles, and embedding hosts can select an absolute storage root while
retaining the app namespace:

```python
store = Store(
    "kilix-95",
    "session.state",
    base_directory="/var/lib/my-host",
)
```

Legacy applications with a complete save path can use `absolute_path`. The
filename may be hidden, and `app_id`/`filename` are not required:

```python
from kilix_state import Format, Store

store = Store(
    absolute_path="/srv/session/.legacy-save",
    format=Format.RAW,
)
```

`Format.RAW` stores byte-for-byte payloads while retaining secure traversal,
private temporary files, `fsync()`, and atomic replacement. New records should
use the default `Format.CRC32`, which is byte-compatible with C consumers.

## Errors and lifecycle

Native results become specific exceptions:

- `StateNotFoundError`
- `InvalidStateError`
- `StateTooLargeError`
- `CorruptStateError`
- `UnsafeStatePathError`
- `StateIOError`
- `StoreClosedError`

All inherit from `KilixStateError`; native operation failures additionally
inherit from `StateOperationError` and expose `.result` and `.operation`.
`remove(missing_ok=True)` returns `False` for a missing record.

`Store.close()` is idempotent, and `Store` is a context manager. Calls on one
instance are serialized with a reentrant lock because the native object owns a
stable directory descriptor. Independent processes get atomic, coherent
records, but concurrent writes remain last-writer-wins rather than a locking
transaction.

Payloads can be any C-contiguous buffer object. Loads return immutable `bytes`.
The maximum payload is mandatory, defaults to 64 KiB, and cannot exceed
16 MiB.

## Build, test, and package

```bash
make check   # native tests, Python syntax, and binding tests
make wheel   # pure-Python wheel under dist/
```

The tests exercise CRC and raw records, C-format compatibility, replacement,
removal, missing/corrupt/oversized records, XDG fallback, absolute paths,
symlink defenses, file modes, library discovery, context management, and
same-instance thread serialization.

## Versioning

The binding and native ABI are versioned independently:

```python
import kilix_state

kilix_state.__version__       # binding version, currently 0.1.0
kilix_state.KILIX_STATE_ABI   # required native ABI line, currently (0, 4)
```

The native 0.4 API has no runtime version-query symbol, so compatibility is
validated by the complete required symbol set. Missing symbols raise
`IncompatibleLibraryError` when the library loads.

## License

MIT. The external `kilix-state` library is also MIT licensed.
