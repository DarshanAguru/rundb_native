#pragma once

#include <atomic>
#include <csignal>

namespace rundb::server {

enum class EngineStatus : int {
    Idle = 0,
    Busy = 1,
    Shutdown = 2
};

/**
 * @brief Thread-safe server shutdown coordinator and POSIX signal handler.
 */
class Shutdown {
public:
    static void setup_signals();
    static bool is_shutdown_requested() noexcept;
    static void request_shutdown(int sig = 0) noexcept;
    static void reset() noexcept;
    static int get_signal() noexcept;

    static void set_engine_status(EngineStatus status) noexcept;
    static EngineStatus get_engine_status() noexcept;

private:
    static inline std::atomic<bool> s_shutdown_requested{false};
    static inline std::atomic<int> s_signal_received{0};
    static inline std::atomic<EngineStatus> s_engine_status{EngineStatus::Idle};
};

} // namespace rundb::server
