# AddressSanitizer turns out-of-bounds memory accesses into test failures.
# Must be set up before any target is created.
option(MEEM_TEST_ENABLE_ASAN "Build the unit tests with AddressSanitizer" ON)

if(MEEM_TEST_ENABLE_ASAN)
  if(MSVC)
    # The /RTC run-time checks are incompatible with AddressSanitizer
    foreach(LANG C CXX)
      string(REGEX REPLACE "/RTC[1csu]*" "" CMAKE_${LANG}_FLAGS_DEBUG "${CMAKE_${LANG}_FLAGS_DEBUG}")
    endforeach()
    add_compile_options(/fsanitize=address)

    # The ASan runtime is not on PATH outside of a VS developer prompt - it is deployed next to the test executables
    get_filename_component(MSVC_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    file(GLOB ASAN_RUNTIME_DLLS "${MSVC_BIN_DIR}/clang_rt.asan*_dynamic-*.dll")
  else()
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
  endif()
endif()

add_executable(mEEM-Test)

set(gtest_force_shared_crt on)

add_subdirectory(googletest)
add_subdirectory(meem_config)

set(MEEM_TEST_SOURCES
    test_eep_sim.cpp
    test_common.cpp
    test_crc.cpp
    test_basic_blocks.cpp
    test_backup_copy_blocks.cpp
    test_multi_profile_blocks.cpp
    test_wear_leveling_blocks.cpp
    test_rewrites.cpp
    test_commit.cpp
    test_reset.cpp
)

target_sources(mEEM-Test
  PRIVATE
    ${MEEM_TEST_SOURCES}
)

target_include_directories(mEEM-Test
  PRIVATE
    .
    ./eeprom_simulator
    ./mocks
)

target_link_libraries(mEEM-Test
  PRIVATE
    mEEM-Core
    mEEM-Config
    gtest_main
    gmock
)

add_test(NAME
    mEEM-Test
    COMMAND
    mEEM-Test)

#------------------------------------------------------------------------------
# All tests, run against the main configuration with the bitwise CRC
add_executable(mEEM-Test-Bitwise)

target_sources(mEEM-Test-Bitwise
  PRIVATE
    ${MEEM_TEST_SOURCES}
)

target_include_directories(mEEM-Test-Bitwise
  PRIVATE
    .
    ./eeprom_simulator
    ./mocks
)

target_link_libraries(mEEM-Test-Bitwise
  PRIVATE
    mEEM-Core
    mEEM-Config-Bitwise
    gtest_main
    gmock
)

add_test(NAME
    mEEM-Test-Bitwise
    COMMAND
    mEEM-Test-Bitwise)

#------------------------------------------------------------------------------
# All tests, run against the main configuration with the CRC unit of the MCU (a mock).
# The tests of the built-in CRC routine are replaced by those of the CRC unit.
add_executable(mEEM-Test-HardwareCRC)

set(MEEM_TEST_SOURCES_HARDWARE_CRC ${MEEM_TEST_SOURCES})
list(REMOVE_ITEM MEEM_TEST_SOURCES_HARDWARE_CRC test_crc.cpp)

target_sources(mEEM-Test-HardwareCRC
  PRIVATE
    ${MEEM_TEST_SOURCES_HARDWARE_CRC}
    test_hardware_crc.cpp
)

target_include_directories(mEEM-Test-HardwareCRC
  PRIVATE
    .
    ./eeprom_simulator
    ./mocks
)

target_link_libraries(mEEM-Test-HardwareCRC
  PRIVATE
    mEEM-Core
    mEEM-Config-HardwareCRC
    gtest_main
    gmock
)

add_test(NAME
    mEEM-Test-HardwareCRC
    COMMAND
    mEEM-Test-HardwareCRC)

#------------------------------------------------------------------------------
# The block management type agnostic tests, run against the multi-profile only configuration
add_executable(mEEM-Test-MultiProfileOnly)

target_sources(mEEM-Test-MultiProfileOnly
  PRIVATE
    test_common.cpp
    test_crc.cpp
    test_multi_profile_blocks.cpp
    test_rewrites.cpp
    test_commit.cpp
    test_reset.cpp
)

target_include_directories(mEEM-Test-MultiProfileOnly
  PRIVATE
    .
    ./eeprom_simulator
    ./mocks
)

target_link_libraries(mEEM-Test-MultiProfileOnly
  PRIVATE
    mEEM-Core
    mEEM-Config-MultiProfileOnly
    gtest_main
    gmock
)

add_test(NAME
    mEEM-Test-MultiProfileOnly
    COMMAND
    mEEM-Test-MultiProfileOnly)

#------------------------------------------------------------------------------
# The block management type agnostic tests and the tests of migration, run against the migration configuration:
# a current model and its legacy models
add_executable(mEEM-Test-Migration)

target_sources(mEEM-Test-Migration
  PRIVATE
    test_common.cpp
    test_crc.cpp
    test_multi_profile_blocks.cpp
    test_rewrites.cpp
    test_commit.cpp
    test_reset.cpp
    test_legacy_tables.cpp
    test_legacy_readers.cpp
    test_legacy_routines.cpp
    test_legacy_passes.cpp
    test_migration_scenarios.cpp
)

target_include_directories(mEEM-Test-Migration
  PRIVATE
    .
    ./eeprom_simulator
    ./mocks
)

target_link_libraries(mEEM-Test-Migration
  PRIVATE
    mEEM-Core
    mEEM-Config-Migration
    gtest_main
    gmock
)

add_test(NAME
    mEEM-Test-Migration
    COMMAND
    mEEM-Test-Migration)

#------------------------------------------------------------------------------
# The conversions of the migration routines, run against the conversions configuration
add_executable(mEEM-Test-Conversions)

target_sources(mEEM-Test-Conversions
  PRIVATE
    test_conversions.cpp
)

target_include_directories(mEEM-Test-Conversions
  PRIVATE
    .
    ./eeprom_simulator
    ./mocks
)

target_link_libraries(mEEM-Test-Conversions
  PRIVATE
    mEEM-Core
    mEEM-Config-Conversions
    gtest_main
    gmock
)

add_test(NAME
    mEEM-Test-Conversions
    COMMAND
    mEEM-Test-Conversions)

#------------------------------------------------------------------------------
# A migration example end to end, run against the configuration of the neutral model with its Block09 changed
add_executable(mEEM-Test-MigrationExample)

target_sources(mEEM-Test-MigrationExample
  PRIVATE
    test_migration_example.cpp
)

target_include_directories(mEEM-Test-MigrationExample
  PRIVATE
    .
    ./eeprom_simulator
    ./mocks
)

target_link_libraries(mEEM-Test-MigrationExample
  PRIVATE
    mEEM-Core
    mEEM-Config-MigrationExample
    gtest_main
    gmock
)

add_test(NAME
    mEEM-Test-MigrationExample
    COMMAND
    mEEM-Test-MigrationExample)

if(ASAN_RUNTIME_DLLS)
  foreach(TEST_TARGET mEEM-Test mEEM-Test-Bitwise mEEM-Test-HardwareCRC mEEM-Test-MultiProfileOnly mEEM-Test-Migration mEEM-Test-Conversions mEEM-Test-MigrationExample)
    add_custom_command(TARGET ${TEST_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ASAN_RUNTIME_DLLS} $<TARGET_FILE_DIR:${TEST_TARGET}>
      VERBATIM
    )
  endforeach()
endif()

#------------------------------------------------------------------------------
# Python tools
add_test(NAME
    tools_common-Test
    COMMAND
    ${UV_EXECUTABLE} run --project ${CMAKE_SOURCE_DIR} python -m unittest discover
      --start-directory ${CMAKE_SOURCE_DIR}/tools/common/py
      --pattern "test_*.py")

add_test(NAME
    eeprom_image_gen-Test
    COMMAND
    ${UV_EXECUTABLE} run --project ${CMAKE_SOURCE_DIR} python -m unittest discover
      --start-directory ${CMAKE_SOURCE_DIR}/tools/eeprom_image_gen
      --pattern "test_*.py")

add_test(NAME
    meem_config_gen-Test
    COMMAND
    ${UV_EXECUTABLE} run --project ${CMAKE_SOURCE_DIR} python -m unittest discover
      --start-directory ${CMAKE_SOURCE_DIR}/tools/meem_config_gen
      --pattern "test_*.py")

add_test(NAME
    legacy_model_collector-Test
    COMMAND
    ${UV_EXECUTABLE} run --project ${CMAKE_SOURCE_DIR} python -m unittest discover
      --start-directory ${CMAKE_SOURCE_DIR}/tools/legacy_model_collector
      --pattern "test_*.py")

#------------------------------------------------------------------------------
# Web tools - their business logic is tested with the built-in test runner of Node.js
find_program(NODE_EXECUTABLE node)

if(NODE_EXECUTABLE)
  add_test(NAME
      web_tools-Test
      COMMAND
      ${NODE_EXECUTABLE} --test
        ${CMAKE_SOURCE_DIR}/tools/common/js/test/crc.test.js
        ${CMAKE_SOURCE_DIR}/tools/common/js/test/identity.test.js
        ${CMAKE_SOURCE_DIR}/tools/configurator/test/config-json.test.js
        ${CMAKE_SOURCE_DIR}/tools/configurator/test/identity.test.js
        ${CMAKE_SOURCE_DIR}/tools/eeprom_inspector/test/report-builder.test.js)
else()
  message(STATUS "Node.js not found - the tests of the web tools will be skipped")
endif()
