#include "kilix_state.h"
#include "kilix_state_codec.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MEBIBYTE (1024u * 1024u)
#define CRC_TOTAL_BYTES (64u * MEBIBYTE)
#define CODEC_VALUES 512u
#define CODEC_ROUNDS 20000u

static double monotonic_seconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1.0;
    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

static bool benchmark_crc(const unsigned char *payload, size_t payload_size)
{
    const size_t iterations = CRC_TOTAL_BYTES / payload_size;
    uint32_t checksum = 0u;
    double started;
    double elapsed;

    started = monotonic_seconds();
    if (started < 0.0) return false;
    for (size_t iteration = 0u; iteration < iterations; ++iteration)
        checksum ^= kilixstate_crc32(payload, payload_size);
    elapsed = monotonic_seconds() - started;
    if (elapsed <= 0.0) return false;

    (void)printf(
        "crc bytes=%zu iterations=%zu throughput_mib_s=%.2f "
        "nanoseconds_per_call=%.1f checksum=%08" PRIx32 "\n",
        payload_size, iterations,
        ((double)payload_size * (double)iterations / (double)MEBIBYTE) /
            elapsed,
        elapsed * 1000000000.0 / (double)iterations, checksum);
    return true;
}

static bool benchmark_codec(void)
{
    unsigned char payload[CODEC_VALUES * sizeof(uint64_t)];
    uint64_t checksum = 0u;
    double started;
    double elapsed;

    started = monotonic_seconds();
    if (started < 0.0) return false;
    for (size_t round = 0u; round < CODEC_ROUNDS; ++round) {
        kilixstate_writer writer;
        kilixstate_reader reader;

        kilixstate_writer_init(&writer, payload, sizeof payload);
        for (uint64_t value = 0u; value < CODEC_VALUES; ++value)
            if (!kilixstate_write_u64(&writer, value + (uint64_t)round))
                return false;
        kilixstate_reader_init(&reader, payload, sizeof payload);
        for (size_t index = 0u; index < CODEC_VALUES; ++index) {
            uint64_t value;
            if (!kilixstate_read_u64(&reader, &value)) return false;
            checksum ^= value;
        }
    }
    elapsed = monotonic_seconds() - started;
    if (elapsed <= 0.0) return false;

    (void)printf(
        "codec_u64 values=%u rounds=%u million_values_s=%.2f "
        "checksum=%016" PRIx64 "\n",
        CODEC_VALUES, CODEC_ROUNDS,
        ((double)CODEC_VALUES * (double)CODEC_ROUNDS / 1000000.0) /
            elapsed,
        checksum);
    return true;
}

static bool benchmark_store(kilixstate_store *store,
                            const unsigned char *payload,
                            unsigned char *loaded, size_t payload_size,
                            size_t save_iterations, size_t load_iterations)
{
    double started;
    double save_elapsed;
    double load_elapsed;

    started = monotonic_seconds();
    if (started < 0.0) return false;
    for (size_t iteration = 0u; iteration < save_iterations; ++iteration)
        if (kilixstate_save(store, payload, payload_size) != KILIXSTATE_OK)
            return false;
    save_elapsed = monotonic_seconds() - started;
    if (save_elapsed <= 0.0) return false;

    started = monotonic_seconds();
    if (started < 0.0) return false;
    for (size_t iteration = 0u; iteration < load_iterations; ++iteration) {
        size_t loaded_size = 0u;
        if (kilixstate_load(store, loaded, payload_size, &loaded_size) !=
                KILIXSTATE_OK ||
            loaded_size != payload_size ||
            memcmp(loaded, payload, payload_size) != 0)
            return false;
    }
    load_elapsed = monotonic_seconds() - started;
    if (load_elapsed <= 0.0) return false;

    (void)printf(
        "store bytes=%zu save_iterations=%zu save_mean_us=%.1f "
        "load_iterations=%zu load_mean_us=%.1f\n",
        payload_size, save_iterations,
        save_elapsed * 1000000.0 / (double)save_iterations,
        load_iterations,
        load_elapsed * 1000000.0 / (double)load_iterations);
    return true;
}

static bool benchmark_store_init(const kilixstate_options *options,
                                 size_t iterations)
{
    double started = monotonic_seconds();
    double elapsed;

    if (started < 0.0) return false;
    for (size_t iteration = 0u; iteration < iterations; ++iteration) {
        kilixstate_store store;
        if (kilixstate_store_init(&store, options) != KILIXSTATE_OK)
            return false;
        kilixstate_store_close(&store);
    }
    elapsed = monotonic_seconds() - started;
    if (elapsed <= 0.0) return false;
    (void)printf(
        "store_init iterations=%zu mean_us=%.2f\n",
        iterations, elapsed * 1000000.0 / (double)iterations);
    return true;
}

int main(void)
{
    char root_template[] = "/tmp/kilix-state-benchmark-XXXXXX";
    char *root;
    unsigned char *payload;
    unsigned char *loaded;
    kilixstate_options options;
    kilixstate_store store;
    bool succeeded = false;

    payload = malloc(MEBIBYTE);
    loaded = malloc(MEBIBYTE);
    if (payload == NULL || loaded == NULL) goto cleanup_buffers;
    for (size_t index = 0u; index < MEBIBYTE; ++index)
        payload[index] = (unsigned char)(index & 0xffu);

    root = mkdtemp(root_template);
    if (root == NULL) goto cleanup_buffers;
    kilixstate_options_init(&options);
    options.app_id = "benchmark";
    options.filename = "state";
    options.base_directory = root;
    options.max_payload = MEBIBYTE;
    if (kilixstate_store_init(&store, &options) != KILIXSTATE_OK)
        goto cleanup_root;

    if (!benchmark_store_init(&options, 5000u) ||
        !benchmark_crc(payload, 64u) ||
        !benchmark_crc(payload, 4096u) ||
        !benchmark_crc(payload, 64u * 1024u) ||
        !benchmark_crc(payload, MEBIBYTE) ||
        !benchmark_codec() ||
        !benchmark_store(&store, payload, loaded, 256u, 40u, 2000u) ||
        !benchmark_store(&store, payload, loaded, 64u * 1024u, 20u, 500u))
        goto cleanup_store;
    succeeded = true;

cleanup_store:
    (void)kilixstate_remove(&store);
    kilixstate_store_close(&store);
    (void)rmdir(store.directory_path);
cleanup_root:
    (void)rmdir(root_template);
cleanup_buffers:
    free(loaded);
    free(payload);
    return succeeded ? EXIT_SUCCESS : EXIT_FAILURE;
}
