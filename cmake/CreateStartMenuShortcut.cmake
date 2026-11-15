set(_lumetra_executable "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/Lumetra.exe")

execute_process(
    COMMAND powershell.exe
        -NoProfile
        -File "${CMAKE_CURRENT_LIST_DIR}/CreateStartMenuShortcut.ps1"
        -Executable "${_lumetra_executable}"
    RESULT_VARIABLE _shortcut_result
)

if(NOT _shortcut_result EQUAL 0)
    message(FATAL_ERROR "Failed to create the Lumetra Start Menu shortcut.")
endif()
