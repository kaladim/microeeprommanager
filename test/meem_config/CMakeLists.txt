# Use Python from the virtual environment
if(WIN32)
    set(Python3_EXECUTABLE "${CMAKE_SOURCE_DIR}/.venv_meem/Scripts/python.exe")
else()
    set(Python3_EXECUTABLE "${CMAKE_SOURCE_DIR}/.venv_meem/bin/python")
endif()

# Define the generated source files (in build directory)
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(GENERATED_FILES
    ${GENERATED_DIR}/MEEM_GenConfig.h
    ${GENERATED_DIR}/MEEM_GenConfig.c
    ${GENERATED_DIR}/MEEM_GenInterface.h
    ${GENERATED_DIR}/MEEM_GenInterface.c
)

# Select platform settings file based on compiler/OS
if(WIN32)
    set(PLATFORM_SETTINGS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/platform_settings_msvc.json")
else()
    # Use GCC settings for non-MSVC compilers (GCC, Clang on Linux/macOS)
    set(PLATFORM_SETTINGS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/platform_settings_gcc.json")
endif()

message(STATUS "Using platform settings: ${PLATFORM_SETTINGS_FILE}")

# Define the input files that the generator depends on
set(GENERATOR_INPUTS
    ${CMAKE_CURRENT_SOURCE_DIR}/eeprom_datamodel.json
    ${PLATFORM_SETTINGS_FILE}
    ${CMAKE_SOURCE_DIR}/tools/meem_config_gen/meem_config_gen.py
)

# Create the generated directory
file(MAKE_DIRECTORY ${GENERATED_DIR})

# Custom command to generate mEEM configuration files
add_custom_command(
    OUTPUT ${GENERATED_FILES}
    COMMAND ${Python3_EXECUTABLE} 
        ${CMAKE_SOURCE_DIR}/tools/meem_config_gen/meem_config_gen.py
        ${CMAKE_CURRENT_SOURCE_DIR}/eeprom_datamodel.json
        ${PLATFORM_SETTINGS_FILE}
        ${GENERATED_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${GENERATOR_INPUTS}
    COMMENT "Generating mEEM configuration files to ${GENERATED_DIR}"
    VERBATIM
)

#------------------------------------------------------------------------------
# mEEM-GenConfig: Generated configuration files
#------------------------------------------------------------------------------
add_library(mEEM-GenConfig)

target_sources(mEEM-GenConfig
    PRIVATE
        ${GENERATED_FILES}
)

target_include_directories(mEEM-GenConfig
    PUBLIC
        ${GENERATED_DIR}
)

target_link_libraries(mEEM-GenConfig
    PUBLIC
      mEEM-Internal
    PRIVATE
      mEEM-RequiredInterface
)

# Custom target for explicit generation
add_custom_target(generate-meem-config
    DEPENDS ${GENERATED_FILES}
    COMMENT "Ensure mEEM configuration files are generated"
)

#------------------------------------------------------------------------------
# mEEM-UserConfig: User-written configuration files 
#------------------------------------------------------------------------------
add_library(mEEM-UserConfig)

target_sources(mEEM-UserConfig
    PRIVATE
        ../crc/CRC.c
        MEEM_Checksum.cpp
        MEEM_EEAIF.cpp
        MEEM_UserCallbacks.cpp
)

target_include_directories(mEEM-UserConfig
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../crc
        ${CMAKE_SOURCE_DIR}/test/eeprom_simulator
        ${CMAKE_SOURCE_DIR}/test/mocks
)

target_link_libraries(mEEM-UserConfig
    PRIVATE
        mEEM-GenConfig
        mEEM-ProvidedInterface
        mEEM-RequiredInterface
        gmock
)

#------------------------------------------------------------------------------
# mEEM-Config: Combined interface
#------------------------------------------------------------------------------
add_library(mEEM-Config INTERFACE)

target_link_libraries(mEEM-Config
    INTERFACE
        mEEM-GenConfig
        mEEM-UserConfig
)