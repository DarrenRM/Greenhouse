cmake_minimum_required(VERSION 3.16)
project(Greenhouse LANGUAGES CXX)
enable_testing()

# Force Visual Studio to use 64-bit architecture
if(MSVC)
    set(CMAKE_VS_PLATFORM_NAME "x64")
    set(CMAKE_GENERATOR_PLATFORM "x64")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# --- Qt path hint. Override with -DQT_DIR=... or CMAKE_PREFIX_PATH if needed. ---
set(QT_DIR "C:/Qt/6.9.0/msvc2022_64" CACHE PATH "Qt 6 installation prefix")
message(STATUS "Using Qt path hint: ${QT_DIR}")

# Add Qt paths directly to CMake path
set(CMAKE_PREFIX_PATH "${QT_DIR}")

# --- Fetch Dependencies ---
include(FetchContent)

# nlohmann/json (header-only)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# Find Qt components (notice we removed REQUIRED to avoid immediate failure)
find_package(Qt6 COMPONENTS Core Gui Widgets)

if(NOT Qt6_FOUND)
    message(FATAL_ERROR "Qt6 not found at: ${QT_DIR}. Please install Qt6 or correct the path.")
endif()

# Define the executable
add_executable(Greenhouse WIN32
    # Headers needing MOC
    include/WindowManager.h
    include/WindowMatcher.h
    include/BrowserFingerprint.h
    include/SavedWindowEntry.h
    include/LayoutGeometry.h
    include/SettingsManager.h
    include/WindowListModel.h
    include/WindowListDelegate.h
    include/MainWindow.h
    include/SettingsDialog.h
    include/BlackoutController.h
    include/MonitorBrightness.h
    # Source Files
    src/main.cpp
    src/Win32Helper.cpp
    src/BrowserFingerprint.cpp
    src/LayoutGeometry.cpp
    src/WindowMatcher.cpp
    src/WindowManager.cpp
    src/SettingsManager.cpp
    src/WindowListModel.cpp
    src/WindowListDelegate.cpp
    src/MainWindow.cpp
    src/SettingsDialog.cpp
    src/BlackoutController.cpp
    src/MonitorBrightness.cpp
    # Resource File
    resources.qrc
    greenhouse.rc
)

# Link libraries
target_link_libraries(Greenhouse PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    # Win32 Libraries
    user32
    gdi32
    dwmapi
    psapi
    shell32
    advapi32
    Shcore
    uiautomationcore
    ole32
    oleaut32
    dxva2
)

# Include directories
target_include_directories(Greenhouse PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${nlohmann_json_SOURCE_DIR}/include
) 

add_executable(GreenhouseLayoutTests
    include/WindowPosition.h
    include/LayoutGeometry.h
    src/LayoutGeometry.cpp
    tests/LayoutGeometryTests.cpp
)

add_executable(GreenhouseWindowMatcherTests
    include/WindowInfo.h
    include/WindowPosition.h
    include/BrowserFingerprint.h
    include/SavedWindowEntry.h
    include/WindowMatcher.h
    src/BrowserFingerprint.cpp
    src/WindowMatcher.cpp
    tests/WindowMatcherTests.cpp
)

add_executable(GreenhouseSettingsManagerTests
    include/WindowInfo.h
    include/WindowPosition.h
    include/BrowserFingerprint.h
    include/SavedWindowEntry.h
    include/SettingsManager.h
    src/BrowserFingerprint.cpp
    src/SettingsManager.cpp
    tests/SettingsManagerTests.cpp
)

add_executable(GreenhouseWindowListModelTests
    include/WindowInfo.h
    include/WindowListModel.h
    include/WindowListDelegate.h
    src/WindowListModel.cpp
    src/WindowListDelegate.cpp
    tests/WindowListModelTests.cpp
)

add_executable(GreenhouseLauncher WIN32
    tools/GreenhouseLauncher.cpp
    greenhouse.rc
)

target_link_libraries(GreenhouseLayoutTests PRIVATE
    Qt6::Core
)

target_link_libraries(GreenhouseWindowMatcherTests PRIVATE
    Qt6::Core
    Qt6::Gui
    user32
    uiautomationcore
    ole32
    oleaut32
)

target_link_libraries(GreenhouseSettingsManagerTests PRIVATE
    Qt6::Core
    Qt6::Gui
    user32
    uiautomationcore
    ole32
    oleaut32
)

target_link_libraries(GreenhouseWindowListModelTests PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    user32
)

target_include_directories(GreenhouseLayoutTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${nlohmann_json_SOURCE_DIR}/include
)

target_include_directories(GreenhouseWindowMatcherTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${nlohmann_json_SOURCE_DIR}/include
)

target_include_directories(GreenhouseSettingsManagerTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${nlohmann_json_SOURCE_DIR}/include
)

target_include_directories(GreenhouseWindowListModelTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${nlohmann_json_SOURCE_DIR}/include
)

add_test(NAME GreenhouseLayoutTests COMMAND GreenhouseLayoutTests)
add_test(NAME GreenhouseWindowMatcherTests COMMAND GreenhouseWindowMatcherTests)
add_test(NAME GreenhouseSettingsManagerTests COMMAND GreenhouseSettingsManagerTests)
add_test(NAME GreenhouseWindowListModelTests COMMAND GreenhouseWindowListModelTests)
set_tests_properties(GreenhouseLayoutTests PROPERTIES
    ENVIRONMENT "PATH=${QT_DIR}/bin;$ENV{PATH}"
)
set_tests_properties(GreenhouseWindowMatcherTests PROPERTIES
    ENVIRONMENT "PATH=${QT_DIR}/bin;$ENV{PATH}"
)
set_tests_properties(GreenhouseSettingsManagerTests PROPERTIES
    ENVIRONMENT "PATH=${QT_DIR}/bin;$ENV{PATH}"
)
set_tests_properties(GreenhouseWindowListModelTests PROPERTIES
    ENVIRONMENT "PATH=${QT_DIR}/bin;$ENV{PATH}"
)
