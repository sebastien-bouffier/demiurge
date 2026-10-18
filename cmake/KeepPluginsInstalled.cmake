# keep_plugins_installed(NAME <name> TARGETS <target>... [VST3_DIR <dir>] [CLAP_DIR <dir>])
#
# Windows: after every build, the plugin folders get the VST3 and CLAP that build just made,
# even when nothing was relinked (switching between Debug and Release, a copy a host blocked
# last time...). The copy-after-build steps only run when a plugin is relinked; this adds a
# <name>_InstallPlugins step, part of every build, that copies whichever file differs.
# A copy that fails (a host holding the plugin open) warns and never fails the build.
# The folders default to C:/Program Files/Common Files/VST3 and .../CLAP.
# Other systems keep their own copy steps: this does nothing there.
function(keep_plugins_installed)
    cmake_parse_arguments(KPI "" "NAME;VST3_DIR;CLAP_DIR" "TARGETS" ${ARGN})
    if(NOT WIN32)
        return()
    endif()
    set(common_files "$ENV{CommonProgramW6432}")
    if(NOT common_files)
        set(common_files "$ENV{CommonProgramFiles}")
    endif()
    file(TO_CMAKE_PATH "${common_files}" common_files)
    if(NOT KPI_VST3_DIR)
        set(KPI_VST3_DIR "${common_files}/VST3")
    endif()
    if(NOT KPI_CLAP_DIR)
        set(KPI_CLAP_DIR "${common_files}/CLAP")
    endif()
    file(TO_CMAKE_PATH "${KPI_VST3_DIR}" KPI_VST3_DIR)
    file(TO_CMAKE_PATH "${KPI_CLAP_DIR}" KPI_CLAP_DIR)

    set(commands)
    set(built)
    foreach(target IN LISTS KPI_TARGETS)
        if(NOT TARGET ${target})
            continue()
        endif()
        string(TOUPPER "${target}" upper)
        if(upper MATCHES "VST3$")
            set(dest "${KPI_VST3_DIR}")
        elseif(upper MATCHES "CLAP$")
            set(dest "${KPI_CLAP_DIR}")
        else()
            continue()
        endif()
        list(APPEND built ${target})
        list(APPEND commands COMMAND "${CMAKE_COMMAND}" "-DSRC=$<TARGET_FILE:${target}>" "-DDEST=${dest}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/KeepPluginsInstalledRun.cmake")
    endforeach()
    if(NOT built)
        return()
    endif()
    add_custom_target(${KPI_NAME}_InstallPlugins ALL ${commands}
        COMMENT "Keeping ${KPI_NAME} up to date in the plugin folders"
        VERBATIM)
    add_dependencies(${KPI_NAME}_InstallPlugins ${built})
    message(STATUS "${KPI_NAME}: the VST3 goes to ${KPI_VST3_DIR}, the CLAP to ${KPI_CLAP_DIR}, after every build")
endfunction()
