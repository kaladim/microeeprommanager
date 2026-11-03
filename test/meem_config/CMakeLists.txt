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
# Builds the EEPROM image of a data model with the image generator: the identity header, then the defaults of every block,
# the unused space filled with 0xFF. The image is a raw binary of the whole EEPROM, so that a test can load it into the EEPROM simulator.
#   NAME                - the image is images/<NAME>.bin in the current build directory
#   DATAMODEL, SETTINGS - the inputs of the image generator
#   OUT_VAR             - receives the path of the image, which is built by the target that lists it among its sources
#------------------------------------------------------------------------------
function(meem_add_test_image NAME DATAMODEL SETTINGS OUT_VAR)
    set(IMAGE_FILE "${CMAKE_CURRENT_BINARY_DIR}/images/${NAME}.bin")
    set(HEX_DIR "${CMAKE_CURRENT_BINARY_DIR}/images/${NAME}")

    # The image generator writes an Intel HEX file, which bincopy converts to the raw binary
    file(GLOB IMAGE_GENERATOR_SOURCES ${CMAKE_SOURCE_DIR}/tools/eeprom_image_gen/*.py ${CMAKE_SOURCE_DIR}/tools/common/py/*.py)
    file(MAKE_DIRECTORY ${HEX_DIR})

    add_custom_command(
        OUTPUT ${IMAGE_FILE}
        COMMAND ${UV_EXECUTABLE} run --project ${CMAKE_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/tools/eeprom_image_gen/eeprom_image_gen.py
            ${DATAMODEL}
            ${SETTINGS}
            -o ${HEX_DIR}
        COMMAND ${UV_EXECUTABLE} run --project ${CMAKE_SOURCE_DIR}
            python -m bincopy convert -i ihex -o binary ${HEX_DIR}/eeprom.hex ${IMAGE_FILE}
        DEPENDS ${DATAMODEL} ${SETTINGS} ${IMAGE_GENERATOR_SOURCES} ${CMAKE_SOURCE_DIR}/uv.lock
        COMMENT "Building the EEPROM image ${IMAGE_FILE}"
        VERBATIM
    )

    set(${OUT_VAR} ${IMAGE_FILE} PARENT_SCOPE)
endfunction()

#------------------------------------------------------------------------------
# Defines the targets of a test configuration, generated from DATAMODEL:
#   mEEM-GenConfig<SUFFIX>       - Generated configuration files
#   mEEM-UserConfig<SUFFIX>      - User-written configuration files
#   mEEM-Config<SUFFIX>          - Combined interface. Defines MEEM_TEST_IMAGE_FILE: the EEPROM image of DATAMODEL (meem_add_test_image()),
#                                  and with LEGACY, MEEM_TEST_LEGACY_IMAGE_FILES: the images of the legacy models in their order, separated by '|'
#   generate-meem-config<SUFFIX> - Explicit generation
# Options:
#   CRC_IMPLEMENTATION <table|bitwise|hardware> - overrides 'crc_implementation' of the platform settings
#   EEPROM_PAGE_SIZE <bytes>                    - overrides 'eeprom_page_size' of the platform settings: 0 aligns no block to a page
#   LEGACY <DATAMODEL> <SETTINGS> ...           - legacy models, each with the platform settings it was released with, for the
#                                                 generator's '--legacy'. Relative paths are relative to this directory.
#------------------------------------------------------------------------------
function(meem_add_test_config SUFFIX DATAMODEL)
    cmake_parse_arguments(PARSE_ARGV 2 ARG "" "CRC_IMPLEMENTATION;EEPROM_PAGE_SIZE" "LEGACY")

    # The platform settings, with the overrides of this configuration
    set(SETTINGS_FILE ${PLATFORM_SETTINGS_FILE})

    if(ARG_CRC_IMPLEMENTATION OR DEFINED ARG_EEPROM_PAGE_SIZE)
        file(READ ${PLATFORM_SETTINGS_FILE} SETTINGS_JSON)
        if(ARG_CRC_IMPLEMENTATION)
            string(JSON SETTINGS_JSON SET "${SETTINGS_JSON}" crc_implementation "\"${ARG_CRC_IMPLEMENTATION}\"")
        endif()
        if(DEFINED ARG_EEPROM_PAGE_SIZE)
            string(JSON SETTINGS_JSON SET "${SETTINGS_JSON}" eeprom_page_size "${ARG_EEPROM_PAGE_SIZE}")
        endif()

        # Rewritten only when its content changes, so that the configuration isn't regenerated at every CMake run
        set(SETTINGS_FILE "${CMAKE_CURRENT_BINARY_DIR}/platform_settings${SUFFIX}.json")
        file(CONFIGURE OUTPUT ${SETTINGS_FILE} CONTENT "${SETTINGS_JSON}" @ONLY)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PLATFORM_SETTINGS_FILE})
    endif()

    # Define the generated source files (in build directory)
    set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated${SUFFIX}")
    set(GENERATED_FILES
        ${GENERATED_DIR}/MEEM_GenConfig.h
        ${GENERATED_DIR}/MEEM_GenConfig.c
        ${GENERATED_DIR}/MEEM_GenInterface.h
        ${GENERATED_DIR}/MEEM_GenInterface.c
        ${GENERATED_DIR}/MEEM_Migration.h
        ${GENERATED_DIR}/MEEM_Migration.c
    )

    # Define the input files that the generator depends on, its own sources included
    file(GLOB GENERATOR_SOURCES ${CMAKE_SOURCE_DIR}/tools/meem_config_gen/*.py ${CMAKE_SOURCE_DIR}/tools/common/py/*.py)
    set(GENERATOR_INPUTS
        ${CMAKE_CURRENT_SOURCE_DIR}/${DATAMODEL}
        ${SETTINGS_FILE}
        ${GENERATOR_SOURCES}
        ${CMAKE_SOURCE_DIR}/uv.lock
    )

    # The legacy models: the generator's arguments, and their EEPROM images, which the tests migrate from
    set(LEGACY_ARGS)
    set(LEGACY_IMAGE_FILES)
    set(LEGACY_INDEX 0)
    list(LENGTH ARG_LEGACY LEGACY_LENGTH)
    math(EXPR LEGACY_ODD "${LEGACY_LENGTH} % 2")
    if(LEGACY_ODD)
        message(FATAL_ERROR "meem_add_test_config(${SUFFIX}): LEGACY takes pairs of a data model and its platform settings")
    endif()

    while(ARG_LEGACY)
        list(POP_FRONT ARG_LEGACY LEGACY_DATAMODEL LEGACY_SETTINGS)
        cmake_path(ABSOLUTE_PATH LEGACY_DATAMODEL BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        cmake_path(ABSOLUTE_PATH LEGACY_SETTINGS BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

        list(APPEND LEGACY_ARGS --legacy ${LEGACY_DATAMODEL} ${LEGACY_SETTINGS})
        list(APPEND GENERATOR_INPUTS ${LEGACY_DATAMODEL} ${LEGACY_SETTINGS})

        meem_add_test_image(eeprom${SUFFIX}-legacy-${LEGACY_INDEX} ${LEGACY_DATAMODEL} ${LEGACY_SETTINGS} LEGACY_IMAGE_FILE)
        list(APPEND LEGACY_IMAGE_FILES ${LEGACY_IMAGE_FILE})
        math(EXPR LEGACY_INDEX "${LEGACY_INDEX} + 1")
    endwhile()

    # Create the generated directory
    file(MAKE_DIRECTORY ${GENERATED_DIR})

    # Custom command to generate mEEM configuration files
    add_custom_command(
        OUTPUT ${GENERATED_FILES}
        COMMAND ${UV_EXECUTABLE} run --project ${CMAKE_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/tools/meem_config_gen/meem_config_gen.py
            ${CMAKE_CURRENT_SOURCE_DIR}/${DATAMODEL}
            ${SETTINGS_FILE}
            ${GENERATED_DIR}
            ${LEGACY_ARGS}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS ${GENERATOR_INPUTS}
        COMMENT "Generating mEEM configuration files to ${GENERATED_DIR}"
        VERBATIM
    )

    # The EEPROM image of the data model, built along with the generated configuration
    meem_add_test_image(eeprom${SUFFIX} ${CMAKE_CURRENT_SOURCE_DIR}/${DATAMODEL} ${SETTINGS_FILE} IMAGE_FILE)

    # mEEM-GenConfig: Generated configuration files
    add_library(mEEM-GenConfig${SUFFIX})

    target_sources(mEEM-GenConfig${SUFFIX}
        PRIVATE
            ${GENERATED_FILES}
            ${IMAGE_FILE}
            ${LEGACY_IMAGE_FILES}
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
            MEEM_EEAIF.cpp
            MEEM_UserCallbacks.cpp
    )

    # The driver of the CRC unit, a mock
    if(ARG_CRC_IMPLEMENTATION STREQUAL "hardware")
        target_sources(mEEM-UserConfig${SUFFIX}
            PRIVATE
                MEEM_CRCIF.cpp
        )
    endif()

    target_include_directories(mEEM-UserConfig${SUFFIX}
        PRIVATE
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

    target_compile_definitions(mEEM-Config${SUFFIX}
        INTERFACE
            MEEM_TEST_IMAGE_FILE="${IMAGE_FILE}"
    )

    if(LEGACY_IMAGE_FILES)
        list(JOIN LEGACY_IMAGE_FILES "|" LEGACY_IMAGE_FILES_STRING)
        target_compile_definitions(mEEM-Config${SUFFIX}
            INTERFACE
                MEEM_TEST_LEGACY_IMAGE_FILES="${LEGACY_IMAGE_FILES_STRING}"
        )
    endif()
endfunction()

# Main test configuration, using all block management types and the default CRC implementation, 'table'
meem_add_test_config("" eeprom_datamodel.json)

# The same with the other built-in CRC implementation
meem_add_test_config("-Bitwise" eeprom_datamodel.json CRC_IMPLEMENTATION bitwise)

# The same with the CRC unit of the MCU, whose driver is a mock
meem_add_test_config("-HardwareCRC" eeprom_datamodel.json CRC_IMPLEMENTATION hardware)

# Multi-profile blocks only. Some code paths are compiled in/out depending on the
# used block management types, so they can't be covered by the main configuration.
meem_add_test_config("-MultiProfileOnly" eeprom_datamodel_multi_profile_only.json)

# Migration: a current model and the legacy models that MEEM_Init() migrates from. Legacy model 0 was released with the
# current platform settings, legacy models 1 and 2 with settings that don't align blocks to pages. Legacy model 2 has only
# blocks of pass 2, in an order and a direction that its layout forces.
meem_add_test_config("-Migration" eeprom_datamodel_migration.json
    LEGACY
        eeprom_datamodel_migration_legacy_0.json ${PLATFORM_SETTINGS_FILE}
        eeprom_datamodel_migration_legacy_1.json platform_settings_migration_legacy_1.json
        eeprom_datamodel_migration_legacy_2.json platform_settings_migration_legacy_1.json
)

# Conversions of the migration routines: a current model whose blocks all have another parameter list than in the legacy model,
# covering every pair of data types. The legacy model is given twice, laid out with and without page alignment, so that both
# share every routine.
meem_add_test_config("-Conversions" eeprom_datamodel_conversions.json
    LEGACY
        eeprom_datamodel_conversions_legacy.json ${PLATFORM_SETTINGS_FILE}
        eeprom_datamodel_conversions_legacy.json platform_settings_migration_legacy_1.json
)

# A migration example on the neutral model of the footprint harness: its Block09 changed as a production application's ConfigCommon, and the
# unchanged model, released with the settings of the harness, as its legacy model. Without page alignment, as there, so that the blocks
# before Block09 keep their places.
meem_add_test_config("-MigrationExample" ../footprint/production_like/eeprom_datamodel_migration.json
    EEPROM_PAGE_SIZE 0
    LEGACY
        ../footprint/production_like/eeprom_datamodel.json ../footprint/production_like/platform_settings_xc8.json
)
