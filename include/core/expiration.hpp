#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include "core/database.hpp"

namespace rundb::core {

/**
 * @brief Proprietary Adaptive Probabilistic Active Key Expiration.
 *
 * DESIGN & WHY IT WORKS:
 * 1. Probabilistic Sampling (20 keys per tick):
 *    - Checking all million keys sequentially would freeze the event loop.
 *    - Instead, RunDB randomly samples 20 keys with TTL from each database partition.
 * 2. Adaptive Repeat Loop (> 25% expired threshold):
 *    - If > 25% (5 keys) of the sample are expired, statistical probability dictates
 *      that a large fraction of keyspace is expired. RunDB resamples and clears again immediately.
 * 3. 1-Millisecond CPU Time Cap:
 *    - The repeat loop is strictly limited to 1ms per event loop tick to preserve ultra-low p99 latency.
 */
class Expiration {
public:
    static constexpr size_t ACTIVE_EXPIRE_SAMPLE_COUNT = 20;
    static constexpr double ACTIVE_EXPIRE_THRESHOLD = 0.25;

    /**
     * @brief Performs active expiration cycle across all database partitions.
     * @return Total number of expired keys removed.
     */
    static size_t run_active_cycle(std::vector<Database>& databases);

private:
    static size_t expire_samples_in_db(Database& db);
};

} // namespace rundb::core
