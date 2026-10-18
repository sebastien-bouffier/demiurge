# Run by keep_plugins_installed (KeepPluginsInstalled.cmake): puts SRC (a plugin file, or the
# file inside a .vst3 bundle) into DEST, copying only what differs. Warns, never fails.

get_filename_component(src "${SRC}" ABSOLUTE)

# The file inside a bundle (X.vst3/Contents/x86_64-win/X.vst3) stands for the whole bundle.
get_filename_component(arch_dir "${src}" DIRECTORY)
get_filename_component(contents_dir "${arch_dir}" DIRECTORY)
get_filename_component(contents_name "${contents_dir}" NAME)
if(NOT IS_DIRECTORY "${src}" AND contents_name STREQUAL "Contents")
    get_filename_component(src "${contents_dir}" DIRECTORY)
endif()
if(NOT EXISTS "${src}")
    message(WARNING "Nothing to install: ${src} was not built")
    return()
endif()
get_filename_component(name "${src}" NAME)
set(out "${DEST}/${name}")

execute_process(COMMAND "${CMAKE_COMMAND}" -E make_directory "${DEST}" RESULT_VARIABLE result)
if(result EQUAL 0)
    if(IS_DIRECTORY "${src}")
        # A plain file where the bundle goes (an old single-file VST3) is replaced.
        if(EXISTS "${out}" AND NOT IS_DIRECTORY "${out}")
            execute_process(COMMAND "${CMAKE_COMMAND}" -E rm -f "${out}")
        endif()
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.26)
            execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different "${src}" "${out}"
                RESULT_VARIABLE result)
        else()
            execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_directory "${src}" "${out}"
                RESULT_VARIABLE result)
        endif()
    else()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${src}" "${out}"
            RESULT_VARIABLE result)
    endif()
endif()

if(result EQUAL 0)
    message(STATUS "${name} is up to date in ${DEST}")
else()
    message(WARNING "Could not copy ${name} to ${DEST}: close the host that has it loaded, "
        "or build with the rights to write there (Visual Studio or build-all.bat as administrator).")
endif()
