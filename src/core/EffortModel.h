// The day's effort model: components, their timers, targets and percentages.
// Pure C++ with no platform dependencies so it can be unit tested and reused.
#pragma once

#include "Json.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace effort {

// A running component counts wall-clock time from `startedAt` until it is stopped,
// even while the app is closed or the machine is off, and across midnight.
struct Component {
    std::string name;
    std::int64_t accumulatedSeconds = 0;  // time from completed start/stop intervals
    bool running = false;
    std::int64_t startedAt = 0;           // epoch seconds when the current interval began
    std::int64_t targetSeconds = 0;       // 0 means no target
    double percent = 0.0;                 // share of the time available, 0..100
    bool notified = false;                // target-reached notification already delivered today

    std::int64_t elapsedSeconds(std::int64_t now) const;
    bool hasTarget() const { return targetSeconds > 0; }
    bool targetReached(std::int64_t now) const;
    // Target minus elapsed; negative once the target is exceeded. Meaningful only with a target.
    std::int64_t remainingToTargetSeconds(std::int64_t now) const { return targetSeconds - elapsedSeconds(now); }
    // Elapsed as a percentage of the target (can exceed 100); 0 when there is no target.
    double percentDone(std::int64_t now) const;
};

class EffortModel {
public:
    static constexpr int kDefaultStartOfDayMinutes = 9 * 60;  // 9 AM
    static constexpr int kDefaultEndOfDayMinutes = 22 * 60;   // 10 PM
    static constexpr const char* kDefaultEndDate = "2099-01-01";

    // Own Research / Funded Project / Coursework / Leisure with 43/30/12/15 % of the
    // default study day (9 AM to 10 PM, 13 hours).
    static EffortModel withDefaults(const std::string& date);

    const std::string& date() const { return date_; }
    std::int64_t savedAt() const { return savedAt_; }
    // Study time per day: from the start to the end of the study day, never negative.
    std::int64_t totalAvailableSeconds() const;
    // When true (default), starting one component stops whichever one is running.
    bool exclusiveTimers() const { return exclusive_; }
    void setExclusiveTimers(bool exclusive) { exclusive_ = exclusive; }

    // Start and end of the study day as minutes since local midnight (default 9:00 and 22:00).
    int startOfDayMinutes() const { return startOfDayMinutes_; }
    void setStartOfDayMinutes(int minutes);
    int endOfDayMinutes() const { return endOfDayMinutes_; }
    void setEndOfDayMinutes(int minutes);
    // Time left until the end of the study day, never negative.
    std::int64_t remainingSecondsToday(std::int64_t now) const;
    // The component's percentage share of the time left today.
    std::int64_t remainingSecondsFor(std::size_t i, std::int64_t now) const;

    // Last day of the study period as "YYYY-MM-DD" (default 2099-01-01).
    const std::string& endDate() const { return endDate_; }
    bool setEndDate(const std::string& yyyymmdd);  // false and unchanged if not a valid date
    // Whole days from today up to the end date: today counts, the end day does not,
    // so it is 0 on the end day and stays 0 afterwards.
    std::int64_t daysLeft(const std::string& today) const;

    const std::vector<Component>& components() const { return comps_; }
    std::size_t size() const { return comps_.size(); }
    const Component& at(std::size_t i) const { return comps_.at(i); }

    bool start(std::size_t i, std::int64_t now);
    bool stop(std::size_t i, std::int64_t now);
    void stopAll(std::int64_t now);
    std::optional<std::size_t> runningIndex() const;

    bool add(const std::string& name);          // false if blank or duplicate
    bool remove(std::size_t i);
    bool rename(std::size_t i, const std::string& name);
    bool setTarget(std::size_t i, std::int64_t seconds);
    bool setPercent(std::size_t i, double percent);
    void computeTargetsFromPercents();          // target = available * percent / 100, whole minutes
    double percentSum() const;
    std::int64_t totalElapsedSeconds(std::int64_t now) const;

    // Start a new day from the defaults: the default components, percentages and
    // targets and the default study times. Elapsed time, a running timer and the
    // notified flag carry over to default components with the same name; other
    // components are dropped. The report date and the exclusive-timers setting stay.
    void resetDay(const std::string& newDate);
    // Set every component's elapsed time to zero and stop all timers.
    void resetElapsed();
    // Marks and returns components whose target was reached and not yet announced.
    std::vector<std::size_t> collectNewlyReached(std::int64_t now);

    Json toJson(std::int64_t now) const;
    static std::optional<EffortModel> fromJson(const Json& json);

private:
    std::string date_;
    std::int64_t savedAt_ = 0;
    bool exclusive_ = true;
    int startOfDayMinutes_ = kDefaultStartOfDayMinutes;
    int endOfDayMinutes_ = kDefaultEndOfDayMinutes;
    std::string endDate_ = kDefaultEndDate;
    std::vector<Component> comps_;
};

std::string trimmed(const std::string& text);

}  // namespace effort
