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

target_sources(mEEM-Test
  PRIVATE
    test_eep_sim.cpp
    test_common.cpp
    test_basic_blocks.cpp
    test_backup_copy_blocks.cpp
    test_multi_profile_blocks.cpp
    test_wear_leveling_blocks.cpp
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
# The block management type agnostic tests, run against the multi-profile only configuration
add_executable(mEEM-Test-MultiProfileOnly)

target_sources(mEEM-Test-MultiProfileOnly
  PRIVATE
    test_common.cpp
    test_multi_profile_blocks.cpp
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

if(ASAN_RUNTIME_DLLS)
  foreach(TEST_TARGET mEEM-Test mEEM-Test-MultiProfileOnly)
    add_custom_command(TARGET ${TEST_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ASAN_RUNTIME_DLLS} $<TARGET_FILE_DIR:${TEST_TARGET}>
      VERBATIM
    )
  endforeach()
endif()

#------------------------------------------------------------------------------
# Python tools
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

#------------------------------------------------------------------------------
# Web tools - their business logic is tested with the built-in test runner of Node.js
find_program(NODE_EXECUTABLE node)

if(NODE_EXECUTABLE)
  add_test(NAME
      web_tools-Test
      COMMAND
      ${NODE_EXECUTABLE} --test
        ${CMAKE_SOURCE_DIR}/tools/configurator/test/config_json.test.js
        ${CMAKE_SOURCE_DIR}/tools/eeprom_inspector/test/report_builder.test.js)
else()
  message(STATUS "Node.js not found - the tests of the web tools will be skipped")
endif()
