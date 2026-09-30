#include "syscheck.hpp"
#include "logger.hpp"

#define SYSCHECK "SYSCHECK"

#ifdef __linux__
    #include <sys/epoll.h>
    #include <unistd.h>
#endif

#include <cstdlib>
#include <stdexcept>

namespace rundb {

    bool SysCheck::epoll_ = false;
    bool SysCheck::linux_ = false;

    /**
     * @brief Probes operating system support for epoll_create1(EPOLL_CLOEXEC).
     */
    void SysCheck::init() {
        #ifdef __linux__
            linux_ = true;
            int fd = ::epoll_create1(EPOLL_CLOEXEC);
            if (fd >= 0) {
                epoll_ = true;
                ::close(fd);
            }
        #else
            linux_ = false;
            epoll_ = false;
        #endif

        if (!(linux_ && epoll_)) {
            ERROR(SYSCHECK, "RunDB requires a Linux environment with epoll support");
            throw std::runtime_error("RunDB platform check failed: epoll is not available");
        }
    }

} // namespace rundb