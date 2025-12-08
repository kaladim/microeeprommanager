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