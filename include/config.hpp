#pragma once 

#include "argparse.hpp"

namespace rundb {

    /**
     * @brief Manages configuration loading, file parsing, and environment synchronization.
     *
     * Precedence: CLI Arguments > Environment Variables > Config File > Defaults
     */
    class Config {
        public:
            /**
             * @brief Resolves configuration by merging defaults, config file, env vars, and CLI flags.
             * @param opts Parsed CLI arguments (will be populated with resolved config).
             */
            static void load(Args& opts);

            /**
             * @brief Reads a key=value or key value formatted configuration file.
             * @param path File system path to the .conf file.
             * @param opts Target options to populate.
             * @param required If true, throws runtime_error if the file cannot be opened.
             */
            static void load_from_file(const char* path, Args& opts, bool required = false);
    };

} // namespace rundb