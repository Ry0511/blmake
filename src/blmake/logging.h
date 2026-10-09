#ifndef BLMAKE_LOGGING_H
#define BLMAKE_LOGGING_H

#include <filesystem>
#include <format>
#include <string_view>

namespace blmake {

namespace fs = std::filesystem;

void setup_logging(const fs::path& in_file);
void shutdown_logging();
void log(std::string_view msg);

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define BLMAKE_LOG(...) (::blmake::log(std::format(__VA_ARGS__)))

}  // namespace blmake

#endif  // BLMAKE_LOGGING_H
