// Unit tests for the C++ core. No framework: each CHECK reports its own failure.
#include "EffortModel.h"
#include "Json.h"
#include "Store.h"
#include "TimeFormat.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

using namespace effort;

static int failures = 0;

#define CHECK(condition)                                                              \
    do {                                                                              \
        if (!(condition)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

static bool parsesTo(const std::string& text, std::int64_t seconds) {
    const auto value = parseDuration(text);
    return value && *value == seconds;
}

static void testDurations() {
    CHECK(parsesTo("2:30", 9000));
    CHECK(parsesTo("2:30:15", 9015));
    CHECK(parsesTo("0:05", 300));
    CHECK(parsesTo("2h30m", 9000));
    CHECK(parsesTo("2h 30 min", 9000));
    CHECK(parsesTo("2 hours 30 minutes", 9000));
    CHECK(parsesTo("45m", 2700));
    CHECK(parsesTo("90s", 90));
    CHECK(parsesTo("1.5h", 5400));
    CHECK(parsesTo("2h30", 9000));
    CHECK(parsesTo("5m30", 330));
    CHECK(parsesTo("2", 7200));
    CHECK(parsesTo("0.5", 1800));
    CHECK(parsesTo(" 8 ", 28800));
    CHECK(!parseDuration(""));
    CHECK(!parseDuration("abc"));
    CHECK(!parseDuration("2:60"));
    CHECK(!parseDuration("1:2:3:4"));
    CHECK(!parseDuration("-1"));
    CHECK(!parseDuration("2x"));

    CHECK(formatHMS(0) == "0:00:00");
    CHECK(formatHMS(3661) == "1:01:01");
    CHECK(formatHMS(36000) == "10:00:00");
    CHECK(formatSignedHMS(3661) == "1:01:01");
    CHECK(formatSignedHMS(0) == "0:00:00");
    CHECK(formatSignedHMS(-312) == "-0:05:12");
    CHECK(formatHM(9000) == "2:30");
    CHECK(formatHM(9015) == "2:30:15");
    CHECK(formatHM(-5) == "0:00");
    CHECK(localDateString(0).size() == 10);
}

static void testClockAndRemaining() {
    // Epoch 0 is midnight; the tests run with TZ=UTC so local time is predictable.
    CHECK(formatClock12(0) == "12:00:00 AM");
    CHECK(formatClock12(17 * 3600 + 36 * 60 + 23) == "5:36:23 PM");
    CHECK(formatClock12(12 * 3600) == "12:00:00 PM");
    CHECK(formatClock12(11 * 3600 + 59 * 60 + 59) == "11:59:59 AM");
    CHECK(formatClock12(86400 + 9 * 3600 + 5 * 60 + 7) == "9:05:07 AM");
    CHECK(formatTimeOfDay12(18 * 60) == "6:00 PM");
    CHECK(formatTimeOfDay12(0) == "12:00 AM");
    CHECK(formatTimeOfDay12(12 * 60 + 5) == "12:05 PM");
    CHECK(localSecondsSinceMidnight(3 * 86400 + 3723) == 3723);
    CHECK(formatDayMonthYearWeekday(0) == "01/01/1970 Thu");
    CHECK(formatDayMonthYearWeekday(20708 * 86400 + 3600) == "12/09/2026 Sat");
    CHECK(formatDayMonthYearWeekday(20709 * 86400) == "13/09/2026 Sun");

    EffortModel m = EffortModel::withDefaults("2026-09-12");
    CHECK(m.endOfDayMinutes() == 22 * 60);  // 10 PM by default
    const std::int64_t threePm = 15 * 3600;
    CHECK(m.remainingSecondsToday(threePm) == 7 * 3600);
    m.setEndOfDayMinutes(18 * 60);
    CHECK(m.remainingSecondsToday(threePm) == 3 * 3600);
    CHECK(m.remainingSecondsFor(0, threePm) == 4644);  // 43 %
    CHECK(m.remainingSecondsFor(1, threePm) == 3240);  // 30 %
    CHECK(m.remainingSecondsFor(2, threePm) == 1296);  // 12 %
    CHECK(m.remainingSecondsFor(3, threePm) == 1620);  // 15 %
    CHECK(m.remainingSecondsFor(9, threePm) == 0);
    CHECK(m.remainingSecondsToday(19 * 3600) == 0);    // past the end of the day
    CHECK(m.remainingSecondsFor(0, 19 * 3600) == 0);

    m.setEndOfDayMinutes(-5);
    CHECK(m.endOfDayMinutes() == 0);
    m.setEndOfDayMinutes(5000);
    CHECK(m.endOfDayMinutes() == 24 * 60 - 1);
    m.setEndOfDayMinutes(17 * 60 + 30);
    CHECK(m.remainingSecondsToday(threePm) == 2 * 3600 + 1800);

    // Start and end times survive saving; a day reset restores the defaults; old files
    // get the defaults.
    m.setStartOfDayMinutes(8 * 60 + 15);
    const auto copy = EffortModel::fromJson(*Json::parse(m.toJson(0).dump()));
    CHECK(copy && copy->endOfDayMinutes() == 17 * 60 + 30);
    CHECK(copy && copy->startOfDayMinutes() == 8 * 60 + 15);
    CHECK(copy && copy->totalAvailableSeconds() == 9 * 3600 + 15 * 60);
    m.resetDay("2026-09-13");
    CHECK(m.endOfDayMinutes() == 22 * 60);
    CHECK(m.startOfDayMinutes() == 9 * 60);
    const auto legacy = EffortModel::fromJson(*Json::parse("{\"components\":[]}"));
    CHECK(legacy && legacy->endOfDayMinutes() == 22 * 60);
}

static void testEndDate() {
    CHECK(dayNumberFromDate("1970-01-01") && *dayNumberFromDate("1970-01-01") == 0);
    CHECK(dayNumberFromDate("1970-01-02") && *dayNumberFromDate("1970-01-02") == 1);
    CHECK(dayNumberFromDate("1969-12-31") && *dayNumberFromDate("1969-12-31") == -1);
    CHECK(*dayNumberFromDate("2000-03-01") - *dayNumberFromDate("2000-02-28") == 2);  // leap year
    CHECK(*dayNumberFromDate("2100-03-01") - *dayNumberFromDate("2100-02-28") == 1);  // not a leap year
    CHECK(*dayNumberFromDate("2027-09-12") - *dayNumberFromDate("2026-09-12") == 365);
    CHECK(*dayNumberFromDate("2026-09-12") == 20708);  // 2026-09-12 is 20708 days after the epoch
    CHECK(!dayNumberFromDate(""));
    CHECK(!dayNumberFromDate("2026-9-1"));
    CHECK(!dayNumberFromDate("2026/09/12"));
    CHECK(!dayNumberFromDate("2026-13-01"));
    CHECK(!dayNumberFromDate("2026-02-29"));
    CHECK(!dayNumberFromDate("2026-04-31"));
    CHECK(!dayNumberFromDate("abcd-ef-gh"));

    EffortModel m = EffortModel::withDefaults("2026-09-12");
    CHECK(m.endDate() == "2099-01-01");
    CHECK(m.daysLeft("2026-09-12") == *dayNumberFromDate("2099-01-01") - *dayNumberFromDate("2026-09-12"));
    CHECK(m.setEndDate("2026-09-14"));
    CHECK(m.daysLeft("2026-09-12") == 2);   // today and tomorrow, not the end day
    CHECK(m.daysLeft("2026-09-13") == 1);
    CHECK(m.daysLeft("2026-09-14") == 0);   // on the end day
    CHECK(m.daysLeft("2026-09-15") == 0);   // never negative
    CHECK(!m.setEndDate("2026-02-30"));
    CHECK(!m.setEndDate("tomorrow"));
    CHECK(m.endDate() == "2026-09-14");

    // Survives saving and, unlike the other settings, a day reset; old files get the default.
    m.setExclusiveTimers(false);
    const auto copy = EffortModel::fromJson(*Json::parse(m.toJson(0).dump()));
    CHECK(copy && copy->endDate() == "2026-09-14");
    m.resetDay("2026-09-13");
    CHECK(m.endDate() == "2026-09-14");
    CHECK(!m.exclusiveTimers());
    const auto legacy = EffortModel::fromJson(*Json::parse("{\"components\":[]}"));
    CHECK(legacy && legacy->endDate() == "2099-01-01");
    const auto broken = EffortModel::fromJson(*Json::parse("{\"components\":[],\"endDate\":\"nope\"}"));
    CHECK(broken && broken->endDate() == "2099-01-01");
}

static void testModel() {
    EffortModel m = EffortModel::withDefaults("2026-09-12");
    CHECK(m.size() == 4);
    CHECK(m.at(0).name == "Own Research");
    CHECK(m.at(1).name == "Funded Project");
    CHECK(m.at(2).name == "Coursework");
    CHECK(m.at(3).name == "Leisure");
    CHECK(m.startOfDayMinutes() == 9 * 60 && m.endOfDayMinutes() == 22 * 60);
    CHECK(m.totalAvailableSeconds() == 13 * 3600);  // 9 AM to 10 PM
    CHECK(m.at(0).percent == 43.0 && m.at(0).targetSeconds == 20100);  // 5:35 (5:35:24 rounded to the minute)
    CHECK(m.at(1).percent == 30.0 && m.at(1).targetSeconds == 14040);  // 3:54
    CHECK(m.at(2).percent == 12.0 && m.at(2).targetSeconds == 5640);   // 1:34 (1:33:36 rounded to the minute)
    CHECK(m.at(3).percent == 15.0 && m.at(3).targetSeconds == 7020);   // 1:57
    CHECK(m.percentSum() == 100.0);

    // Time available is end minus start; it never goes negative.
    m.setStartOfDayMinutes(23 * 60);
    CHECK(m.totalAvailableSeconds() == 0);
    m.setStartOfDayMinutes(-10);
    CHECK(m.startOfDayMinutes() == 0);
    m.setStartOfDayMinutes(9999);
    CHECK(m.startOfDayMinutes() == 24 * 60 - 1);

    // Percent-based targets are rounded to whole minutes.
    m.setStartOfDayMinutes(9 * 60);
    m.setEndOfDayMinutes(16 * 60);  // 7 hours
    m.setPercent(0, 33.3);
    m.computeTargetsFromPercents();
    CHECK(m.at(0).targetSeconds % 60 == 0);
    CHECK(m.at(0).targetSeconds == 8400);  // 2h19m50s rounds to 2:20

    // Exclusive timers: starting one stops the other.
    CHECK(m.start(0, 1000));
    CHECK(m.at(0).running);
    CHECK(m.runningIndex() && *m.runningIndex() == 0);
    CHECK(m.start(1, 1600));
    CHECK(!m.at(0).running);
    CHECK(m.at(0).accumulatedSeconds == 600);
    CHECK(m.at(0).elapsedSeconds(2000) == 600);
    CHECK(m.at(1).elapsedSeconds(2000) == 400);
    CHECK(m.stop(1, 2000));
    CHECK(m.at(1).accumulatedSeconds == 400);
    CHECK(!m.runningIndex());
    CHECK(m.totalElapsedSeconds(3000) == 1000);
    CHECK(!m.start(99, 0));

    // Non-exclusive timers can overlap.
    m.setExclusiveTimers(false);
    m.start(0, 3000);
    m.start(1, 3000);
    CHECK(m.at(0).running && m.at(1).running);
    m.stopAll(3100);
    CHECK(m.at(0).accumulatedSeconds == 700 && m.at(1).accumulatedSeconds == 500);
    m.setExclusiveTimers(true);

    // Remaining to target is signed: positive before, negative after.
    m.setTarget(0, 650);
    CHECK(m.at(0).remainingToTargetSeconds(3200) == 650 - 700);
    m.setTarget(0, 1000);
    CHECK(m.at(0).remainingToTargetSeconds(3200) == 300);

    // Percent done is elapsed over target; it can exceed 100 and is 0 without a target.
    CHECK(m.at(0).percentDone(3200) == 70.0);
    m.setTarget(0, 350);
    CHECK(m.at(0).percentDone(3200) == 200.0);
    m.setTarget(0, 0);
    CHECK(m.at(0).percentDone(3200) == 0.0);

    // Reaching a target is reported exactly once.
    m.setTarget(0, 650);
    auto reached = m.collectNewlyReached(3200);
    CHECK(reached.size() == 1 && reached[0] == 0);
    CHECK(m.at(0).notified);
    CHECK(m.collectNewlyReached(3200).empty());
    // Changing the target re-arms the notification.
    m.setTarget(0, 5000);
    CHECK(!m.at(0).notified);
    CHECK(m.collectNewlyReached(3200).empty());
    m.setTarget(0, 700);
    CHECK(m.collectNewlyReached(3200).size() == 1);
    // No target means never reached.
    m.setTarget(2, 0);
    CHECK(!m.at(2).hasTarget());
    CHECK(!m.at(2).targetReached(999999));

    // Add / rename / remove.
    CHECK(m.add("Teaching"));
    CHECK(!m.add("Teaching"));
    CHECK(!m.add("  teaching  ") == false);  // different case is a different name
    CHECK(!m.add("   "));
    CHECK(m.size() == 6);
    CHECK(m.rename(4, "  TA duties "));
    CHECK(m.at(4).name == "TA duties");
    CHECK(!m.rename(4, "Coursework"));
    CHECK(!m.rename(4, ""));
    CHECK(m.remove(5));
    CHECK(m.remove(4));
    CHECK(m.size() == 4);
    CHECK(!m.remove(4));

    // Reset day restores the defaults (components, percentages, targets, study times)
    // but keeps elapsed time and a running timer for default components, by name.
    m.add("Teaching");
    m.start(4, 3500);          // time on a non-default component is dropped with it
    m.start(1, 4000);          // Funded Project running (stops Teaching at 4000)
    const std::int64_t research = m.at(0).elapsedSeconds(4000);
    const std::int64_t funded = m.at(1).accumulatedSeconds;  // earlier intervals, kept too
    CHECK(research > 0);
    m.resetDay("2026-09-13");
    CHECK(m.date() == "2026-09-13");
    CHECK(m.size() == 4);
    CHECK(m.at(0).name == "Own Research" && m.at(0).percent == 43.0 && m.at(0).targetSeconds == 20100);
    CHECK(m.at(0).accumulatedSeconds == research);
    CHECK(m.at(1).running && m.at(1).startedAt == 4000);
    CHECK(m.at(1).elapsedSeconds(4600) == funded + 600);
    CHECK(m.at(3).name == "Leisure" && m.at(3).percent == 15.0 && m.at(3).targetSeconds == 7020);
    CHECK(m.startOfDayMinutes() == 9 * 60 && m.endOfDayMinutes() == 22 * 60);
    CHECK(m.totalAvailableSeconds() == 13 * 3600);
    CHECK(m.exclusiveTimers());

    // Reset elapsed zeroes every component and stops all timers, nothing else.
    m.setTarget(0, 500);
    m.resetElapsed();
    CHECK(m.totalElapsedSeconds(9999) == 0);
    CHECK(!m.runningIndex());
    CHECK(!m.at(0).notified);
    CHECK(m.at(0).targetSeconds == 500 && m.at(0).percent == 43.0);
    CHECK(m.date() == "2026-09-13");
}

static void testJsonRoundTrip() {
    EffortModel m = EffortModel::withDefaults("2026-09-12");
    m.start(0, 1000);
    m.start(1, 1600);  // component 0 now has 600 s, component 1 is running
    m.setTarget(0, 500);
    CHECK(m.collectNewlyReached(1700).size() == 1);

    const std::string text = m.toJson(2000).dump(2);
    std::string error;
    const auto parsed = Json::parse(text, &error);
    CHECK(parsed);
    if (!parsed) {
        std::fprintf(stderr, "parse error: %s\n", error.c_str());
        return;
    }
    const auto copy = EffortModel::fromJson(*parsed);
    CHECK(copy);
    if (!copy) return;
    CHECK(copy->date() == "2026-09-12");
    CHECK(copy->savedAt() == 2000);
    CHECK(copy->size() == 4);
    CHECK(copy->at(3).name == "Leisure" && copy->at(3).targetSeconds == 7020);
    CHECK(copy->startOfDayMinutes() == 9 * 60);
    CHECK(copy->at(0).accumulatedSeconds == 600);
    CHECK(copy->at(0).notified);
    CHECK(copy->at(0).targetSeconds == 500);
    CHECK(copy->at(1).running);
    CHECK(copy->at(1).startedAt == 1600);
    CHECK(copy->at(1).elapsedSeconds(2100) == 500);
    CHECK(copy->at(2).percent == 12.0);
    CHECK(copy->exclusiveTimers());

    // A running timer keeps counting wall-clock time after a reload, even on a later day.
    CHECK(copy->at(1).elapsedSeconds(5000) == 3400);
    CHECK(copy->at(1).elapsedSeconds(90000) == 88400);

    // Structural errors are rejected; missing fields get defaults.
    CHECK(!EffortModel::fromJson(Json(5)));
    CHECK(!EffortModel::fromJson(*Json::parse("{\"date\":\"x\"}")));
    const auto sparse = EffortModel::fromJson(*Json::parse("{\"components\":[{\"name\":\"A\"},{\"name\":\"\"},7]}"));
    CHECK(sparse && sparse->size() == 1 && sparse->at(0).name == "A");
    CHECK(sparse && sparse->totalAvailableSeconds() == 13 * 3600);
    CHECK(sparse && sparse->startOfDayMinutes() == 9 * 60);
}

static void testJson() {
    Json o = Json::object();
    o.set("text", "quote\" backslash\\ newline\n tab\t");
    o.set("unicode", "caf\xC3\xA9");
    o.set("n", 1.5);
    o.set("i", 42);
    o.set("neg", -7);
    o.set("t", true);
    o.set("null", nullptr);
    Json arr = Json::array();
    arr.push(1).push("two").push(Json::object());
    o.set("arr", std::move(arr));

    for (int indent : {0, 2}) {
        std::string error;
        const auto back = Json::parse(o.dump(indent), &error);
        CHECK(back);
        if (!back) {
            std::fprintf(stderr, "parse error: %s\n", error.c_str());
            continue;
        }
        CHECK(back->get("text").toString() == "quote\" backslash\\ newline\n tab\t");
        CHECK(back->get("unicode").toString() == "caf\xC3\xA9");
        CHECK(back->get("n").toNumber() == 1.5);
        CHECK(back->get("i").toInt() == 42);
        CHECK(back->get("neg").toInt() == -7);
        CHECK(back->get("t").toBool());
        CHECK(back->get("null").isNull());
        CHECK(back->get("missing").isNull());
        CHECK(back->get("arr").size() == 3);
        CHECK(back->get("arr").at(1).toString() == "two");
        CHECK(back->get("arr").at(9).isNull());
    }

    CHECK(Json::parse("\"\\u00e9\\ud83d\\ude00\"")->toString() == "\xC3\xA9\xF0\x9F\x98\x80");
    CHECK(Json::parse("  [ ]  ")->isArray());
    CHECK(Json::parse("{\"a\":{\"b\":[1,2,{\"c\":null}]}}")->get("a").get("b").at(2).get("c").isNull());
    CHECK(!Json::parse(""));
    CHECK(!Json::parse("{"));
    CHECK(!Json::parse("[1,]"));
    CHECK(!Json::parse("{\"a\" 1}"));
    CHECK(!Json::parse("tru"));
    CHECK(!Json::parse("1 2"));
    CHECK(!Json::parse("\"unterminated"));
}

static void testStore() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("effort-tests-" + std::to_string(nowEpochSeconds()));
    const std::filesystem::path state = Store::statePath(dir);

    std::string error;
    CHECK(!Store::load(state, &error));
    CHECK(!error.empty());

    EffortModel m = EffortModel::withDefaults("2026-09-12");
    m.start(2, 100);
    m.stop(2, 160);
    CHECK(Store::save(m, state, 500, &error));
    CHECK(std::filesystem::exists(state));
    CHECK(!std::filesystem::exists(state.string() + ".tmp"));

    const auto loaded = Store::load(state, &error);
    CHECK(loaded);
    CHECK(loaded && loaded->savedAt() == 500);
    CHECK(loaded && loaded->at(2).accumulatedSeconds == 60);

    {
        std::ofstream corrupt(state, std::ios::trunc);
        corrupt << "{ not json";
    }
    CHECK(!Store::load(state, &error));
    CHECK(error.find("invalid JSON") != std::string::npos);

    std::filesystem::remove_all(dir);
}

int main() {
    setenv("TZ", "UTC", 1);
    tzset();
    testDurations();
    testClockAndRemaining();
    testEndDate();
    testModel();
    testJsonRoundTrip();
    testJson();
    testStore();
    if (failures == 0) {
        std::printf("core_tests: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "core_tests: %d check(s) failed\n", failures);
    return 1;
}
