# Python tools are run through uv, which provisions the interpreter and the
# dependencies from the pyproject.toml/uv.lock in the project root.
find_program(UV_EXECUTABLE uv REQUIRED)

# Select platform settings file based on compiler/OS
if(WIN32)
    set(PLATFORM_SETTINGS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/platform_settings_msvc.json")
else()
    # Use GCC settings for non-MSVC compilers (GCC, Clang on Linux/macOS)
    set(PLATFORM_SETTINGS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/platform_settings_gcc.json")
endif()

message(STATUS "Using platform settings: ${PLATFORM_SETTINGS_FILE}")

#------------------------------------------------------------------------------
# Defines the targets of a test configuration, generated from DATAMODEL:
#   mEEM-GenConfig<SUFFIX>       - Generated configuration files
#   mEEM-UserConfig<SUFFIX>      - User-written configuration files
#   mEEM-Config<SUFFIX>          - Combined interface
#   generate-meem-config<SUFFIX> - Explicit generation
#------------------------------------------------------------------------------
function(meem_add_test_config SUFFIX DATAMODEL)
    # Define the generated source files (in build directory)
    set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated${SUFFIX}")
    set(GENERATED_FILES
        ${GENERATED_DIR}/MEEM_GenConfig.h
        ${GENERATED_DIR}/MEEM_GenConfig.c
        ${GENERATED_DIR}/MEEM_GenInterface.h
        ${GENERATED_DIR}/MEEM_GenInterface.c
    )

    # Define the input files that the generator depends on
    set(GENERATOR_INPUTS
        ${CMAKE_CURRENT_SOURCE_DIR}/${DATAMODEL}
        ${PLATFORM_SETTINGS_FILE}
        ${CMAKE_SOURCE_DIR}/tools/meem_config_gen/meem_config_gen.py
        ${CMAKE_SOURCE_DIR}/uv.lock
    )

    # Create the generated directory
    file(MAKE_DIRECTORY ${GENERATED_DIR})

    # Custom command to generate mEEM configuration files
    add_custom_command(
        OUTPUT ${GENERATED_FILES}
        COMMAND ${UV_EXECUTABLE} run --project ${CMAKE_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/tools/meem_config_gen/meem_config_gen.py
            ${CMAKE_CURRENT_SOURCE_DIR}/${DATAMODEL}
            ${PLATFORM_SETTINGS_FILE}
            ${GENERATED_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS ${GENERATOR_INPUTS}
        COMMENT "Generating mEEM configuration files to ${GENERATED_DIR}"
        VERBATIM
    )

    # mEEM-GenConfig: Generated configuration files
    add_library(mEEM-GenConfig${SUFFIX})

    target_sources(mEEM-GenConfig${SUFFIX}
        PRIVATE
            ${GENERATED_FILES}
    )

    target_include_directories(mEEM-GenConfig${SUFFIX}
        PUBLIC
            ${GENERATED_DIR}
    )

    target_link_libraries(mEEM-GenConfig${SUFFIX}
        PUBLIC
          mEEM-Internal
        PRIVATE
          mEEM-RequiredInterface
    )

    # Custom target for explicit generation
    add_custom_target(generate-meem-config${SUFFIX}
        DEPENDS ${GENERATED_FILES}
        COMMENT "Ensure mEEM configuration files are generated"
    )

    # mEEM-UserConfig: User-written configuration files
    add_library(mEEM-UserConfig${SUFFIX})

    target_sources(mEEM-UserConfig${SUFFIX}
        PRIVATE
            ../crc/CRC.c
            MEEM_Checksum.cpp
            MEEM_EEAIF.cpp
            MEEM_UserCallbacks.cpp
    )

    target_include_directories(mEEM-UserConfig${SUFFIX}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../crc
            ${CMAKE_SOURCE_DIR}/test/eeprom_simulator
            ${CMAKE_SOURCE_DIR}/test/mocks
    )

    target_link_libraries(mEEM-UserConfig${SUFFIX}
        PRIVATE
            mEEM-GenConfig${SUFFIX}
            mEEM-ProvidedInterface
            mEEM-RequiredInterface
            gmock
    )

    # mEEM-Config: Combined interface
    add_library(mEEM-Config${SUFFIX} INTERFACE)

    target_link_libraries(mEEM-Config${SUFFIX}
        INTERFACE
            mEEM-GenConfig${SUFFIX}
            mEEM-UserConfig${SUFFIX}
    )
endfunction()

# Main test configuration, using all block management types
meem_add_test_config("" eeprom_datamodel.json)

# Multi-profile blocks only. Some code paths are compiled in/out depending on the
# used block management types, so they can't be covered by the main configuration.
meem_add_test_config("-MultiProfileOnly" eeprom_datamodel_multi_profile_only.json)
