#pragma once 

namespace rundb {

    /**
     * @brief Performs pre-flight system platform and kernel capability checks.
     *
     * Validates that RunDB is executing in a compatible Linux environment with
     * functional epoll support before initializing the network layer.
     */
    class SysCheck {
        public:
            /**
             * @brief Tests kernel support for epoll_create1 and non-blocking I/O.
             */
            static void init();

        private:
            static bool linux_;
            static bool epoll_;
    };

} // namespace rundb