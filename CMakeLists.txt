cmake_minimum_required(VERSION 3.21)

# Must be set before project() so it applies to every target.
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

project(EffortManager VERSION 1.0.0 LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---- Core: pure C++, no Apple frameworks ------------------------------------
add_library(effort_core STATIC
  src/core/EffortModel.cpp
  src/core/Json.cpp
  src/core/Store.cpp
  src/core/TimeFormat.cpp
)
target_include_directories(effort_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
target_compile_options(effort_core PRIVATE -Wall -Wextra)

# ---- App bundle: Cocoa front-end (Objective-C++) ----------------------------
add_executable(EffortManager MACOSX_BUNDLE src/mac/main.mm)
target_link_libraries(EffortManager PRIVATE
  effort_core
  "-framework Cocoa"
  "-framework UserNotifications"
)
target_compile_options(EffortManager PRIVATE -Wall -Wextra -Wno-unused-parameter -fobjc-arc)
set_target_properties(EffortManager PROPERTIES
  OUTPUT_NAME "EffortManager"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/mac/Info.plist.in"
  MACOSX_BUNDLE_BUNDLE_NAME "EffortManager"
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.zehaoduan.effortmanager"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
)

# Ad-hoc sign the bundle so the notification center accepts it. Xcode signs on its own.
if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
  add_custom_command(TARGET EffortManager POST_BUILD
    COMMAND xattr -cr "$<TARGET_BUNDLE_DIR:EffortManager>"
    COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:EffortManager>"
    COMMENT "Ad-hoc signing EffortManager.app (Finder metadata stripped first)"
    VERBATIM
  )
endif()

# ---- Tests for the core -----------------------------------------------------
enable_testing()
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE effort_core)
target_compile_options(core_tests PRIVATE -Wall -Wextra)
add_test(NAME core_tests COMMAND core_tests)
