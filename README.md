# Effort Manager

A small native macOS app for tracking how a day at the office is split between
components such as *Own Research*, *Funded Project* and *Coursework*.
All logic is plain C++; only the thin Cocoa window layer is Objective-C++.

## Build and run

Requires the Xcode Command Line Tools and CMake 3.21 or newer (Ninja is optional).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
open build/EffortManager.app
```

Run the unit tests for the core with:

```bash
ctest --test-dir build --output-on-failure
```

To work in Xcode instead: `cmake -S . -B xcode -G Xcode`, then open `xcode/EffortManager.xcodeproj`.

## Using the app

- **Header** — today's date, weekday and the days left until the end date, as
    `12/09/2026 Sat @ 26409 Days Left`, in large dark-red type; the clock line
  below it is larger still. The Reset day button turns orange if the timers shown belong to an
  earlier day, and the "%" column header turns orange while the percentages do
  not add up to 100.
- **Clock** — the large label shows the current time and the time left until the
  end of study, as `6:46:18 PM @ 3:13:29 Left`.
- **Study starts at / Study ends at** — pick when your study time starts and
  ends today (9 AM and 10 PM by default). *Time available today* is the
  difference, and the clock line counts down to the end time. **Set study start
  time as now** puts the current time into *Study starts at*.
- **Unlock inputs** — every input field and selector (component names, targets,
  percentages, the new-component field, and the date and time selectors) is
  disabled until this box is ticked, and they lock again whenever the window
  loses focus, so a stray key press cannot change a value. Buttons always work.
- **Remain Target** — the component's target minus its elapsed time; it goes
  negative once the target is exceeded.
- **% Done** — elapsed time as a percentage of the target.
- **Report on** — pick the report date. The days in the header
  count from today up to that day: today counts, the end day does not, so it
  shows 0 on the end day. Defaults to 2099-01-01 and is remembered.
- **Show data folder** — reveals `state.json` in the Finder.
- **Reset day** — press it when a new day starts. The components, percentages,
  targets and study start/end times go back to their defaults. Elapsed times and
  a running timer are kept for the default components (matched by name); any
  other component is removed. The *Report on* date is kept.
- **Reset elapsed time** — sets every component to 0:00:00 and stops all timers,
  after a confirmation in which Cancel is the default. The totals are not kept
  anywhere.
- **Components** — the defaults are Own Research, Funded Project, Coursework
  and Leisure at 43 / 30 / 12 / 15 % of the study day (9 AM to 10 PM by
  default, 13 hours). Type a name in *New component* and press Add or Return to add
  one; *Remove* deletes a row (it asks first if the row has time tracked today).
  The name field is editable.
- **Start / Stop** — one timer per component. Starting a component stops the one
  that is running, so the day's effort is always divided between components.
  A running timer counts wall-clock time until you press Stop, start another
  component, or press Reset day: it keeps counting while the Mac sleeps or is
  shut down, across midnight, and while the app is closed.
- **Target** — type a duration directly into the *Target* field, or fill in
  each component's *%* and press **Compute targets from %**, which shares
  *Time available today* out by percentage. Percent-based targets are rounded to
  whole minutes. Leave a target empty for none.
- **Screen overlay** — while the app is open, the top right of every screen
  shows a click-through label on a light rounded backdrop: "Timer Stopped"
  flashing red and black twice a second, about a fifth of the screen height
  tall, while no component is running; otherwise the running component's name
  and its percentage of target, such as `Leisure @ 104%`, flashing green and
  black once a second, about a twentieth of the screen height tall, translucent
  and set a tenth of the screen height lower so it gets in the way less. Hover
  the pointer over the running label and it hides, leaving only a red outline
  0.2 % of the screen height thick, so whatever is underneath can be read.
- **Notification** — when a component reaches its target you get a macOS
  notification (allow it when asked at first launch; it can be changed later in
  System Settings > Notifications), a beep, a bouncing dock icon, and the row
  turns green. If notifications are not allowed, an alert is shown inside the
  window instead. Each target notifies once per day; changing the target re-arms it.
- **Durations** are accepted as `2:30`, `2:30:15`, `2h30m`, `45m`, `90s`, or a plain
  number of hours such as `1.5`.

## Where the data lives

Everything is stored in `~/Library/Application Support/EffortManager/`:

- `state.json` — the live state (components, timers, targets, percentages,
  start and end of the study time, and the report date).
  It is written after every change and every 15 s while a timer runs. No
  history is kept: Reset day discards the day's totals.

Closing the window quits the app; the state is saved first. A running timer keeps
counting by wall-clock time while the app is closed and is still running when the
app is reopened, whichever day that is. Until you press *Reset day* the timers
belong to the day they were started on, and the Reset day button turns orange to
say so.

To allow several timers to run at once, quit the app and set
`"exclusiveTimers": false` in `state.json`.

## Project layout

```
CMakeLists.txt
src/core/        Pure C++: EffortModel (timers, targets), Json, Store, TimeFormat
src/mac/main.mm  Cocoa window, menu and notifications (the only Objective-C++ file)
src/mac/Info.plist.in
tests/core_tests.cpp   Unit tests for src/core (ctest)
```
