add_library(mEEM-ProvidedInterface INTERFACE)

target_include_directories(mEEM-ProvidedInterface
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/provided_interface
)

#------------------------------------------------------------------------------
add_library(mEEM-RequiredInterface INTERFACE)

target_include_directories(mEEM-RequiredInterface
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/required_interface
)

#------------------------------------------------------------------------------
add_library(mEEM-Internal INTERFACE)

target_include_directories(mEEM-Internal
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/core
)

#------------------------------------------------------------------------------
add_library(mEEM-Core INTERFACE)

target_sources(mEEM-Core
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MEEM_BlockManagement_Basic.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MEEM_BlockManagement_Common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MEEM_BlockManagement_MultiProfile.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MEEM_BlockManagement_BackupCopy.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MEEM_BlockManagement_WearLeveling.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/MEEM.c
)

target_link_libraries(mEEM-Core
  INTERFACE
    mEEM-ProvidedInterface
    mEEM-RequiredInterface
    mEEM-Internal
)
