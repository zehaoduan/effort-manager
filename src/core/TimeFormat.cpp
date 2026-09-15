#include "TimeFormat.h"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>

namespace effort {

namespace {

std::string normalized(const std::string& raw) {
    std::string s;
    for (unsigned char c : raw) {
        if (std::isspace(c)) continue;
        s += static_cast<char>(std::tolower(c));
    }
    return s;
}

bool parseNonNegativeInt(const std::string& token, std::int64_t& out) {
    if (token.empty()) return false;
    for (unsigned char c : token) {
        if (!std::isdigit(c)) return false;
    }
    out = std::strtoll(token.c_str(), nullptr, 10);
    return true;
}

// Reads an unsigned decimal number starting at s[i] and advances i past it.
bool readNumber(const std::string& s, std::size_t& i, double& out) {
    const std::size_t start = i;
    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) ++i;
    if (i == start) return false;
    const std::string token = s.substr(start, i - start);
    if (token == ".") return false;
    char* end = nullptr;
    out = std::strtod(token.c_str(), &end);
    return end == token.c_str() + token.size();
}

std::optional<std::int64_t> parseClockForm(const std::string& s) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t colon = s.find(':', start);
        parts.push_back(s.substr(start, colon == std::string::npos ? std::string::npos : colon - start));
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    if (parts.size() < 2 || parts.size() > 3) return std::nullopt;
    std::int64_t hours = 0, minutes = 0, seconds = 0;
    if (!parseNonNegativeInt(parts[0], hours) || !parseNonNegativeInt(parts[1], minutes)) return std::nullopt;
    if (parts.size() == 3 && !parseNonNegativeInt(parts[2], seconds)) return std::nullopt;
    if (minutes >= 60 || seconds >= 60) return std::nullopt;
    return hours * 3600 + minutes * 60 + seconds;
}

std::optional<std::int64_t> parseUnitForm(const std::string& s) {
    double total = 0.0;
    std::size_t i = 0;
    char lastUnit = 0;
    while (i < s.size()) {
        double value = 0.0;
        if (!readNumber(s, i, value)) return std::nullopt;
        if (i >= s.size()) {
            // Bare trailing number: "2h30" means 2h 30m, "5m30" means 5m 30s.
            const double multiplier = lastUnit == 'h' ? 60.0 : lastUnit == 'm' ? 1.0 : 3600.0;
            total += value * multiplier;
            break;
        }
        const char unit = s[i++];
        if (unit == 'h') total += value * 3600.0;
        else if (unit == 'm') total += value * 60.0;
        else if (unit == 's') total += value;
        else return std::nullopt;
        // Skip the rest of a spelled-out unit: "hrs", "hours", "min", "mins", "sec".
        while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) ++i;
        lastUnit = unit;
    }
    return static_cast<std::int64_t>(std::llround(total));
}

}  // namespace

std::optional<std::int64_t> parseDuration(const std::string& text) {
    const std::string s = normalized(text);
    if (s.empty()) return std::nullopt;
    if (s.find(':') != std::string::npos) return parseClockForm(s);
    if (s.find_first_of("hms") != std::string::npos) return parseUnitForm(s);

    // Plain number: hours.
    std::size_t i = 0;
    double hours = 0.0;
    if (!readNumber(s, i, hours) || i != s.size()) return std::nullopt;
    return static_cast<std::int64_t>(std::llround(hours * 3600.0));
}

std::string formatHMS(std::int64_t seconds) {
    if (seconds < 0) seconds = 0;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld",
                  static_cast<long long>(seconds / 3600),
                  static_cast<long long>((seconds % 3600) / 60),
                  static_cast<long long>(seconds % 60));
    return buf;
}

std::string formatSignedHMS(std::int64_t seconds) {
    if (seconds < 0) return "-" + formatHMS(-seconds);
    return formatHMS(seconds);
}

std::string formatHM(std::int64_t seconds) {
    if (seconds < 0) seconds = 0;
    if (seconds % 60 != 0) return formatHMS(seconds);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%lld:%02lld",
                  static_cast<long long>(seconds / 3600),
                  static_cast<long long>((seconds % 3600) / 60));
    return buf;
}

std::string localDateString(std::int64_t epochSeconds) {
    const std::time_t t = static_cast<std::time_t>(epochSeconds);
    std::tm local{};
    localtime_r(&t, &local);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &local);
    return buf;
}

namespace {

bool isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int daysInMonth(int year, int month) {
    static const int lengths[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && isLeapYear(year)) return 29;
    return lengths[month - 1];
}

// Howard Hinnant's days_from_civil: days since 1970-01-01 for a Gregorian date.
std::int64_t daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const std::int64_t yoe = y - era * 400;
    const std::int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

std::tm localTime(std::int64_t epochSeconds) {
    const std::time_t t = static_cast<std::time_t>(epochSeconds);
    std::tm local{};
    localtime_r(&t, &local);
    return local;
}

std::string clock12(int hour, int minute, int second, bool withSeconds) {
    const char* period = hour < 12 ? "AM" : "PM";
    int hour12 = hour % 12;
    if (hour12 == 0) hour12 = 12;
    char buf[32];
    if (withSeconds) {
        std::snprintf(buf, sizeof buf, "%d:%02d:%02d %s", hour12, minute, second, period);
    } else {
        std::snprintf(buf, sizeof buf, "%d:%02d %s", hour12, minute, period);
    }
    return buf;
}

}  // namespace

std::optional<std::int64_t> dayNumberFromDate(const std::string& yyyymmdd) {
    if (yyyymmdd.size() != 10 || yyyymmdd[4] != '-' || yyyymmdd[7] != '-') return std::nullopt;
    for (std::size_t i = 0; i < yyyymmdd.size(); ++i) {
        if (i == 4 || i == 7) continue;
        if (!std::isdigit(static_cast<unsigned char>(yyyymmdd[i]))) return std::nullopt;
    }
    const int year = std::atoi(yyyymmdd.substr(0, 4).c_str());
    const int month = std::atoi(yyyymmdd.substr(5, 2).c_str());
    const int day = std::atoi(yyyymmdd.substr(8, 2).c_str());
    if (month < 1 || month > 12 || day < 1 || day > daysInMonth(year, month)) return std::nullopt;
    return daysFromCivil(year, month, day);
}

std::string formatDayMonthYearWeekday(std::int64_t epochSeconds) {
    static const char* const weekdays[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const std::tm local = localTime(epochSeconds);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02d/%02d/%04d %s", local.tm_mday, local.tm_mon + 1,
                  local.tm_year + 1900, weekdays[local.tm_wday % 7]);
    return buf;
}

std::int64_t localSecondsSinceMidnight(std::int64_t epochSeconds) {
    const std::tm local = localTime(epochSeconds);
    return local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
}

std::string formatClock12(std::int64_t epochSeconds) {
    const std::tm local = localTime(epochSeconds);
    return clock12(local.tm_hour, local.tm_min, local.tm_sec, true);
}

std::string formatTimeOfDay12(int minutesSinceMidnight) {
    if (minutesSinceMidnight < 0) minutesSinceMidnight = 0;
    minutesSinceMidnight %= 24 * 60;
    return clock12(minutesSinceMidnight / 60, minutesSinceMidnight % 60, 0, false);
}

std::int64_t nowEpochSeconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace effort
