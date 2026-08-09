# Changelog

## Unreleased

- Avoid redundant copies for immutable Python payloads and extract loaded
  bytes without materializing a list of Python integers.
- Load common records with one native call while retaining bounded retries for
  concurrently replaced or larger records.
- Clarify that the binding requires the API introduced in native 0.4 and
  accepts compatible later releases.

## 0.1.0 - 2026-07-22

- Add a complete typed `ctypes` binding for the kilix-state 0.4 API.
- Add Pythonic store lifecycle, bytes loading, buffer saves, context management,
  missing defaults, durable removal, and result-specific exceptions.
- Add authoritative native-library overrides and conventional source/system
  discovery.
- Add native-backed tests for record compatibility, bounds, corruption,
  symlink defenses, path modes, XDG behavior, and thread serialization.
- Add packaging metadata, CI, documentation, and an example.
