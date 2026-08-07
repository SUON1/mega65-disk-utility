cmake_minimum_required(VERSION 3.24)
project(mega65_disk_utility VERSION 0.1.0 LANGUAGES C)

if(NOT APPLE)
  message(FATAL_ERROR "m65floppy-probe currently supports macOS only")
endif()

option(M65_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(M65_ENABLE_HARDWARE_TESTS "Enable opt-in tests that access real floppy hardware" OFF)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

function(m65_strict_warnings target)
  target_compile_options(${target} PRIVATE
    -Wall -Wextra -Wpedantic -Werror
    -Wconversion -Wsign-conversion -Wshadow
    -Wstrict-prototypes -Wmissing-prototypes
  )
endfunction()

function(m65_sanitizers target)
  if(M65_ENABLE_SANITIZERS)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

add_library(m65core STATIC
  src/bytes.c
  src/cli.c
  src/device.c
  src/json.c
  src/probe.c
  src/ufi.c
)
target_include_directories(m65core PUBLIC include)
m65_strict_warnings(m65core)
m65_sanitizers(m65core)

find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
find_library(DISKARBITRATION_FRAMEWORK DiskArbitration REQUIRED)

add_library(m65mac STATIC
  src/discovery_macos.c
  src/scsi_macos.c
)
target_include_directories(m65mac PUBLIC include)
target_link_libraries(m65mac PUBLIC m65core
  ${COREFOUNDATION_FRAMEWORK}
  ${IOKIT_FRAMEWORK}
  ${DISKARBITRATION_FRAMEWORK}
)
m65_strict_warnings(m65mac)
m65_sanitizers(m65mac)

add_executable(m65floppy-probe
  src/main.c
  src/output.c
)
target_link_libraries(m65floppy-probe PRIVATE m65mac)
m65_strict_warnings(m65floppy-probe)
m65_sanitizers(m65floppy-probe)

include(CTest)
if(BUILD_TESTING)
  add_executable(m65-unit-tests
    tests/fake_transport.c
    tests/test_bytes.c
    tests/test_cli.c
    tests/test_device.c
    tests/test_json.c
    tests/test_main.c
    tests/test_probe.c
    tests/test_ufi.c
  )
  target_include_directories(m65-unit-tests PRIVATE tests src)
  target_link_libraries(m65-unit-tests PRIVATE m65core)
  m65_strict_warnings(m65-unit-tests)
  m65_sanitizers(m65-unit-tests)
  add_test(NAME unit COMMAND m65-unit-tests)

  if(M65_ENABLE_HARDWARE_TESTS)
    add_test(NAME hardware-list COMMAND m65floppy-probe list --json)
    set_tests_properties(hardware-list PROPERTIES LABELS hardware)
  endif()
endif()
