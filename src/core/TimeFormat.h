// Duration parsing/formatting and clock helpers. Pure C++, no platform dependencies.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace effort {

// Accepts "2:30", "2:30:15", "2h30m", "45m", "90s", "1.5h", "2h30" (bare trailing
// number = next smaller unit) and plain numbers, which are read as hours ("2" = 2:00).
// Whitespace and case are ignored. Returns seconds, or nullopt if the text is not a duration.
std::optional<std::int64_t> parseDuration(const std::string& text);

// "H:MM:SS"; negative values are clamped to zero.
std::string formatHMS(std::int64_t seconds);

// "H:MM:SS", or "-H:MM:SS" for negative values.
std::string formatSignedHMS(std::int64_t seconds);

// "H:MM", or "H:MM:SS" when the value is not a whole number of minutes.
std::string formatHM(std::int64_t seconds);

// Local calendar date as "YYYY-MM-DD".
std::string localDateString(std::int64_t epochSeconds);

// Day number (days since 1970-01-01, proleptic Gregorian) of a "YYYY-MM-DD" string,
// or nullopt if the text is not a valid calendar date.
std::optional<std::int64_t> dayNumberFromDate(const std::string& yyyymmdd);

// Local date with weekday as "12/09/2026 Sat" (day/month/year, English abbreviation).
std::string formatDayMonthYearWeekday(std::int64_t epochSeconds);

// Seconds elapsed since local midnight for the given instant.
std::int64_t localSecondsSinceMidnight(std::int64_t epochSeconds);

// Local wall-clock time as "5:36:23 PM" (12-hour, no leading zero).
std::string formatClock12(std::int64_t epochSeconds);

// A time of day given as minutes since midnight, as "6:00 PM".
std::string formatTimeOfDay12(int minutesSinceMidnight);

std::int64_t nowEpochSeconds();

}  // namespace effort
