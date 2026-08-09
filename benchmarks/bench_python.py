"""Manual Python binding benchmarks; not a pass/fail CI test."""

from __future__ import annotations

import tempfile
import time

import kilix_state as ks


MEBIBYTE = 1024 * 1024
CRC_TOTAL_BYTES = 32 * MEBIBYTE


def benchmark_crc(payload: bytes) -> None:
    iterations = CRC_TOTAL_BYTES // len(payload)
    checksum = 0
    started = time.perf_counter()
    for _ in range(iterations):
        checksum ^= ks.crc32(payload)
    elapsed = time.perf_counter() - started
    throughput = len(payload) * iterations / MEBIBYTE / elapsed
    nanoseconds = elapsed * 1_000_000_000 / iterations
    print(
        f"python_crc bytes={len(payload)} iterations={iterations} "
        f"throughput_mib_s={throughput:.2f} "
        f"nanoseconds_per_call={nanoseconds:.1f} "
        f"checksum={checksum:08x}"
    )


def benchmark_store_operation(
    store: ks.Store,
    payload: bytes,
    *,
    save_iterations: int,
    load_iterations: int,
) -> None:
    started = time.perf_counter()
    for _ in range(save_iterations):
        store.save(payload)
    save_elapsed = time.perf_counter() - started

    started = time.perf_counter()
    for _ in range(load_iterations):
        if store.load() != payload:
            raise RuntimeError("loaded payload did not match benchmark input")
    load_elapsed = time.perf_counter() - started

    print(
        f"python_store bytes={len(payload)} "
        f"save_iterations={save_iterations} "
        f"save_mean_us={save_elapsed * 1_000_000 / save_iterations:.1f} "
        f"load_iterations={load_iterations} "
        f"load_mean_us={load_elapsed * 1_000_000 / load_iterations:.1f}"
    )


def main() -> None:
    payload = bytes(range(256)) * (MEBIBYTE // 256)
    for size in (64, 4096, 64 * 1024, MEBIBYTE):
        benchmark_crc(payload[:size])

    with tempfile.TemporaryDirectory(
        prefix="kilix-state-python-benchmark-"
    ) as root:
        with ks.Store(
            "benchmark",
            "state",
            base_directory=root,
            max_payload=MEBIBYTE,
        ) as store:
            benchmark_store_operation(
                store,
                payload[:256],
                save_iterations=40,
                load_iterations=2000,
            )
            benchmark_store_operation(
                store,
                payload[: 64 * 1024],
                save_iterations=20,
                load_iterations=500,
            )


if __name__ == "__main__":
    main()
