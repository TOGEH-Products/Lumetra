cmake_minimum_required(VERSION 3.20)

project(Lumetra
    VERSION 0.2.0
    DESCRIPTION "Automatic display color profiles for games"
    LANGUAGES CXX RC
)

set(LUMETRA_GITHUB_OWNER "" CACHE STRING "Official GitHub release owner")
set(LUMETRA_GITHUB_REPO "" CACHE STRING "Official GitHub release repository")
if(NOT LUMETRA_GITHUB_OWNER MATCHES "^[A-Za-z0-9._-]*$" OR
   NOT LUMETRA_GITHUB_REPO MATCHES "^[A-Za-z0-9._-]*$")
    message(FATAL_ERROR "The Lumetra release owner and repository may contain only letters, digits, '.', '_' or '-'.")
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/updates")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/UpdateConfig.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/updates/UpdateConfig.h"
    @ONLY
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Source files
set(LUMETRA_SOURCES
    src/main.cpp
    src/app/Application.h
    src/app/Application.cpp
    src/detection/ProcessHelper.h
    src/detection/ProcessHelper.cpp
    src/detection/ForegroundDetector.h
    src/detection/ForegroundDetector.cpp
    src/display/GammaRamp.h
    src/display/GammaRamp.cpp
    src/display/IColorEngine.h
    src/display/MagnificationEngine.h
    src/display/MagnificationEngine.cpp
    src/display/Win32GammaEngine.h
    src/display/Win32GammaEngine.cpp
    src/display/DisplayManager.h
    src/display/DisplayManager.cpp
    src/display/DisplayDevices.h
    src/display/DisplayDevices.cpp
    src/display/ResolutionManager.h
    src/display/ResolutionManager.cpp
    src/profiles/ColorProfile.h
    src/profiles/ProfileManager.h
    src/profiles/ProfileManager.cpp
    src/profiles/ProfileStorage.h
    src/profiles/ProfileStorage.cpp
    src/profiles/BuiltinGames.h
    src/profiles/BuiltinGames.cpp
    src/screenshots/ScreenshotCapture.h
    src/screenshots/ScreenshotCapture.cpp
    src/screenshots/HotkeyManager.h
    src/screenshots/HotkeyManager.cpp
    src/screenshots/ScreenshotManager.h
    src/screenshots/ScreenshotManager.cpp
    src/updates/SemanticVersion.h
    src/updates/SemanticVersion.cpp
    src/updates/ReleaseMetadata.h
    src/updates/ReleaseMetadata.cpp
    src/updates/UpdateService.h
    src/updates/UpdateService.cpp
    src/ui/UITheme.h
    src/ui/Renderer.h
    src/ui/Renderer.cpp
    src/ui/DirectWriteText.h
    src/ui/DirectWriteText.cpp
    src/ui/Anim.h
    src/ui/Icons.h
    src/ui/Icons.cpp
    src/ui/Controls.h
    src/ui/Controls.cpp
    src/ui/SettingsView.h
    src/ui/SettingsView.cpp
    src/ui/HotkeyDialog.h
    src/ui/HotkeyDialog.cpp
    src/ui/HotkeyCapture.h
    src/ui/HotkeyCapture.cpp
    src/ui/TrayIcon.h
    src/ui/TrayIcon.cpp
    src/ui/ProfileListView.h
    src/ui/ProfileListView.cpp
    src/ui/ProfileEditorView.h
    src/ui/ProfileEditorView.cpp
    src/ui/AddAppDialog.h
    src/ui/AddAppDialog.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/utilities/NonCopyable.h
    src/utilities/Logger.h
    src/utilities/Logger.cpp
    src/utilities/AppIcon.h
    src/utilities/AppIcon.cpp
    src/utilities/JsonHelper.h
    src/utilities/JsonHelper.cpp
    src/utilities/StartupManager.h
    src/utilities/StartupManager.cpp
    src/utilities/Preferences.h
    src/utilities/Preferences.cpp
    Lumetra.rc
)

# Executable target
#
# The version resource supplies file metadata. The named Start Menu shortcut
# installed below gives Windows Search a friendly app entry.
add_executable(Lumetra WIN32 ${LUMETRA_SOURCES})

target_include_directories(Lumetra PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

# Compiler flags
if(MSVC)
    target_compile_options(Lumetra PRIVATE
        /W4
        /permissive-
        /utf-8
        /EHsc
    )
    target_compile_definitions(Lumetra PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    # Win32 subsystem for clean background execution
    set_target_properties(Lumetra PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

# Win32 libraries
target_link_libraries(Lumetra PRIVATE
    user32
    gdi32
    kernel32
    shell32
    comdlg32
    dwmapi
    advapi32
    shlwapi
    d2d1
    dwrite
    dxgi
    dxguid
    winmm
    winhttp
    bcrypt
    ole32
)

add_custom_command(TARGET Lumetra POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Sounds"
        "$<TARGET_FILE_DIR:Lumetra>/Sounds"
    COMMENT "Copying screenshot notification sounds"
)

install(TARGETS Lumetra RUNTIME DESTINATION ".")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Sounds/" DESTINATION "Sounds")
install(SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CreateStartMenuShortcut.cmake")

include(CTest)
if(BUILD_TESTING)
    add_executable(LumetraUpdateTests
        tests/UpdateTests.cpp
        src/updates/SemanticVersion.cpp
        src/updates/ReleaseMetadata.cpp
        src/utilities/JsonHelper.cpp
    )
    target_include_directories(LumetraUpdateTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_compile_definitions(LumetraUpdateTests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    if(MSVC)
        target_compile_options(LumetraUpdateTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME LumetraUpdateTests COMMAND LumetraUpdateTests)

    add_executable(LumetraProfileStorageTests
        tests/ProfileStorageTests.cpp
    )
    target_include_directories(LumetraProfileStorageTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    if(MSVC)
        target_compile_options(LumetraProfileStorageTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME LumetraProfileStorageTests COMMAND LumetraProfileStorageTests)
endif()
