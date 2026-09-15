#include "Store.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace effort {

namespace {

void setError(std::string* error, const std::string& message) {
    if (error) *error = message;
}

}  // namespace

std::filesystem::path Store::defaultDirectory() {
    const char* home = std::getenv("HOME");
    std::filesystem::path base = (home && *home) ? std::filesystem::path(home)
                                                 : std::filesystem::temp_directory_path();
    return base / "Library" / "Application Support" / "EffortManager";
}

std::filesystem::path Store::statePath(const std::filesystem::path& directory) {
    return directory / "state.json";
}

bool Store::save(const EffortModel& model, const std::filesystem::path& file,
                 std::int64_t now, std::string* error) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    if (ec) {
        setError(error, "cannot create " + file.parent_path().string() + ": " + ec.message());
        return false;
    }
    std::filesystem::path temp = file;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out) {
            setError(error, "cannot write " + temp.string());
            return false;
        }
        out << model.toJson(now).dump(2) << '\n';
        if (!out) {
            setError(error, "write failed for " + temp.string());
            return false;
        }
    }
    std::filesystem::rename(temp, file, ec);
    if (ec) {
        setError(error, "cannot replace " + file.string() + ": " + ec.message());
        return false;
    }
    return true;
}

std::optional<EffortModel> Store::load(const std::filesystem::path& file, std::string* error) {
    std::ifstream in(file);
    if (!in) {
        setError(error, "cannot open " + file.string());
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string parseError;
    const std::optional<Json> json = Json::parse(buffer.str(), &parseError);
    if (!json) {
        setError(error, "invalid JSON in " + file.string() + ": " + parseError);
        return std::nullopt;
    }
    std::optional<EffortModel> model = EffortModel::fromJson(*json);
    if (!model) setError(error, "unexpected structure in " + file.string());
    return model;
}

}  // namespace effort
