// Cocoa front-end for Effort Manager.
//
// This is the only Objective-C++ file in the project. Every timing, target and
// persistence rule lives in src/core (pure C++); this file just draws the window,
// forwards clicks to the model and delivers notifications.

#import <Cocoa/Cocoa.h>
#import <UserNotifications/UserNotifications.h>

#include "EffortModel.h"
#include "Store.h"
#include "TimeFormat.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using effort::EffortModel;
using effort::Store;

namespace {

constexpr int kAutosaveEverySeconds = 15;  // while a timer runs, keep the saved timestamp fresh
constexpr CGFloat kNameFieldWidth = 170;
constexpr CGFloat kDurationFieldWidth = 76;
constexpr CGFloat kPercentFieldWidth = 56;
constexpr CGFloat kStatusMinWidth = 170;
constexpr CGFloat kMinWindowWidth = 780;
constexpr CGFloat kStoppedOverlayTextHeightFraction = 0.20;  // "Timer Stopped" line height vs. screen height
constexpr CGFloat kRunningOverlayTextHeightFraction = 0.05;  // running component name, same ratio
constexpr CGFloat kRunningOverlayTopOffsetFraction = 0.10;   // pushed down so it covers less of other apps
constexpr CGFloat kRunningOverlayAlpha = 0.45;               // whole label, text and backdrop, is translucent
constexpr CGFloat kOverlayMargin = 12;
constexpr NSTimeInterval kStoppedFlashInterval = 0.25;  // red/black, 2 Hz
constexpr NSTimeInterval kRunningFlashInterval = 0.5;   // green/black, 1 Hz
constexpr CGFloat kOverlayBackdropAlpha = 0.78;      // light, slightly translucent backdrop
constexpr CGFloat kInset = 18;
constexpr NSUInteger kGridRowIndex = 2;  // title row, clock row, then the grid

NSString *ns(const std::string &s) {
    NSString *out = [NSString stringWithUTF8String:s.c_str()];
    return out ? out : @"";
}

std::string str(NSString *s) {
    const char *utf8 = [s UTF8String];
    return utf8 ? std::string(utf8) : std::string();
}

NSTextField *makeLabel(NSString *text) {
    NSTextField *label = [NSTextField labelWithString:text];
    label.translatesAutoresizingMaskIntoConstraints = NO;
    return label;
}

NSTextField *makeField(NSString *text, CGFloat width, NSString *identifier, NSInteger tag,
                       id<NSTextFieldDelegate> delegate) {
    NSTextField *field = [NSTextField textFieldWithString:text];
    field.translatesAutoresizingMaskIntoConstraints = NO;
    field.identifier = identifier;
    field.tag = tag;
    field.delegate = delegate;
    [field.widthAnchor constraintEqualToConstant:width].active = YES;
    return field;
}

NSButton *makeButton(NSString *title, id target, SEL action, NSInteger tag) {
    NSButton *button = [NSButton buttonWithTitle:title target:target action:action];
    button.translatesAutoresizingMaskIntoConstraints = NO;
    button.tag = tag;
    return button;
}

NSString *percentText(double percent) {
    return percent > 0 ? [NSString stringWithFormat:@"%g", percent] : @"";
}

NSFont *bigFont() {
    return [NSFont monospacedDigitSystemFontOfSize:44 weight:NSFontWeightSemibold];
}

// The clock line is about 180 % of the header size.
NSFont *clockFont() {
    return [NSFont monospacedDigitSystemFontOfSize:80 weight:NSFontWeightSemibold];
}

// Dark red for the two big labels; a lighter red in dark mode so it stays readable.
NSColor *darkRed() {
    return [NSColor colorWithName:@"EffortManagerDarkRed" dynamicProvider:^NSColor *(NSAppearance *appearance) {
        NSAppearanceName best = [appearance bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]];
        if ([best isEqualToString:NSAppearanceNameDarkAqua]) {
            return [NSColor colorWithSRGBRed:0.85 green:0.32 blue:0.32 alpha:1.0];
        }
        return [NSColor colorWithSRGBRed:0.55 green:0.0 blue:0.0 alpha:1.0];
    }];
}

// The text in `color`, except for the "@" separator which keeps the normal label colour.
NSAttributedString *accentedText(NSString *text, NSFont *font, NSColor *color) {
    NSMutableAttributedString *result = [[NSMutableAttributedString alloc]
        initWithString:text
            attributes:@{ NSFontAttributeName : font, NSForegroundColorAttributeName : color }];
    const NSRange separator = [text rangeOfString:@"@"];
    if (separator.location != NSNotFound) {
        [result addAttribute:NSForegroundColorAttributeName value:NSColor.labelColor range:separator];
    }
    return result;
}

}  // namespace

@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate, NSTextFieldDelegate, UNUserNotificationCenterDelegate>
@end

@implementation AppDelegate {
    EffortModel _model;
    std::filesystem::path _statePath;

    NSWindow *_window;
    NSStackView *_rootStack;
    NSTextField *_headerLabel;  // "12/09/2026 Sat @ 26409 Days Left"
    NSTextField *_clockLabel;
    NSDatePicker *_startPicker;
    NSDatePicker *_endPicker;
    NSDatePicker *_endDatePicker;
    NSButton *_unlockCheckbox;     // every input field and selector is disabled unless this is ticked
    BOOL _inputsUnlocked;
    NSTextField *_totalLabel;  // Study ends at minus Study starts at
    NSGridView *_grid;
    NSTextField *_newNameField;
    NSButton *_resetButton;
    NSTextField *_percentHeader;
    BOOL _saveErrorShown;

    NSMutableArray<NSTextField *> *_nameFields;
    NSMutableArray<NSTextField *> *_elapsedLabels;
    NSMutableArray<NSTextField *> *_targetFields;
    NSMutableArray<NSTextField *> *_percentFields;
    NSMutableArray<NSTextField *> *_remainTargetLabels;
    NSMutableArray<NSTextField *> *_percentDoneLabels;
    NSMutableArray<NSTextField *> *_rowStatusLabels;
    NSMutableArray<NSButton *> *_startButtons;
    NSMutableArray<NSButton *> *_stopButtons;

    NSTimer *_timer;
    BOOL _notificationsAuthorized;
    int _secondsSinceSave;

    // Top-right overlay on every screen: flashing "Timer Stopped" while nothing runs,
    // or the running component's name in green.
    NSMutableArray<NSWindow *> *_overlayWindows;
    NSMutableArray<NSTextField *> *_overlayLabels;
    NSMutableArray<NSNumber *> *_overlayPaddings;  // per window, depends on its screen
    NSString *_overlayText;        // text currently shown
    CGFloat _overlayHeightFraction;  // mode the windows were built for
    NSTimer *_flashTimer;
    NSColor *_flashColorA;
    NSColor *_flashColorB;
    BOOL _flashPhase;
}

