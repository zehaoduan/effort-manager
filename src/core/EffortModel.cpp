#include "EffortModel.h"

#include "TimeFormat.h"

#include <algorithm>
#include <cmath>

namespace effort {

std::string trimmed(const std::string& text) {
    const char* whitespace = " \t\r\n";
    const std::size_t begin = text.find_first_not_of(whitespace);
    if (begin == std::string::npos) return std::string();
    const std::size_t end = text.find_last_not_of(whitespace);
    return text.substr(begin, end - begin + 1);
}

std::int64_t Component::elapsedSeconds(std::int64_t now) const {
    std::int64_t total = accumulatedSeconds;
    if (running && now > startedAt) total += now - startedAt;
    return total;
}

bool Component::targetReached(std::int64_t now) const {
    return hasTarget() && elapsedSeconds(now) >= targetSeconds;
}

double Component::percentDone(std::int64_t now) const {
    if (!hasTarget()) return 0.0;
    return 100.0 * static_cast<double>(elapsedSeconds(now)) / static_cast<double>(targetSeconds);
}

namespace {

Component makeComponent(std::string name, double percent) {
    Component c;
    c.name = std::move(name);
    c.percent = percent;
    return c;
}

std::int64_t roundToMinute(double seconds) {
    return static_cast<std::int64_t>(std::llround(seconds / 60.0)) * 60;
}

}  // namespace

EffortModel EffortModel::withDefaults(const std::string& date) {
    EffortModel m;
    m.date_ = date;
    m.comps_.push_back(makeComponent("Own Research", 43.0));
    m.comps_.push_back(makeComponent("Funded Project", 30.0));
    m.comps_.push_back(makeComponent("Coursework", 12.0));
    m.comps_.push_back(makeComponent("Leisure", 15.0));
    m.computeTargetsFromPercents();
    return m;
}

std::int64_t EffortModel::totalAvailableSeconds() const {
    return std::max(0, endOfDayMinutes_ - startOfDayMinutes_) * std::int64_t{60};
}

void EffortModel::setStartOfDayMinutes(int minutes) {
    startOfDayMinutes_ = std::clamp(minutes, 0, 24 * 60 - 1);
}

void EffortModel::setEndOfDayMinutes(int minutes) {
    endOfDayMinutes_ = std::clamp(minutes, 0, 24 * 60 - 1);
}

std::int64_t EffortModel::remainingSecondsToday(std::int64_t now) const {
    const std::int64_t end = static_cast<std::int64_t>(endOfDayMinutes_) * 60;
    return std::max<std::int64_t>(0, end - localSecondsSinceMidnight(now));
}

std::int64_t EffortModel::remainingSecondsFor(std::size_t i, std::int64_t now) const {
    if (i >= comps_.size()) return 0;
    const double share = static_cast<double>(remainingSecondsToday(now)) * comps_[i].percent / 100.0;
    return static_cast<std::int64_t>(std::llround(share));
}

bool EffortModel::setEndDate(const std::string& yyyymmdd) {
    if (!dayNumberFromDate(yyyymmdd)) return false;
    endDate_ = yyyymmdd;
    return true;
}

std::int64_t EffortModel::daysLeft(const std::string& today) const {
    const std::optional<std::int64_t> end = dayNumberFromDate(endDate_);
    const std::optional<std::int64_t> now = dayNumberFromDate(today);
    if (!end || !now) return 0;
    return std::max<std::int64_t>(0, *end - *now);
}

bool EffortModel::start(std::size_t i, std::int64_t now) {
    if (i >= comps_.size()) return false;
    if (comps_[i].running) return true;
    if (exclusive_) stopAll(now);
    comps_[i].running = true;
    comps_[i].startedAt = now;
    return true;
}

bool EffortModel::stop(std::size_t i, std::int64_t now) {
    if (i >= comps_.size()) return false;
    Component& c = comps_[i];
    if (!c.running) return true;
    c.accumulatedSeconds = c.elapsedSeconds(now);
    c.running = false;
    c.startedAt = 0;
    return true;
}

void EffortModel::stopAll(std::int64_t now) {
    for (std::size_t i = 0; i < comps_.size(); ++i) stop(i, now);
}

std::optional<std::size_t> EffortModel::runningIndex() const {
    for (std::size_t i = 0; i < comps_.size(); ++i) {
        if (comps_[i].running) return i;
    }
    return std::nullopt;
}

bool EffortModel::add(const std::string& rawName) {
    const std::string name = trimmed(rawName);
    if (name.empty()) return false;
    for (const Component& c : comps_) {
        if (c.name == name) return false;
    }
    comps_.push_back(makeComponent(name, 0.0));
    return true;
}

bool EffortModel::remove(std::size_t i) {
    if (i >= comps_.size()) return false;
    comps_.erase(comps_.begin() + static_cast<std::ptrdiff_t>(i));
    return true;
}

bool EffortModel::rename(std::size_t i, const std::string& rawName) {
    if (i >= comps_.size()) return false;
    const std::string name = trimmed(rawName);
    if (name.empty()) return false;
    for (std::size_t k = 0; k < comps_.size(); ++k) {
        if (k != i && comps_[k].name == name) return false;
    }
    comps_[i].name = name;
    return true;
}

bool EffortModel::setTarget(std::size_t i, std::int64_t seconds) {
    if (i >= comps_.size()) return false;
    seconds = std::max<std::int64_t>(0, seconds);
    if (comps_[i].targetSeconds != seconds) {
        comps_[i].targetSeconds = seconds;
        comps_[i].notified = false;
    }
    return true;
}

bool EffortModel::setPercent(std::size_t i, double percent) {
    if (i >= comps_.size() || !std::isfinite(percent)) return false;
    comps_[i].percent = std::max(0.0, percent);
    return true;
}

void EffortModel::computeTargetsFromPercents() {
    for (std::size_t i = 0; i < comps_.size(); ++i) {
        setTarget(i, roundToMinute(static_cast<double>(totalAvailableSeconds()) * comps_[i].percent / 100.0));
    }
}

double EffortModel::percentSum() const {
    double sum = 0.0;
    for (const Component& c : comps_) sum += c.percent;
    return sum;
}

std::int64_t EffortModel::totalElapsedSeconds(std::int64_t now) const {
    std::int64_t total = 0;
    for (const Component& c : comps_) total += c.elapsedSeconds(now);
    return total;
}

void EffortModel::resetDay(const std::string& newDate) {
    EffortModel fresh = withDefaults(newDate);
    fresh.endDate_ = endDate_;
    fresh.exclusive_ = exclusive_;
    for (Component& d : fresh.comps_) {
        for (const Component& c : comps_) {
            if (c.name != d.name) continue;
            d.accumulatedSeconds = c.accumulatedSeconds;
            d.running = c.running;
            d.startedAt = c.startedAt;
            d.notified = c.notified;
            break;
        }
    }
    *this = std::move(fresh);
}

void EffortModel::resetElapsed() {
    for (Component& c : comps_) {
        c.accumulatedSeconds = 0;
        c.running = false;
        c.startedAt = 0;
        c.notified = false;
    }
}

std::vector<std::size_t> EffortModel::collectNewlyReached(std::int64_t now) {
    std::vector<std::size_t> reached;
    for (std::size_t i = 0; i < comps_.size(); ++i) {
        Component& c = comps_[i];
        if (!c.notified && c.targetReached(now)) {
            c.notified = true;
            reached.push_back(i);
        }
    }
    return reached;
}

Json EffortModel::toJson(std::int64_t now) const {
    Json components = Json::array();
    for (const Component& c : comps_) {
        Json j = Json::object();
        j.set("name", c.name);
        j.set("accumulatedSeconds", c.accumulatedSeconds);
        j.set("running", c.running);
        j.set("startedAt", c.startedAt);
        j.set("targetSeconds", c.targetSeconds);
        j.set("percent", c.percent);
        j.set("notified", c.notified);
        components.push(std::move(j));
    }
    Json root = Json::object();
    root.set("version", 1);
    root.set("date", date_);
    root.set("savedAt", now);
    root.set("exclusiveTimers", exclusive_);
    root.set("startOfDayMinutes", startOfDayMinutes_);
    root.set("endOfDayMinutes", endOfDayMinutes_);
    root.set("endDate", endDate_);
    root.set("components", std::move(components));
    return root;
}

std::optional<EffortModel> EffortModel::fromJson(const Json& json) {
    if (!json.isObject() || !json.get("components").isArray()) return std::nullopt;
    EffortModel m;
    m.date_ = json.get("date").toString();
    m.savedAt_ = json.get("savedAt").toInt();
    m.exclusive_ = json.get("exclusiveTimers").toBool(true);
    m.setStartOfDayMinutes(static_cast<int>(json.get("startOfDayMinutes").toInt(kDefaultStartOfDayMinutes)));
    m.setEndOfDayMinutes(static_cast<int>(json.get("endOfDayMinutes").toInt(kDefaultEndOfDayMinutes)));
    m.setEndDate(json.get("endDate").toString());  // invalid or missing keeps the default
    for (const Json& j : json.get("components").elements()) {
        if (!j.isObject()) continue;
        Component c;
        c.name = trimmed(j.get("name").toString());
        if (c.name.empty()) continue;
        c.accumulatedSeconds = std::max<std::int64_t>(0, j.get("accumulatedSeconds").toInt());
        c.running = j.get("running").toBool();
        c.startedAt = c.running ? j.get("startedAt").toInt() : 0;
        c.targetSeconds = std::max<std::int64_t>(0, j.get("targetSeconds").toInt());
        c.percent = std::max(0.0, j.get("percent").toNumber());
        c.notified = j.get("notified").toBool();
        m.comps_.push_back(std::move(c));
    }
    return m;
}

}  // namespace effort
