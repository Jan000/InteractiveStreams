if(NOT DEFINED INPUT_DIR OR NOT DEFINED OUTPUT_DIR OR NOT DEFINED STAMP_FILE)
    message(FATAL_ERROR "CopyIfStale.cmake requires INPUT_DIR, OUTPUT_DIR and STAMP_FILE")
endif()

if(NOT EXISTS "${INPUT_DIR}")
    return()
endif()

file(GLOB_RECURSE _copy_inputs LIST_DIRECTORIES false "${INPUT_DIR}/*")
if(DEFINED INPUT_STAMP AND EXISTS "${INPUT_STAMP}")
    list(APPEND _copy_inputs "${INPUT_STAMP}")
endif()

set(_needs_copy FALSE)
if(NOT EXISTS "${STAMP_FILE}" OR NOT EXISTS "${OUTPUT_DIR}")
    set(_needs_copy TRUE)
else()
    file(TIMESTAMP "${STAMP_FILE}" _stamp_ts UTC "%Y%m%d%H%M%S")
    foreach(_copy_input IN LISTS _copy_inputs)
        if(NOT EXISTS "${_copy_input}")
            continue()
        endif()

        file(TIMESTAMP "${_copy_input}" _input_ts UTC "%Y%m%d%H%M%S")
        if(_input_ts STRGREATER _stamp_ts)
            set(_needs_copy TRUE)
            break()
        endif()
    endforeach()
endif()

if(_needs_copy)
    file(REMOVE_RECURSE "${OUTPUT_DIR}")
    get_filename_component(_output_parent "${OUTPUT_DIR}" DIRECTORY)
    file(MAKE_DIRECTORY "${_output_parent}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${INPUT_DIR}" "${OUTPUT_DIR}"
        COMMAND_ERROR_IS_FATAL ANY
    )
    file(TOUCH "${STAMP_FILE}")
endif()