#pragma mark - Application lifecycle

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    const std::filesystem::path directory = Store::defaultDirectory();
    _statePath = Store::statePath(directory);

    const std::int64_t now = effort::nowEpochSeconds();
    const std::string today = effort::localDateString(now);
    NSString *initialStatus = nil;

    std::string error;
    std::error_code ec;
    const bool stateExists = std::filesystem::exists(_statePath, ec);
    if (std::optional<EffortModel> loaded = stateExists ? Store::load(_statePath, &error) : std::nullopt) {
        _model = std::move(*loaded);  // a running timer keeps counting, whatever the day
        initialStatus = @"State restored from ~/Library/Application Support/EffortManager.";
    } else {
        if (stateExists) {
            // Keep the unreadable file around rather than overwriting it.
            std::filesystem::path backup = _statePath;
            backup += ".unreadable-" + std::to_string(now);
            std::filesystem::rename(_statePath, backup, ec);
            NSLog(@"Could not read state (%s); moved it to %s", error.c_str(), backup.string().c_str());
            initialStatus = @"Previous state could not be read; it was kept as state.json.unreadable-*.";
        } else {
            initialStatus = @"Fresh start. Data is saved in ~/Library/Application Support/EffortManager.";
        }
        _model = EffortModel::withDefaults(today);
    }

    [self buildMenu];
    [self buildWindow];
    [self rebuildGrid];
    [self refreshFields];
    [self refreshDynamic];
    [self showStatus:initialStatus];
    [self showWindowInitially];
    [self persist];
    [self setupNotifications];

    _timer = [NSTimer timerWithTimeInterval:1.0 target:self selector:@selector(tick:) userInfo:nil repeats:YES];
    _timer.tolerance = 0.1;
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(screensChanged:)
                                                 name:NSApplicationDidChangeScreenParametersNotification
                                               object:nil];

    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return YES;
}

// When the window goes inactive, any edit in progress is committed and the inputs lock again.
- (void)windowDidResignKey:(NSNotification *)notification {
    if (notification.object != _window) return;
    [_window makeFirstResponder:nil];
    _inputsUnlocked = NO;
    [self applyInputLock];
}

// The overlay windows would otherwise keep the app alive after the main window closes.
- (void)windowWillClose:(NSNotification *)notification {
    if (notification.object != _window) return;
    [self hideOverlay];
    dispatch_async(dispatch_get_main_queue(), ^{ [NSApp terminate:nil]; });
}

- (BOOL)applicationSupportsSecureRestorableState:(NSApplication *)app {
    return YES;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    [_window makeFirstResponder:nil];  // commit any edit still in progress
    [self persist];
    return NSTerminateNow;
}

#pragma mark - Menu and window construction

