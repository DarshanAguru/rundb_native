#include "server/shutdown.hpp"
#include <csignal>

namespace rundb::server {

static void signal_handler(int sig) {
    Shutdown::request_shutdown(sig);
}

void Shutdown::setup_signals() {
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGHUP, SIG_IGN);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
}

bool Shutdown::is_shutdown_requested() noexcept {
    return s_shutdown_requested.load(std::memory_order_relaxed);
}

void Shutdown::request_shutdown(int sig) noexcept {
    s_signal_received.store(sig, std::memory_order_relaxed);
    s_engine_status.store(EngineStatus::Shutdown, std::memory_order_relaxed);
    s_shutdown_requested.store(true, std::memory_order_release);
}

void Shutdown::reset() noexcept {
    s_signal_received.store(0, std::memory_order_relaxed);
    s_engine_status.store(EngineStatus::Idle, std::memory_order_relaxed);
    s_shutdown_requested.store(false, std::memory_order_release);
}

int Shutdown::get_signal() noexcept {
    return s_signal_received.load(std::memory_order_relaxed);
}

void Shutdown::set_engine_status(EngineStatus status) noexcept {
    s_engine_status.store(status, std::memory_order_relaxed);
}

EngineStatus Shutdown::get_engine_status() noexcept {
    return s_engine_status.load(std::memory_order_relaxed);
}

} // namespace rundb::server
