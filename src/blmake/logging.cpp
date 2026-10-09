#include <fstream>
#include <mutex>

#include "blmake/logging.h"

namespace blmake {

namespace {

std::mutex log_mutex;
std::ofstream log_file;

}  // namespace

void setup_logging(const fs::path& in_file) {
    const std::scoped_lock lock(log_mutex);
    log_file.open(in_file, std::ios::trunc);
}

void shutdown_logging() {
    const std::scoped_lock lock(log_mutex);
    log_file.close();
}

void log(std::string_view msg) {
    const std::scoped_lock lock(log_mutex);
    if (log_file.is_open()) {
        log_file << msg << '\n'
                 << std::flush;
    }
}

}  // namespace blmake
