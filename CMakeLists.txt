cmake_minimum_required(VERSION 3.24)
project(omarchy-quicklook VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Qml Quick DBus Multimedia Svg Concurrent Network Test)
find_package(KF6SyntaxHighlighting 6.29 REQUIRED)
find_package(KF6Archive 6.29 REQUIRED)
find_package(taglib 2.3 REQUIRED)
find_package(libheif REQUIRED)
find_package(uchardet REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(POPPLER REQUIRED IMPORTED_TARGET poppler-qt6)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
pkg_check_modules(LIBRAW REQUIRED IMPORTED_TARGET libraw)

qt_standard_project_setup(REQUIRES 6.11)

add_library(qlcore STATIC)
target_include_directories(qlcore PUBLIC src/core)
target_compile_definitions(qlcore PUBLIC QT_NO_KEYWORDS)
target_link_libraries(qlcore PUBLIC
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::DBus Qt6::Multimedia Qt6::Svg Qt6::Concurrent Qt6::Network
    KF6::SyntaxHighlighting KF6::Archive TagLib::tag heif uchardet::libuchardet
    PkgConfig::POPPLER PkgConfig::GIO PkgConfig::LIBRAW)

add_executable(omarchy-quicklook src/app/main.cpp)
target_link_libraries(omarchy-quicklook PRIVATE qlcore)
install(TARGETS omarchy-quicklook RUNTIME DESTINATION bin)

enable_testing()
set(QL_TEST_HOME "${CMAKE_BINARY_DIR}/test-home")
function(ql_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE qlcore Qt6::Test)
    target_compile_definitions(${name} PRIVATE QL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT_MODIFICATION "QT_QPA_PLATFORM=set:offscreen;QT_QPA_PLATFORMTHEME=unset:;HYPRLAND_INSTANCE_SIGNATURE=unset:;XDG_CACHE_HOME=set:${QL_TEST_HOME}/cache;XDG_STATE_HOME=set:${QL_TEST_HOME}/state;XDG_CONFIG_HOME=set:${QL_TEST_HOME}/config;XDG_DATA_HOME=set:${QL_TEST_HOME}/data;QT_QPA_OFFSCREEN_NO_GLX=set:1;QT_QUICK_BACKEND=set:software"
        LABELS "${ARGN}")
endfunction()

# ---- sources and tests: every task appends its own lines below ----
target_sources(qlcore PRIVATE src/core/Types.h src/core/Types.cpp src/core/Pools.h src/core/Pools.cpp)
ql_test(tst_types)
