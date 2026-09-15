// On-disk persistence: the live state file.
#pragma once

#include "EffortModel.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace effort {

class Store {
public:
    // ~/Library/Application Support/EffortManager
    static std::filesystem::path defaultDirectory();
    static std::filesystem::path statePath(const std::filesystem::path& directory);

    // Writes atomically (temp file + rename) so a crash never leaves a half-written state.
    static bool save(const EffortModel& model, const std::filesystem::path& file,
                     std::int64_t now, std::string* error = nullptr);
    static std::optional<EffortModel> load(const std::filesystem::path& file, std::string* error = nullptr);
};

}  // namespace effort
