# FxSound — Linux/CMake build
#
# This build targets Linux (PipeWire). It replicates the Projucer-exported
# build for the JUCE GUI app and links the DSP + audio-passthru components.
# On Windows continue to use the Visual Studio solutions under fxsound/Project.

cmake_minimum_required(VERSION 3.20)
project(FxSound VERSION 1.2.8.0 LANGUAGES C CXX)
set(FXSOUND_LINUX_REVISION 2)
set(FXSOUND_LINUX_VERSION "${PROJECT_VERSION}-linux.${FXSOUND_LINUX_REVISION}")

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This CMake build is Linux-only. Use the VS solutions on Windows.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# ---- Options --------------------------------------------------------------
option(FXSOUND_STUB_DSP        "Use stub DfxDsp instead of the ported engine"    OFF)
option(FXSOUND_STUB_AUDIO      "Use stub AudioPassthru instead of PipeWire"       OFF)
set(FXSOUND_SANITIZER "none" CACHE STRING "Linux instrumentation: none, address, or thread")
set_property(CACHE FXSOUND_SANITIZER PROPERTY STRINGS none address thread)
if(NOT FXSOUND_SANITIZER STREQUAL "none")
    if(NOT FXSOUND_SANITIZER MATCHES "^(address|thread)$")
        message(FATAL_ERROR "Unsupported FXSOUND_SANITIZER=${FXSOUND_SANITIZER}")
    endif()
    add_compile_options(-fsanitize=${FXSOUND_SANITIZER} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${FXSOUND_SANITIZER})
endif()

enable_testing()

set(FX_ROOT       ${CMAKE_CURRENT_SOURCE_DIR})
set(JUCE_DIR      ${FX_ROOT}/third_party/JUCE)
set(JUCE_MODULES  ${JUCE_DIR}/modules)
set(FX_GUI        ${FX_ROOT}/fxsound/Source)
set(FX_JLC        ${FX_ROOT}/fxsound/JuceLibraryCode)

if(NOT EXISTS ${JUCE_MODULES}/juce_core/juce_core.h)
    message(FATAL_ERROR "JUCE 6.1.6 modules not found at ${JUCE_MODULES}. "
                        "Run: git clone --depth 1 --branch 6.1.6 "
                        "https://github.com/juce-framework/JUCE third_party/JUCE")
endif()

# ===========================================================================
#  DfxDsp library
# ===========================================================================
if(FXSOUND_STUB_DSP)
    add_library(DfxDsp STATIC ${FX_ROOT}/linux/stubs/DfxDsp_stub.cpp)
    target_include_directories(DfxDsp PUBLIC ${FX_ROOT}/dsp/include)
else()

    include(${FX_ROOT}/dsp/DfxDsp.cmake)
endif()

# ===========================================================================
#  AudioPassthru library
# ===========================================================================
if(FXSOUND_STUB_AUDIO)
    add_library(AudioPassthru STATIC ${FX_ROOT}/linux/stubs/AudioPassthru_stub.cpp)
    target_include_directories(AudioPassthru PUBLIC ${FX_ROOT}/audiopassthru/include)
    target_link_libraries(AudioPassthru PUBLIC DfxDsp)
else()

    include(${FX_ROOT}/audiopassthru/AudioPassthruLinux.cmake)
endif()

# ===========================================================================
#  FxSound GUI app (JUCE)  — replicates the Projucer Linux export
# ===========================================================================
find_package(PkgConfig REQUIRED)
pkg_check_modules(GUI_DEPS REQUIRED
    x11 xext xinerama xrandr xcursor
    freetype2 fontconfig gl alsa
    xcb xcb-keysyms)
# GIO/GDBus for the StatusNotifierItem tray.
pkg_check_modules(GIO REQUIRED gio-2.0)
find_package(Threads REQUIRED)