- (void)buildMenu {
    NSMenu *menuBar = [[NSMenu alloc] init];

    NSMenuItem *appItem = [[NSMenuItem alloc] init];
    [menuBar addItem:appItem];
    NSMenu *appMenu = [[NSMenu alloc] init];
    [appMenu addItemWithTitle:@"About Effort Manager" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Hide Effort Manager" action:@selector(hide:) keyEquivalent:@"h"];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Quit Effort Manager" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = appMenu;

    NSMenuItem *editItem = [[NSMenuItem alloc] init];
    [menuBar addItem:editItem];
    NSMenu *editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
    [editMenu addItemWithTitle:@"Undo" action:NSSelectorFromString(@"undo:") keyEquivalent:@"z"];
    [editMenu addItemWithTitle:@"Redo" action:NSSelectorFromString(@"redo:") keyEquivalent:@"Z"];
    [editMenu addItem:[NSMenuItem separatorItem]];
    [editMenu addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
    [editMenu addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
    [editMenu addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
    [editMenu addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
    editItem.submenu = editMenu;

    NSMenuItem *windowItem = [[NSMenuItem alloc] init];
    [menuBar addItem:windowItem];
    NSMenu *windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
    [windowMenu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [windowMenu addItemWithTitle:@"Close" action:@selector(performClose:) keyEquivalent:@"w"];
    windowItem.submenu = windowMenu;
    NSApp.windowsMenu = windowMenu;

    NSApp.mainMenu = menuBar;
}

- (NSDatePicker *)makeTimePickerWithAction:(SEL)action toolTip:(NSString *)toolTip {
    NSDatePicker *picker = [[NSDatePicker alloc] init];
    picker.translatesAutoresizingMaskIntoConstraints = NO;
    picker.datePickerStyle = NSDatePickerStyleTextFieldAndStepper;
    picker.datePickerMode = NSDatePickerModeSingle;
    picker.datePickerElements = NSDatePickerElementFlagHourMinute;
    picker.target = self;
    picker.action = action;
    picker.toolTip = toolTip;
    return picker;
}

// A caption followed by a control, packed to the left of the row.
- (NSStackView *)pickerRowWithLabel:(NSString *)text picker:(NSView *)control {
    NSStackView *row = [self horizontalRow];
    row.spacing = 6;
    [row addView:makeLabel(text) inGravity:NSStackViewGravityLeading];
    [row addView:control inGravity:NSStackViewGravityLeading];
    return row;
}

- (void)setPicker:(NSDatePicker *)picker toMinutesSinceMidnight:(int)minutes {
    NSDate *date = [[NSCalendar currentCalendar] dateBySettingHour:minutes / 60
                                                             minute:minutes % 60
                                                             second:0
                                                             ofDate:[NSDate date]
                                                            options:0];
    if (date) picker.dateValue = date;
}

- (int)minutesSinceMidnightFromPicker:(NSDatePicker *)picker {
    NSDateComponents *parts = [[NSCalendar currentCalendar] components:(NSCalendarUnitHour | NSCalendarUnitMinute)
                                                              fromDate:picker.dateValue];
    return static_cast<int>(parts.hour * 60 + parts.minute);
}

- (NSStackView *)horizontalRow {
    NSStackView *row = [[NSStackView alloc] init];
    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    row.alignment = NSLayoutAttributeCenterY;
    row.spacing = 10;
    row.translatesAutoresizingMaskIntoConstraints = NO;
    return row;
}

- (void)buildWindow {
    // Not resizable: the window always sizes itself to its content (see fitWindowToContent).
    const NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                    NSWindowStyleMaskMiniaturizable;
    _window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, kMinWindowWidth, 360)
                                          styleMask:style
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    _window.title = @"Effort Manager";
    _window.releasedWhenClosed = NO;
    _window.delegate = self;

    _rootStack = [[NSStackView alloc] init];
    _rootStack.orientation = NSUserInterfaceLayoutOrientationVertical;
    _rootStack.alignment = NSLayoutAttributeLeading;
    _rootStack.spacing = 12;
    _rootStack.edgeInsets = NSEdgeInsetsMake(16, kInset, 16, kInset);
    _rootStack.translatesAutoresizingMaskIntoConstraints = NO;
    NSView *content = _window.contentView;
    [content addSubview:_rootStack];
    [NSLayoutConstraint activateConstraints:@[
        [_rootStack.topAnchor constraintEqualToAnchor:content.topAnchor],
        [_rootStack.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
        [_rootStack.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
        [_rootStack.bottomAnchor constraintLessThanOrEqualToAnchor:content.bottomAnchor],
    ]];

    // Row 0: today's date with the days left, data folder, reset.
    _headerLabel = makeLabel(@"");
    _headerLabel.font = bigFont();
    _resetButton = makeButton(@"Reset day", self, @selector(resetPressed:), -1);
    _resetButton.toolTip = @"Start a new day: inputs back to their defaults, elapsed times kept, report date kept.";
    NSButton *reset = _resetButton;
    NSButton *showData = makeButton(@"Show data folder", self, @selector(showDataFolderPressed:), -1);
    showData.toolTip = @"Reveal state.json in the Finder.";
    NSButton *resetElapsed = makeButton(@"Reset elapsed time", self, @selector(resetElapsedPressed:), -1);
    resetElapsed.toolTip = @"Set every component's elapsed time to zero and stop all timers. Asks first.";
    NSStackView *buttonColumn = [[NSStackView alloc] init];
    buttonColumn.orientation = NSUserInterfaceLayoutOrientationVertical;
    buttonColumn.alignment = NSLayoutAttributeTrailing;
    buttonColumn.spacing = 4;
    buttonColumn.translatesAutoresizingMaskIntoConstraints = NO;

    NSStackView *header = [self horizontalRow];
    [header addView:_headerLabel inGravity:NSStackViewGravityLeading];
    NSStackView *topButtons = [self horizontalRow];
    [topButtons addView:showData inGravity:NSStackViewGravityLeading];
    [topButtons addView:reset inGravity:NSStackViewGravityLeading];
    [buttonColumn addArrangedSubview:topButtons];
    [buttonColumn addArrangedSubview:resetElapsed];
    [header addView:buttonColumn inGravity:NSStackViewGravityTrailing];
    [_rootStack addArrangedSubview:header];
    [self spanFullWidth:header];

    // Row 1: large clock on the left; end-of-study time and date pickers on the right.
    _clockLabel = makeLabel(@"");
    _clockLabel.font = clockFont();
    _clockLabel.toolTip = @"Current time, and the time left until the end of study.";

    _startPicker = [self makeTimePickerWithAction:@selector(startTimeChanged:)
                                          toolTip:@"When your study time starts today."];
    NSStackView *startRow = [self pickerRowWithLabel:@"Study starts at" picker:_startPicker];

    _endPicker = [self makeTimePickerWithAction:@selector(endTimeChanged:)
                                        toolTip:@"When your study time ends today. The clock line counts down to this."];
    NSStackView *endRow = [self pickerRowWithLabel:@"Study ends at" picker:_endPicker];

    // Time available today = Study ends at - Study starts at.
    _totalLabel = makeLabel(@"0:00");
    _totalLabel.font = [NSFont monospacedDigitSystemFontOfSize:13 weight:NSFontWeightMedium];
    _totalLabel.toolTip = @"Study ends at minus Study starts at.";
    NSStackView *totalRow = [self pickerRowWithLabel:@"Time available today:" picker:_totalLabel];

    NSButton *compute = makeButton(@"Compute targets from %", self, @selector(computePressed:), -1);
    compute.toolTip = @"Set each component's target to its percentage of the time available.";

    _endDatePicker = [[NSDatePicker alloc] init];
    _endDatePicker.translatesAutoresizingMaskIntoConstraints = NO;
    _endDatePicker.datePickerStyle = NSDatePickerStyleTextFieldAndStepper;
    _endDatePicker.datePickerMode = NSDatePickerModeSingle;
    _endDatePicker.datePickerElements = NSDatePickerElementFlagYearMonthDay;
    _endDatePicker.target = self;
    _endDatePicker.action = @selector(endDateChanged:);
    _endDatePicker.toolTip = @"The report date. The days shown in the header count today but not this day.";
    NSStackView *endDateRow = [self pickerRowWithLabel:@"Report on" picker:_endDatePicker];

    _unlockCheckbox = [NSButton checkboxWithTitle:@"Unlock inputs" target:self action:@selector(unlockToggled:)];
    _unlockCheckbox.translatesAutoresizingMaskIntoConstraints = NO;
    _unlockCheckbox.toolTip = @"Every input field and selector stays disabled until this is ticked, and they lock "
                              @"again whenever the window loses focus, so values cannot change by accident.";

    NSButton *startNow = makeButton(@"Set study start time as now", self, @selector(setStartNowPressed:), -1);
    startNow.toolTip = @"Set Study starts at to the current time.";

    NSStackView *rightColumn = [[NSStackView alloc] init];
    rightColumn.orientation = NSUserInterfaceLayoutOrientationVertical;
    rightColumn.alignment = NSLayoutAttributeTrailing;
    rightColumn.spacing = 4;
    rightColumn.translatesAutoresizingMaskIntoConstraints = NO;
    [rightColumn addArrangedSubview:_unlockCheckbox];
    [rightColumn addArrangedSubview:endDateRow];
    [rightColumn addArrangedSubview:startNow];
    [rightColumn addArrangedSubview:startRow];
    [rightColumn addArrangedSubview:endRow];
    [rightColumn addArrangedSubview:totalRow];
    [rightColumn addArrangedSubview:compute];

    NSStackView *clockRow = [self horizontalRow];
    [clockRow addView:_clockLabel inGravity:NSStackViewGravityLeading];
    [clockRow addView:rightColumn inGravity:NSStackViewGravityTrailing];
    [_rootStack addArrangedSubview:clockRow];
    [self spanFullWidth:clockRow];


    // Row 2 is the component grid, inserted by rebuildGrid.

    // Row 3: add component, with the duration hint on the same line.
    NSStackView *add = [self horizontalRow];
    [add addView:makeLabel(@"New component:") inGravity:NSStackViewGravityLeading];
    _newNameField = makeField(@"", 220, @"newName", -1, self);
    _newNameField.placeholderString = @"e.g. Teaching";
    _newNameField.target = self;
    _newNameField.action = @selector(addPressed:);
    [add addView:_newNameField inGravity:NSStackViewGravityLeading];
    [add addView:makeButton(@"Add", self, @selector(addPressed:), -1) inGravity:NSStackViewGravityLeading];
    NSTextField *hint = makeLabel(@"Durations: 2:30, 2h30m, 45m, or 1.5 (hours)");
    hint.textColor = NSColor.tertiaryLabelColor;
    hint.font = [NSFont systemFontOfSize:11];
    [add addView:hint inGravity:NSStackViewGravityTrailing];
    [_rootStack addArrangedSubview:add];
    [self spanFullWidth:add];
}

// Rows in the vertical stack are leading-aligned and keep their natural width;
// this makes a row fill the window width instead.
- (void)spanFullWidth:(NSView *)view {
    [view.widthAnchor constraintEqualToAnchor:_rootStack.widthAnchor constant:-2 * kInset].active = YES;
}

- (void)rebuildGrid {
    _nameFields = [NSMutableArray array];
    _elapsedLabels = [NSMutableArray array];
    _targetFields = [NSMutableArray array];
    _percentFields = [NSMutableArray array];
    _remainTargetLabels = [NSMutableArray array];
    _percentDoneLabels = [NSMutableArray array];
    _rowStatusLabels = [NSMutableArray array];
    _startButtons = [NSMutableArray array];
    _stopButtons = [NSMutableArray array];

    NSMutableArray<NSView *> *headers = [NSMutableArray array];
    for (NSString *text in @[ @"Component", @"Elapsed", @"Target", @"%", @"Remain Target", @"% Done", @"Status", @"", @"", @"" ]) {
        NSTextField *label = makeLabel(text);
        label.font = [NSFont boldSystemFontOfSize:12];
        label.textColor = NSColor.secondaryLabelColor;
        if ([text isEqualToString:@"Remain Target"]) {
            label.toolTip = @"Target minus elapsed. Negative once the target is exceeded.";
        } else if ([text isEqualToString:@"% Done"]) {
            label.toolTip = @"Elapsed as a percentage of the target.";
        } else if ([text isEqualToString:@"%"]) {
            _percentHeader = label;
        }
        [headers addObject:label];
    }
    NSGridView *grid = [NSGridView gridViewWithViews:@[ headers ]];
    grid.translatesAutoresizingMaskIntoConstraints = NO;
    grid.rowSpacing = 6;
    grid.columnSpacing = 12;
    grid.xPlacement = NSGridCellPlacementLeading;
    grid.yPlacement = NSGridCellPlacementCenter;
    [grid columnAtIndex:1].xPlacement = NSGridCellPlacementTrailing;
    [grid columnAtIndex:4].xPlacement = NSGridCellPlacementTrailing;
    [grid columnAtIndex:5].xPlacement = NSGridCellPlacementTrailing;

    for (std::size_t i = 0; i < _model.size(); ++i) {
        const effort::Component &c = _model.at(i);
        const NSInteger tag = static_cast<NSInteger>(i);

        NSTextField *name = makeField(ns(c.name), kNameFieldWidth, @"name", tag, self);
        name.toolTip = @"Rename the component.";

        NSTextField *elapsed = makeLabel(@"0:00:00");
        elapsed.font = [NSFont monospacedDigitSystemFontOfSize:15 weight:NSFontWeightMedium];
        elapsed.alignment = NSTextAlignmentRight;

        NSTextField *target = makeField(c.hasTarget() ? ns(effort::formatHM(c.targetSeconds)) : @"",
                                        kDurationFieldWidth, @"target", tag, self);
        target.placeholderString = @"h:mm";
        target.alignment = NSTextAlignmentRight;
        target.toolTip = @"Time to spend on this component today. Leave empty for no target.";

        NSTextField *percent = makeField(percentText(c.percent), kPercentFieldWidth, @"percent", tag, self);
        percent.placeholderString = @"0";
        percent.alignment = NSTextAlignmentRight;
        percent.toolTip = @"Share of the time available, used by \"Compute targets from %\".";

        NSTextField *remainTarget = makeLabel(@"0:00:00");
        remainTarget.font = [NSFont monospacedDigitSystemFontOfSize:13 weight:NSFontWeightRegular];
        remainTarget.alignment = NSTextAlignmentRight;
        remainTarget.toolTip = @"Target minus elapsed. Negative once the target is exceeded.";

        NSTextField *percentDone = makeLabel(@"0%");
        percentDone.font = [NSFont monospacedDigitSystemFontOfSize:13 weight:NSFontWeightRegular];
        percentDone.alignment = NSTextAlignmentRight;
        percentDone.toolTip = @"Elapsed as a percentage of the target.";

        NSTextField *status = makeLabel(@"");
        status.font = [NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightRegular];
        [status.widthAnchor constraintGreaterThanOrEqualToConstant:kStatusMinWidth].active = YES;

        NSButton *start = makeButton(@"Start", self, @selector(startPressed:), tag);
        NSButton *stop = makeButton(@"Stop", self, @selector(stopPressed:), tag);
        NSButton *remove = makeButton(@"Remove", self, @selector(removePressed:), tag);

        [grid addRowWithViews:@[ name, elapsed, target, percent, remainTarget, percentDone, status, start, stop, remove ]];

        [_nameFields addObject:name];
        [_elapsedLabels addObject:elapsed];
        [_targetFields addObject:target];
        [_percentFields addObject:percent];
        [_remainTargetLabels addObject:remainTarget];
        [_percentDoneLabels addObject:percentDone];
        [_rowStatusLabels addObject:status];
        [_startButtons addObject:start];
        [_stopButtons addObject:stop];
    }

    NSUInteger index = kGridRowIndex;
    if (_grid) {
        const NSUInteger existing = [_rootStack.arrangedSubviews indexOfObject:_grid];
        if (existing != NSNotFound) index = existing;
        [_rootStack removeArrangedSubview:_grid];
        [_grid removeFromSuperview];
    }
    [_rootStack insertArrangedSubview:grid atIndex:index];
    _grid = grid;
    [self applyInputLock];  // new fields start in the current lock state

    // Keep the grid at its natural width. Without this it stretches to the widest row
    // (the header) and NSGridView pads the extra space into the first columns.
    [grid layoutSubtreeIfNeeded];
    NSLayoutConstraint *natural = [grid.widthAnchor constraintEqualToConstant:grid.fittingSize.width];
    natural.priority = NSLayoutPriorityDefaultHigh;  // content that grows can still widen it
    natural.active = YES;
}

// Content size needed to show every row without clipping. Computed from the rows
// themselves because NSStackView's fittingSize leaves out the trailing inset.
- (NSSize)requiredContentSize {
    [_rootStack layoutSubtreeIfNeeded];
    NSArray<NSView *> *rows = _rootStack.arrangedSubviews;
    CGFloat width = 0;
    CGFloat height = _rootStack.edgeInsets.top + _rootStack.edgeInsets.bottom;
    for (NSView *row in rows) {
        const NSSize fit = row.fittingSize;
        height += fit.height;
        width = MAX(width, fit.width);
    }
    if (rows.count > 1) height += _rootStack.spacing * (rows.count - 1);
    width += _rootStack.edgeInsets.left + _rootStack.edgeInsets.right;
    return NSMakeSize(MAX(width, kMinWindowWidth), height);
}

- (void)showWindowInitially {
    const NSSize required = [self requiredContentSize];
    [_window setContentSize:required];
    _window.contentMinSize = required;
    [_window center];
    [_window setFrameAutosaveName:@"EffortManagerMainWindow"];  // may restore an older, smaller frame
    [self fitWindowToContent];
    [_window makeKeyAndOrderFront:nil];
}

// Sizes the window exactly to its content, keeping the top-left corner in place,
// so rows can be added or removed without leaving blank space or clipping.
- (void)fitWindowToContent {
    const NSSize required = [self requiredContentSize];
    const NSRect current = [_window contentRectForFrameRect:_window.frame];
    const CGFloat width = required.width;
    const CGFloat height = required.height;
    _window.contentMinSize = required;
    _window.contentMaxSize = required;
    if (width == current.size.width && height == current.size.height) return;
    NSRect grown = current;
    grown.origin.y -= height - current.size.height;
    grown.size = NSMakeSize(width, height);
    [_window setFrame:[_window frameRectForContentRect:grown] display:YES animate:NO];
}

#pragma mark - Refresh

// Values that only change through user edits: the editable fields.
- (void)refreshFields {
    [self applyInputLock];
    [self setPicker:_startPicker toMinutesSinceMidnight:_model.startOfDayMinutes()];
    [self setPicker:_endPicker toMinutesSinceMidnight:_model.endOfDayMinutes()];
    const std::int64_t available = _model.totalAvailableSeconds();
    _totalLabel.stringValue = ns(effort::formatHM(available));
    _totalLabel.textColor = available > 0 ? NSColor.labelColor : NSColor.systemOrangeColor;

    int year = 0, month = 0, day = 0;
    if (std::sscanf(_model.endDate().c_str(), "%d-%d-%d", &year, &month, &day) == 3) {
        NSDateComponents *parts = [[NSDateComponents alloc] init];
        parts.year = year;
        parts.month = month;
        parts.day = day;
        parts.hour = 12;  // noon keeps the calendar day stable across DST changes
        NSDate *date = [[NSCalendar currentCalendar] dateFromComponents:parts];
        if (date) _endDatePicker.dateValue = date;
    }
    for (std::size_t i = 0; i < _model.size() && i < _nameFields.count; ++i) {
        const effort::Component &c = _model.at(i);
        _nameFields[i].stringValue = ns(c.name);
        _targetFields[i].stringValue = c.hasTarget() ? ns(effort::formatHM(c.targetSeconds)) : @"";
        _percentFields[i].stringValue = percentText(c.percent);
    }
}

// Values that change every second: elapsed times, statuses, buttons, summary.
- (void)refreshDynamic {
    const std::int64_t now = effort::nowEpochSeconds();
    const std::string today = effort::localDateString(now);
    const std::int64_t remainingToday = _model.remainingSecondsToday(now);

    NSString *clockText = [NSString stringWithFormat:@"%@ @ %@ Left",
                           ns(effort::formatClock12(now)), ns(effort::formatHMS(remainingToday))];
    _clockLabel.attributedStringValue = accentedText(clockText, clockFont(), darkRed());
    const std::int64_t daysLeft = _model.daysLeft(today);
    const bool endDatePassed = daysLeft == 0 && _model.endDate() != today;
    const bool staleDay = _model.date() != today;
    NSString *headerText = [NSString stringWithFormat:@"%@ @ %lld Days Left",
                            ns(effort::formatDayMonthYearWeekday(now)), static_cast<long long>(daysLeft)];
    _headerLabel.attributedStringValue = accentedText(headerText, bigFont(), darkRed());
    _headerLabel.toolTip = staleDay ? @"The data shown belongs to an earlier day. Press Reset day to start today."
                         : endDatePassed ? @"The end date has passed."
                         : @"Today, and the days left until the end date (today counts, the end day does not).";

    for (std::size_t i = 0; i < _model.size() && i < _elapsedLabels.count; ++i) {
        const effort::Component &c = _model.at(i);
        const std::int64_t elapsed = c.elapsedSeconds(now);
        const bool reached = c.targetReached(now);

        _elapsedLabels[i].stringValue = ns(effort::formatHMS(elapsed));
        _elapsedLabels[i].textColor = reached ? NSColor.systemGreenColor : NSColor.labelColor;
        if (c.hasTarget()) {
            _remainTargetLabels[i].stringValue = ns(effort::formatSignedHMS(c.remainingToTargetSeconds(now)));
            _remainTargetLabels[i].textColor = reached ? NSColor.systemGreenColor : NSColor.labelColor;
            _percentDoneLabels[i].stringValue = [NSString stringWithFormat:@"%.0f%%", c.percentDone(now)];
            _percentDoneLabels[i].textColor = reached ? NSColor.systemGreenColor : NSColor.labelColor;
        } else {
            _remainTargetLabels[i].stringValue = @"\u2014";
            _remainTargetLabels[i].textColor = NSColor.secondaryLabelColor;
            _percentDoneLabels[i].stringValue = @"\u2014";
            _percentDoneLabels[i].textColor = NSColor.secondaryLabelColor;
        }

        NSString *status = @"";
        NSColor *color = NSColor.secondaryLabelColor;
        if (c.running && reached) {
            status = @"● running · target reached";
            color = NSColor.systemGreenColor;
        } else if (c.running) {
            status = @"● running";
            color = NSColor.systemBlueColor;
        } else if (reached) {
            status = @"✓ target reached";
            color = NSColor.systemGreenColor;
        }
        _rowStatusLabels[i].stringValue = status;
        _rowStatusLabels[i].textColor = color;

        _startButtons[i].enabled = !c.running;
        _stopButtons[i].enabled = c.running;
    }

    [self updateOverlay];

    // The Reset day button turns orange while the data shown belongs to an earlier day.
    _resetButton.bezelColor = staleDay ? NSColor.systemOrangeColor : nil;
    _resetButton.toolTip = staleDay
        ? [NSString stringWithFormat:@"The timers shown are from %@. Press to start today.", ns(_model.date())]
        : @"Start a new day: inputs back to their defaults, elapsed times kept, report date kept.";

    // The % header turns orange while the percentages do not add up to 100.
    const double percentSum = _model.percentSum();
    const bool percentOff = std::fabs(percentSum - 100.0) > 0.01;
    _percentHeader.textColor = percentOff ? NSColor.systemOrangeColor : NSColor.secondaryLabelColor;
    _percentHeader.toolTip = percentOff
        ? [NSString stringWithFormat:@"Percentages sum to %g%%, not 100%%.", percentSum]
        : @"Share of the time available for each component.";
}

// Progress messages go to the console only; the window has no status line.
- (void)showStatus:(NSString *)message {
    if (message.length > 0) NSLog(@"%@", message);
}

- (void)rejectInput:(NSString *)message {
    NSBeep();
    [self showStatus:message];
}

- (void)persist {
    std::string error;
    if (!Store::save(_model, _statePath, effort::nowEpochSeconds(), &error)) {
        NSLog(@"Save failed: %s", error.c_str());
        if (!_saveErrorShown && _window) {
            _saveErrorShown = YES;  // report once, not on every autosave
            NSAlert *alert = [[NSAlert alloc] init];
            alert.messageText = @"Could not save the state file";
            alert.informativeText = ns(error);
            [alert addButtonWithTitle:@"OK"];
            [alert beginSheetModalForWindow:_window completionHandler:nil];
        }
    }
    _secondsSinceSave = 0;
}

- (BOOL)confirmWithTitle:(NSString *)title text:(NSString *)text button:(NSString *)button {
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = title;
    alert.informativeText = text;
    [alert addButtonWithTitle:button];
    [alert addButtonWithTitle:@"Cancel"];
    return [alert runModal] == NSAlertFirstButtonReturn;
}

#pragma mark - Timer

- (void)tick:(NSTimer *)timer {
    const std::int64_t now = effort::nowEpochSeconds();
    const std::vector<std::size_t> reached = _model.collectNewlyReached(now);
    for (std::size_t i : reached) [self announceTargetReached:i now:now];
    [self refreshDynamic];
    if (!reached.empty()) {
        [self persist];
    } else if (_model.runningIndex() && ++_secondsSinceSave >= kAutosaveEverySeconds) {
        [self persist];
    }
}

#pragma mark - Actions

- (void)startPressed:(NSButton *)sender {
    [_window makeFirstResponder:nil];
    const std::size_t i = static_cast<std::size_t>(sender.tag);
    if (i >= _model.size()) return;
    _model.start(i, effort::nowEpochSeconds());
    [self persist];
    [self refreshDynamic];
    [self showStatus:[NSString stringWithFormat:@"Started %@.", ns(_model.at(i).name)]];
}

- (void)stopPressed:(NSButton *)sender {
    [_window makeFirstResponder:nil];
    const std::size_t i = static_cast<std::size_t>(sender.tag);
    if (i >= _model.size()) return;
    const std::int64_t now = effort::nowEpochSeconds();
    _model.stop(i, now);
    [self persist];
    [self refreshDynamic];
    [self showStatus:[NSString stringWithFormat:@"Stopped %@ at %@.", ns(_model.at(i).name),
                                                ns(effort::formatHMS(_model.at(i).elapsedSeconds(now)))]];
}

- (void)removePressed:(NSButton *)sender {
    [_window makeFirstResponder:nil];
    const std::size_t i = static_cast<std::size_t>(sender.tag);
    if (i >= _model.size()) return;
    const std::int64_t now = effort::nowEpochSeconds();
    const std::string name = _model.at(i).name;
    const std::int64_t elapsed = _model.at(i).elapsedSeconds(now);
    if (elapsed > 0) {
        NSString *text = [NSString stringWithFormat:@"%@ has %@ tracked today. That time will be discarded.",
                                                    ns(name), ns(effort::formatHMS(elapsed))];
        if (![self confirmWithTitle:[NSString stringWithFormat:@"Remove “%@”?", ns(name)]
                               text:text button:@"Remove"]) {
            return;
        }
    }
    _model.remove(i);
    [self persist];
    [self rebuildGrid];
    [self refreshFields];
    [self refreshDynamic];
    [self fitWindowToContent];
    [self showStatus:[NSString stringWithFormat:@"Removed %@.", ns(name)]];
}

- (void)addPressed:(id)sender {
    const std::string name = effort::trimmed(str(_newNameField.stringValue));
    if (name.empty()) {
        [_window makeFirstResponder:_newNameField];
        return;
    }
    if (!_model.add(name)) {
        [self rejectInput:@"A component with that name already exists."];
        return;
    }
    _newNameField.stringValue = @"";
    [self persist];
    [self rebuildGrid];
    [self refreshFields];
    [self refreshDynamic];
    [self fitWindowToContent];
    [self showStatus:[NSString stringWithFormat:@"Added %@. Give it a percentage or a target.", ns(name)]];
}

- (void)computePressed:(id)sender {
    [_window makeFirstResponder:nil];  // commits a pending edit of a percentage field
    if (_model.totalAvailableSeconds() <= 0) {
        [self rejectInput:@"Study ends at must be later than Study starts at."];
        return;
    }
    _model.computeTargetsFromPercents();
    [self persist];
    [self refreshFields];
    [self refreshDynamic];
    const double sum = _model.percentSum();
    NSString *note = std::fabs(sum - 100.0) > 0.01
        ? [NSString stringWithFormat:@" Note: the percentages sum to %g%%, not 100%%.", sum]
        : @"";
    [self showStatus:[NSString stringWithFormat:@"Targets computed from percentages of %@.%@",
                                                ns(effort::formatHM(_model.totalAvailableSeconds())), note]];
}

- (void)startTimeChanged:(NSDatePicker *)sender {
    const int minutes = [self minutesSinceMidnightFromPicker:sender];
    if (minutes == _model.startOfDayMinutes()) return;
    _model.setStartOfDayMinutes(minutes);
    [self persist];
    [self refreshFields];
    [self refreshDynamic];
}

// Locked by default: selectors and every text field are disabled until "Unlock inputs" is ticked.
- (void)applyInputLock {
    const BOOL on = _inputsUnlocked;
    _startPicker.enabled = on;
    _endPicker.enabled = on;
    _endDatePicker.enabled = on;
    _newNameField.enabled = on;
    for (NSTextField *field in _nameFields) field.enabled = on;
    for (NSTextField *field in _targetFields) field.enabled = on;
    for (NSTextField *field in _percentFields) field.enabled = on;
    _unlockCheckbox.state = on ? NSControlStateValueOn : NSControlStateValueOff;
}

- (void)unlockToggled:(NSButton *)sender {
    _inputsUnlocked = sender.state == NSControlStateValueOn;
    if (!_inputsUnlocked) [_window makeFirstResponder:nil];  // commit an edit in progress
    [self applyInputLock];
}

- (void)setStartNowPressed:(id)sender {
    [_window makeFirstResponder:nil];
    const std::int64_t now = effort::nowEpochSeconds();
    const int minutes = static_cast<int>(effort::localSecondsSinceMidnight(now) / 60);
    _model.setStartOfDayMinutes(minutes);
    [self persist];
    [self refreshFields];
    [self refreshDynamic];
    [self showStatus:[NSString stringWithFormat:@"Study starts at set to %@.",
                                                ns(effort::formatTimeOfDay12(minutes))]];
}

- (void)endTimeChanged:(NSDatePicker *)sender {
    const int minutes = [self minutesSinceMidnightFromPicker:sender];
    if (minutes == _model.endOfDayMinutes()) return;
    _model.setEndOfDayMinutes(minutes);
    [self persist];
    [self refreshFields];
    [self refreshDynamic];
    [self showStatus:[NSString stringWithFormat:@"Study time now ends at %@.",
                                                ns(effort::formatTimeOfDay12(_model.endOfDayMinutes()))]];
}

- (void)endDateChanged:(NSDatePicker *)sender {
    NSDateComponents *parts = [[NSCalendar currentCalendar] components:(NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay)
                                                              fromDate:sender.dateValue];
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%04ld-%02ld-%02ld", static_cast<long>(parts.year),
                  static_cast<long>(parts.month), static_cast<long>(parts.day));
    const std::string date = buffer;
    if (date == _model.endDate()) return;
    if (!_model.setEndDate(date)) {
        [self rejectInput:@"That is not a valid date."];
        [self refreshFields];
        return;
    }
    [self persist];
    [self refreshDynamic];
    [self showStatus:[NSString stringWithFormat:@"Study period now ends on %@.", ns(_model.endDate())]];
}

- (void)showDataFolderPressed:(id)sender {
    [self persist];  // make sure the file exists before revealing it
    NSURL *stateURL = [NSURL fileURLWithPath:ns(_statePath.string())];
    [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ stateURL ]];
}

- (void)resetPressed:(id)sender {
    [_window makeFirstResponder:nil];
    const std::int64_t now = effort::nowEpochSeconds();
    NSString *text = @"The components, percentages, targets and study times go back to their defaults. "
                     @"Elapsed times and a running timer are kept for the default components; any other "
                     @"component is removed. The report date is kept. Use Reset elapsed time to zero the timers.";
    if (![self confirmWithTitle:@"Start a new day?" text:text button:@"Reset"]) return;

    _model.resetDay(effort::localDateString(now));
    [self persist];
    [self rebuildGrid];  // the component list may have changed
    [self refreshFields];
    [self refreshDynamic];
    [self fitWindowToContent];
    [self showStatus:@"New day started from the defaults; elapsed times kept."];
}

// Destructive, so the alert makes Cancel the default and warns before zeroing anything.
- (void)resetElapsedPressed:(id)sender {
    [_window makeFirstResponder:nil];
    const std::int64_t now = effort::nowEpochSeconds();
    NSAlert *alert = [[NSAlert alloc] init];
    alert.alertStyle = NSAlertStyleCritical;
    alert.messageText = @"Reset all elapsed time?";
    alert.informativeText = [NSString stringWithFormat:@"%@ tracked today across all components will be set to 0:00:00 "
                                                        @"and every timer stopped. This cannot be undone.",
                                                        ns(effort::formatHMS(_model.totalElapsedSeconds(now)))];
    [alert addButtonWithTitle:@"Cancel"];               // default: Return keeps the data
    [alert addButtonWithTitle:@"Reset elapsed time"];
    if ([alert runModal] != NSAlertSecondButtonReturn) return;

    _model.resetElapsed();
    [self persist];
    [self refreshDynamic];
    [self showStatus:@"Elapsed times reset."];
}

#pragma mark - Text field editing

- (void)controlTextDidEndEditing:(NSNotification *)notification {
    NSTextField *field = notification.object;
    if (![field isKindOfClass:[NSTextField class]]) return;
    NSString *kind = field.identifier;
    const std::string text = effort::trimmed(str(field.stringValue));
    const std::size_t row = field.tag >= 0 ? static_cast<std::size_t>(field.tag) : SIZE_MAX;
    bool changed = false;

    if ([kind isEqualToString:@"target"] && row < _model.size()) {
        if (text.empty()) {
            _model.setTarget(row, 0);
            changed = true;
        } else if (const auto seconds = effort::parseDuration(text)) {
            _model.setTarget(row, *seconds);
            changed = true;
        } else {
            [self rejectInput:@"Could not read the target. Use h:mm, 2h30m or 1.5 (hours)."];
        }
    } else if ([kind isEqualToString:@"percent"] && row < _model.size()) {
        std::string digits = text;
        if (!digits.empty() && digits.back() == '%') digits.pop_back();
        char *end = nullptr;
        const double value = digits.empty() ? 0.0 : std::strtod(digits.c_str(), &end);
        if (digits.empty() || (end != nullptr && *end == '\0')) {
            _model.setPercent(row, value);
            changed = true;
        } else {
            [self rejectInput:@"The percentage must be a number."];
        }
    } else if ([kind isEqualToString:@"name"] && row < _model.size()) {
        if (text != _model.at(row).name) {
            if (_model.rename(row, text)) {
                changed = true;
            } else {
                [self rejectInput:@"Component names must be unique and not empty."];
            }
        }
    } else {
        return;  // the new-component field is handled by its action
    }

    if (changed) [self persist];
    [self refreshFields];
    [self refreshDynamic];
}

#pragma mark - Screen overlay

// Shows the right overlay for the current state: "Timer Stopped" flashing red/black
// while nothing runs, otherwise the running component's name in green (smaller).
- (void)updateOverlay {
    const std::optional<std::size_t> running = _model.runningIndex();
    NSString *text = @"Timer Stopped";
    if (running) {
        const effort::Component &c = _model.at(*running);
        text = c.hasTarget() ? [NSString stringWithFormat:@"%@ @ %.0f%%", ns(c.name), c.percentDone(effort::nowEpochSeconds())]
                             : ns(c.name);
    }
    const CGFloat fraction = running ? kRunningOverlayTextHeightFraction : kStoppedOverlayTextHeightFraction;
    if (_overlayWindows.count == 0 || fraction != _overlayHeightFraction) {
        [self buildOverlayWindowsWithText:text
                           heightFraction:fraction
                        topOffsetFraction:running ? kRunningOverlayTopOffsetFraction : 0.0
                                    alpha:running ? kRunningOverlayAlpha : 1.0];
    } else if (![text isEqualToString:_overlayText]) {
        [self setOverlayText:text];  // e.g. the percentage moved on
    }
    if (running) {
        [self flashBetween:[NSColor colorWithSRGBRed:0.0 green:0.55 blue:0.1 alpha:1.0]
                       and:NSColor.blackColor
                  interval:kRunningFlashInterval];
    } else {
        [self flashBetween:NSColor.systemRedColor and:NSColor.blackColor interval:kStoppedFlashInterval];
    }
    for (NSWindow *window in _overlayWindows) {
        if (!window.visible) [window orderFrontRegardless];
    }
}

- (void)hideOverlay {
    [self stopFlashing];
    for (NSWindow *window in _overlayWindows) [window orderOut:nil];
}

// Alternates the overlay text between two colours; a call with the same colours and
// interval leaves the running flash untouched so the rhythm is not reset every second.
- (void)flashBetween:(NSColor *)colorA and:(NSColor *)colorB interval:(NSTimeInterval)interval {
    if (_flashTimer && _flashTimer.timeInterval == interval && [_flashColorA isEqual:colorA] &&
        [_flashColorB isEqual:colorB]) {
        return;
    }
    [self stopFlashing];
    _flashColorA = colorA;
    _flashColorB = colorB;
    _flashPhase = NO;
    [self flashTick:nil];
    _flashTimer = [NSTimer timerWithTimeInterval:interval target:self
                                        selector:@selector(flashTick:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_flashTimer forMode:NSRunLoopCommonModes];
}

- (void)stopFlashing {
    [_flashTimer invalidate];
    _flashTimer = nil;
}

- (void)flashTick:(NSTimer *)timer {
    _flashPhase = !_flashPhase;
    NSColor *color = _flashPhase ? _flashColorA : _flashColorB;
    for (NSTextField *label in _overlayLabels) label.textColor = color;
}

- (void)destroyOverlayWindows {
    for (NSWindow *window in _overlayWindows) [window close];
    _overlayWindows = [NSMutableArray array];
    _overlayLabels = [NSMutableArray array];
    _overlayPaddings = [NSMutableArray array];
    _overlayText = nil;
    _overlayHeightFraction = 0;
}

// Changes the text of the existing windows, resizing each so it stays anchored at its
// top-right corner. Cheaper than rebuilding, which matters as the percentage ticks up.
- (void)setOverlayText:(NSString *)text {
    _overlayText = text;
    for (NSUInteger i = 0; i < _overlayWindows.count; ++i) {
        NSWindow *window = _overlayWindows[i];
        NSTextField *label = _overlayLabels[i];
        const CGFloat padding = _overlayPaddings[i].doubleValue;
        label.autoresizingMask = NSViewNotSizable;
        label.stringValue = text;
        [label sizeToFit];
        const NSRect old = window.frame;
        const NSSize size = NSMakeSize(label.frame.size.width + 2 * padding, label.frame.size.height + 2 * padding);
        [window setFrame:NSMakeRect(NSMaxX(old) - size.width, NSMaxY(old) - size.height, size.width, size.height)
                 display:YES];
        label.frame = NSInsetRect(window.contentView.bounds, padding, padding);
        label.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    }
}

// One borderless, transparent, click-through window per screen, top right, below the
// menu bar (moved down by `topOffset` of the screen height), with the text on a light
// rounded backdrop. `alpha` applies to the whole window.
- (void)buildOverlayWindowsWithText:(NSString *)text
                     heightFraction:(CGFloat)fraction
                  topOffsetFraction:(CGFloat)topOffset
                              alpha:(CGFloat)alpha {
    [self destroyOverlayWindows];
    _overlayText = text;
    _overlayHeightFraction = fraction;
    for (NSScreen *screen in NSScreen.screens) {
        // Pick the point size whose line height is the wanted fraction of the screen height.
        NSFont *probe = [NSFont systemFontOfSize:100 weight:NSFontWeightBold];
        const CGFloat lineHeightAt100 = probe.ascender - probe.descender;
        const CGFloat wantedHeight = screen.frame.size.height * fraction;
        NSFont *font = [NSFont systemFontOfSize:100.0 * wantedHeight / lineHeightAt100 weight:NSFontWeightBold];

        NSTextField *label = [NSTextField labelWithString:text];
        label.translatesAutoresizingMaskIntoConstraints = YES;
        label.font = font;
        label.textColor = NSColor.blackColor;
        [label sizeToFit];  // width follows the text at this height

        // Padding around the text; the backdrop fills the window. visibleFrame is in global
        // screen coordinates, so use the initializer without a screen: argument (that variant
        // would treat the frame as relative to the screen).
        const CGFloat padding = wantedHeight * 0.15;
        const NSRect area = screen.visibleFrame;
        const NSSize size = NSMakeSize(label.frame.size.width + 2 * padding, label.frame.size.height + 2 * padding);
        const NSRect frame = NSMakeRect(NSMaxX(area) - size.width - kOverlayMargin,
                                        NSMaxY(area) - size.height - kOverlayMargin - screen.frame.size.height * topOffset,
                                        size.width, size.height);
        NSWindow *window = [[NSWindow alloc] initWithContentRect:frame
                                                       styleMask:NSWindowStyleMaskBorderless
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        window.level = NSStatusWindowLevel;
        window.backgroundColor = NSColor.clearColor;
        window.opaque = NO;
        window.hasShadow = NO;
        window.ignoresMouseEvents = YES;
        window.releasedWhenClosed = NO;
        window.hidesOnDeactivate = NO;
        window.alphaValue = alpha;
        window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorStationary |
                                    NSWindowCollectionBehaviorIgnoresCycle | NSWindowCollectionBehaviorFullScreenAuxiliary;
        NSView *content = window.contentView;
        content.wantsLayer = YES;
        content.layer.backgroundColor = [NSColor colorWithWhite:1.0 alpha:kOverlayBackdropAlpha].CGColor;
        content.layer.cornerRadius = padding;
        content.layer.masksToBounds = YES;
        label.frame = NSInsetRect(content.bounds, padding, padding);
        label.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [content addSubview:label];

        [_overlayWindows addObject:window];
        [_overlayLabels addObject:label];
        [_overlayPaddings addObject:@(padding)];
    }
}

- (void)screensChanged:(NSNotification *)notification {
    [self hideOverlay];
    [self destroyOverlayWindows];
    [self updateOverlay];
}

#pragma mark - Notifications

- (void)setupNotifications {
    if ([[NSBundle mainBundle] bundleIdentifier] == nil) {
        NSLog(@"Not running from an app bundle; system notifications are disabled.");
        return;
    }
    @try {
        UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];
        center.delegate = self;
        __weak AppDelegate *weakSelf = self;
        [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                              completionHandler:^(BOOL granted, NSError *error) {
            dispatch_async(dispatch_get_main_queue(), ^{
                AppDelegate *strongSelf = weakSelf;
                if (!strongSelf) return;
                strongSelf->_notificationsAuthorized = granted;
                if (error) NSLog(@"Notification authorization failed: %@", error);
                if (!granted) NSLog(@"Notifications not authorized; target alerts will use the dock and an in-app sheet.");
            });
        }];
    } @catch (NSException *exception) {
        NSLog(@"UNUserNotificationCenter unavailable: %@", exception);
    }
}

// Show banners even while Effort Manager is the frontmost app.
- (void)userNotificationCenter:(UNUserNotificationCenter *)center
       willPresentNotification:(UNNotification *)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler {
    completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList |
                      UNNotificationPresentationOptionSound);
}

- (void)announceTargetReached:(std::size_t)index now:(std::int64_t)now {
    if (index >= _model.size()) return;
    const effort::Component &c = _model.at(index);
    NSString *title = [NSString stringWithFormat:@"%@: target reached", ns(c.name)];
    NSString *body = [NSString stringWithFormat:@"You have spent %@ on %@ today (target %@).",
                                                ns(effort::formatHMS(c.elapsedSeconds(now))), ns(c.name),
                                                ns(effort::formatHM(c.targetSeconds))];
    [self showStatus:[NSString stringWithFormat:@"%@. %@", title, body]];

    NSBeep();
    [NSApp requestUserAttention:NSCriticalRequest];  // bounces the dock icon until you come back

    if (_notificationsAuthorized) {
        UNMutableNotificationContent *content = [[UNMutableNotificationContent alloc] init];
        content.title = title;
        content.body = body;
        content.sound = [UNNotificationSound defaultSound];
        NSString *identifier = [NSString stringWithFormat:@"effort-target-%@", [[NSUUID UUID] UUIDString]];
        UNNotificationRequest *request = [UNNotificationRequest requestWithIdentifier:identifier content:content trigger:nil];
        [[UNUserNotificationCenter currentNotificationCenter] addNotificationRequest:request
                                                               withCompletionHandler:^(NSError *error) {
            if (error) NSLog(@"Could not deliver notification: %@", error);
        }];
    } else {
        NSAlert *alert = [[NSAlert alloc] init];
        alert.messageText = title;
        alert.informativeText = body;
        [alert addButtonWithTitle:@"OK"];
        [alert beginSheetModalForWindow:_window completionHandler:nil];
    }
}

@end

int main(int argc, const char *argv[]) {
    (void)argc;
    (void)argv;
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        AppDelegate *delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
