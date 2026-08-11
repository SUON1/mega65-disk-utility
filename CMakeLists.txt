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
  src/cbi.c
  src/cli.c
  src/device.c
  src/json.c
  src/layout.c
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
  src/usb_cbi_macos.c
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
    tests/test_cbi.c
    tests/test_cli.c
    tests/test_device.c
    tests/test_diagnose.c
    tests/test_json.c
    tests/test_layout.c
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

# ---------------------------------------------------------------------------
# Phase 2: IOUSBHost whole-device capture transport
# ---------------------------------------------------------------------------
# The IOUSBHost adapter (src/usb_cbi_iousbhost_macos.m) is an Objective-C (ARC)
# translation unit that implements the low-level M65CbiIo operation table on top
# of IOUSBHost.framework and hands it to the shared, unit-tested CBI/UFI engine
# in src/cbi.c.  Its factory symbol m65_iousbhost_transport_create() is used
# ONLY by the executable (src/main.c), which links m65mac.  Neither m65core nor
# the unit-test binary references it, so no stub is linked into the tests.
if(APPLE)
  # Enable Objective-C so CMake compiles the .m source; the project is
  # otherwise pure C.
  enable_language(OBJC)

  target_sources(m65mac PRIVATE
    src/usb_cbi_iousbhost_macos.m
  )
  # -fobjc-arc: automatic reference counting for the adapter's ObjC objects.
  # -fmodules: required so the `@import Foundation;` / `@import IOUSBHost;`
  # module imports in the .m resolve (they fail with modules disabled).
  set_source_files_properties(src/usb_cbi_iousbhost_macos.m
    PROPERTIES COMPILE_FLAGS "-fobjc-arc -fmodules"
  )
  target_link_libraries(m65mac PRIVATE
    "-framework IOUSBHost"
    "-framework Foundation"
  )
endif()

# Stub factory for non-Apple (Linux/Windows) builds, where IOUSBHost is absent.
# (This project currently FATAL_ERRORs on non-Apple above; the stub keeps the
# link self-consistent should that guard ever be relaxed.)
if(NOT APPLE)
  target_sources(m65mac PRIVATE
    src/usb_cbi_iousbhost_stubs.c
  )
endif()