file(GLOB_RECURSE FX_GUI_SOURCES CONFIGURE_DEPENDS ${FX_GUI}/*.cpp)
# MainComponent.* is a leftover Projucer default (references a missing
# AnnouncementComponent.h) and is not part of the app — exclude it.
list(FILTER FX_GUI_SOURCES EXCLUDE REGEX "MainComponent\\.cpp$")
# SysInfo.cpp is COM/WASAPI based; the Linux reimplementation replaces it.
list(FILTER FX_GUI_SOURCES EXCLUDE REGEX "SysInfo/SysInfo\\.cpp$")
list(APPEND FX_GUI_SOURCES ${FX_ROOT}/linux/SysInfo_linux.cpp)

set(JUCE_WRAPPERS
    ${FX_JLC}/include_juce_core.cpp
    ${FX_JLC}/include_juce_cryptography.cpp
    ${FX_JLC}/include_juce_data_structures.cpp
    ${FX_JLC}/include_juce_events.cpp
    ${FX_JLC}/include_juce_graphics.cpp
    ${FX_JLC}/include_juce_gui_basics.cpp
    ${FX_JLC}/include_juce_gui_extra.cpp
    ${FX_JLC}/include_juce_opengl.cpp)

add_executable(FxSound
    ${FX_GUI_SOURCES}
    ${JUCE_WRAPPERS}
    ${FX_JLC}/BinaryData.cpp
    ${FX_ROOT}/linux/tray/fx_tray_sni.cpp
    ${FX_ROOT}/linux/LinuxHotkeys.cpp
    ${FX_ROOT}/linux/PortalHotkeys.cpp
    ${FX_ROOT}/linux/Autostart.cpp)

set_property(SOURCE ${FX_GUI}/GUI/FxTheme.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    FXSOUND_SOURCE_FONT_DIR="${FX_ROOT}/fxsound/Fonts")

target_include_directories(FxSound PRIVATE
    ${FX_JLC}
    ${FX_GUI}
    ${FX_GUI}/GUI
    ${FX_GUI}/Utils/SysInfo
    ${FX_ROOT}/linux
    ${FX_ROOT}/linux/tray
    ${JUCE_MODULES}
    ${GIO_INCLUDE_DIRS})

# JUCE module configuration. AppConfig.h carries the module-availability flags;
# here we set the platform/runtime flags the Projucer would inject.
target_compile_definitions(FxSound PRIVATE
    LINUX=1
    JUCE_APP_VERSION=1.2.8.0
    JUCE_APP_VERSION_HEX=0x1020800
    FXSOUND_LINUX_VERSION="${FXSOUND_LINUX_VERSION}"
    JUCER_LINUX_MAKE=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    # Last-resort factory preset location for builds run without installing.
    FXSOUND_SOURCE_PRESET_DIR="${FX_ROOT}/Installer/Resources/Factsoft"
    $<$<CONFIG:Debug>:DEBUG=1 _DEBUG=1>
    $<$<CONFIG:Release>:NDEBUG=1>)

target_link_libraries(FxSound PRIVATE
    DfxDsp
    AudioPassthru
    ${GUI_DEPS_LIBRARIES}
    ${GIO_LIBRARIES}
    Threads::Threads
    ${CMAKE_DL_LIBS})

target_link_directories(FxSound PRIVATE ${GUI_DEPS_LIBRARY_DIRS})
target_include_directories(FxSound PRIVATE ${GUI_DEPS_INCLUDE_DIRS})
target_compile_options(FxSound PRIVATE ${GUI_DEPS_CFLAGS_OTHER})

# ===========================================================================
#  Install — standard freedesktop layout
# ===========================================================================
include(GNUInstallDirs)

# Install the binary as lowercase "fxsound" (matches the .desktop Exec and the
# Linux convention); the in-tree build artifact keeps its Projucer name.
install(PROGRAMS $<TARGET_FILE:FxSound>
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        RENAME fxsound)

install(FILES ${FX_ROOT}/packaging/fxsound.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

install(FILES ${FX_ROOT}/fxsound/Images/fxsound_large.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
        RENAME fxsound.png)
install(FILES ${FX_ROOT}/fxsound/Images/fxsound.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/32x32/apps
        RENAME fxsound.png)
install(CODE "execute_process(COMMAND gtk-update-icon-cache -qtf \
    \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/icons/hicolor\" \
    ERROR_QUIET)")

# Factory presets: the app loads these from <prefix>/share/fxsound/Factsoft
# (see FxController::getFactoryPresetDir).
if(EXISTS ${FX_ROOT}/Installer/Resources/Factsoft)
    install(DIRECTORY ${FX_ROOT}/Installer/Resources/Factsoft
            DESTINATION ${CMAKE_INSTALL_DATADIR}/fxsound)
endif()
if(EXISTS ${FX_ROOT}/bin/BonusPresets)
    install(DIRECTORY ${FX_ROOT}/bin/BonusPresets
            DESTINATION ${CMAKE_INSTALL_DATADIR}/fxsound)
endif()

# Live: quitting via SIGTERM (logout/shutdown) hands the default sink back.
add_test(NAME app_exit_restores_default_sink
    COMMAND bash ${FX_ROOT}/audiopassthru/src/linux/test_app_exit_live.sh $<TARGET_FILE:FxSound> TERM)
set_tests_properties(app_exit_restores_default_sink PROPERTIES
    LABELS pw-live SKIP_RETURN_CODE 77 RUN_SERIAL TRUE TIMEOUT 60)

# Live: the FxSound sink volume (volume keys) is applied to what we process.
add_test(NAME app_sink_volume_applies
    COMMAND bash ${FX_ROOT}/audiopassthru/src/linux/test_app_volume_live.sh $<TARGET_FILE:FxSound>)
set_tests_properties(app_sink_volume_applies PROPERTIES
    LABELS pw-live SKIP_RETURN_CODE 77 RUN_SERIAL TRUE TIMEOUT 60)

# Live: with power on, hot-plugged sinks appear in and leave the tray output menu.
add_test(NAME app_hotplug_updates_output_menu
    COMMAND bash ${FX_ROOT}/audiopassthru/src/linux/test_app_hotplug_live.sh $<TARGET_FILE:FxSound>)
set_tests_properties(app_hotplug_updates_output_menu PROPERTIES
    LABELS pw-live SKIP_RETURN_CODE 77 RUN_SERIAL TRUE TIMEOUT 60)

add_test(NAME app_xdg_config
    COMMAND bash ${FX_ROOT}/linux/tests/test_xdg_config.sh $<TARGET_FILE:FxSound>)
set_tests_properties(app_xdg_config PROPERTIES
    LABELS pw-live SKIP_RETURN_CODE 77 RUN_SERIAL TRUE TIMEOUT 30)

install(FILES ${FX_ROOT}/LICENSE ${FX_ROOT}/CREDITS.md ${FX_ROOT}/THIRD_PARTY_NOTICES.md ${FX_ROOT}/MODIFICATIONS.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/fxsound)
install(FILES ${JUCE_DIR}/LICENSE.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/fxsound
        RENAME JUCE-LICENSE.md)
install(DIRECTORY ${FX_ROOT}/LICENSES DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/fxsound)
install(DIRECTORY ${FX_ROOT}/fxsound/Fonts
        DESTINATION ${CMAKE_INSTALL_DATADIR}/fxsound
        FILES_MATCHING PATTERN "*.ttf" PATTERN "*.otf")

# Stub builds are development aids and must never become installable releases.
if(NOT FXSOUND_STUB_DSP AND NOT FXSOUND_STUB_AUDIO AND FXSOUND_SANITIZER STREQUAL "none")
    include(${FX_ROOT}/packaging/debian.cmake)
endif()

# Parse and launch generated Desktop Entries through the actual GIO launcher.
pkg_check_modules(GIO_UNIX REQUIRED gio-unix-2.0)
add_executable(autostart_probe linux/tests/test_autostart_probe.cpp linux/Autostart.cpp)
target_include_directories(autostart_probe PRIVATE linux ${GIO_UNIX_INCLUDE_DIRS})
target_link_libraries(autostart_probe PRIVATE ${GIO_UNIX_LIBRARIES})
add_test(NAME desktop_autostart
    COMMAND python3 ${FX_ROOT}/linux/tests/test_autostart.py $<TARGET_FILE:autostart_probe>)
set_tests_properties(desktop_autostart PROPERTIES LABELS unit TIMEOUT 30)
add_test(NAME login_starts_minimized
    COMMAND bash ${FX_ROOT}/linux/tests/test_login_minimized.sh $<TARGET_FILE:FxSound>)
set_tests_properties(login_starts_minimized PROPERTIES LABELS pw-live RUN_SERIAL TRUE SKIP_RETURN_CODE 77 TIMEOUT 30)
add_test(NAME desktop_client_audio
    COMMAND bash ${FX_ROOT}/linux/tests/test_desktop_clients.sh $<TARGET_FILE:FxSound>)
set_tests_properties(desktop_client_audio PROPERTIES LABELS desktop-clients RUN_SERIAL TRUE SKIP_RETURN_CODE 77 TIMEOUT 180)
add_test(NAME appimage_login_gui
    COMMAND bash ${FX_ROOT}/linux/tests/test_login_gui.sh
        ${FX_ROOT}/dist/FxSound-${FXSOUND_LINUX_VERSION}-${CMAKE_SYSTEM_PROCESSOR}.AppImage $<TARGET_FILE:autostart_probe>)
set_tests_properties(appimage_login_gui PROPERTIES LABELS appimage-login RUN_SERIAL TRUE SKIP_RETURN_CODE 77 TIMEOUT 60)
add_test(NAME private_cleanup_scope COMMAND python3 ${FX_ROOT}/linux/tests/test_cleanup_scoping.py)
set_tests_properties(private_cleanup_scope PROPERTIES LABELS unit TIMEOUT 10)

add_executable(portal_hotkeys_probe linux/tests/test_portal_probe.cpp linux/PortalHotkeys.cpp)
target_include_directories(portal_hotkeys_probe PRIVATE linux ${GIO_INCLUDE_DIRS})
target_link_libraries(portal_hotkeys_probe PRIVATE ${GIO_LIBRARIES} pthread)
add_test(NAME portal_hotkeys_protocol COMMAND dbus-run-session -- env FXSOUND_PRIVATE_PORTAL_TEST=1
    /usr/bin/python3 ${FX_ROOT}/linux/tests/test_portal_hotkeys.py $<TARGET_FILE:portal_hotkeys_probe>)
set_tests_properties(portal_hotkeys_protocol PROPERTIES LABELS unit TIMEOUT 30)
add_executable(wfopen_selftest linux/tests/test_wfopen.cpp)
target_include_directories(wfopen_selftest PRIVATE dsp/linux)
target_compile_options(wfopen_selftest PRIVATE -UNDEBUG)
add_test(NAME wfopen_utf8 COMMAND wfopen_selftest)
set_tests_properties(wfopen_utf8 PROPERTIES LABELS unit TIMEOUT 10)

add_executable(tray_probe linux/tests/test_tray_probe.cpp linux/tray/fx_tray_sni.cpp)
target_include_directories(tray_probe PRIVATE linux/tray ${GIO_INCLUDE_DIRS})
target_link_libraries(tray_probe PRIVATE ${GIO_LIBRARIES} Threads::Threads)
add_test(NAME tray_lifecycle COMMAND dbus-run-session -- /usr/bin/python3
    ${FX_ROOT}/linux/tests/test_tray_lifecycle.py $<TARGET_FILE:tray_probe>)
set_tests_properties(tray_lifecycle PROPERTIES LABELS unit TIMEOUT 45)
add_test(NAME crash_default_recovery COMMAND python3
    ${FX_ROOT}/linux/tests/test_crash_recovery.py $<TARGET_FILE:graph_probe>)
set_tests_properties(crash_default_recovery PROPERTIES
    LABELS pw-live RUN_SERIAL TRUE SKIP_RETURN_CODE 77 TIMEOUT 60)
add_executable(default_recovery_selftest linux/tests/test_default_recovery.cpp)
target_include_directories(default_recovery_selftest PRIVATE audiopassthru/src/linux)
target_compile_options(default_recovery_selftest PRIVATE -UNDEBUG)
add_test(NAME default_recovery_storage COMMAND default_recovery_selftest)
set_tests_properties(default_recovery_storage PROPERTIES LABELS unit TIMEOUT 10)
add_test(NAME release_source_notices COMMAND python3 ${FX_ROOT}/packaging/verify-release-source.py)
set_tests_properties(release_source_notices PROPERTIES LABELS unit TIMEOUT 10)
