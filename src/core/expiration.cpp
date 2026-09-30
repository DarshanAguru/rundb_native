#include "core/expiration.hpp"
#include <chrono>

namespace rundb::core {

size_t Expiration::expire_samples_in_db(Database& db) {
    if (db.expires_count() == 0) return 0;

    auto sampled = db.sample_expires(ACTIVE_EXPIRE_SAMPLE_COUNT);
    if (sampled.empty()) return 0;

    size_t expired_count = 0;
    for (const auto& key : sampled) {
        if (db.is_expired(key)) {
            db.del(key);
            expired_count++;
        }
    }
    return expired_count;
}

size_t Expiration::run_active_cycle(std::vector<Database>& databases) {
    auto start_time = std::chrono::steady_clock::now();
    size_t total_expired = 0;

    for (auto& db : databases) {
        if (db.expires_count() == 0) continue;

        size_t samples = std::min(ACTIVE_EXPIRE_SAMPLE_COUNT, db.expires_count());
        if (samples == 0) continue;

        while (true) {
            size_t expired = expire_samples_in_db(db);
            total_expired += expired;

            double ratio = static_cast<double>(expired) / static_cast<double>(samples);
            if (ratio <= ACTIVE_EXPIRE_THRESHOLD) {
                break; // Under 25% expired, move to next DB
            }

            // Time cap: do not exceed 1 millisecond total across all DBs
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start_time
            ).count();
            if (elapsed > 1000) {
                return total_expired; // Yield event loop
            }

            if (db.expires_count() == 0) break;
            samples = std::min(ACTIVE_EXPIRE_SAMPLE_COUNT, db.expires_count());
        }
    }

    return total_expired;
}

} // namespace rundb::core
