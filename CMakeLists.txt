cmake_minimum_required(VERSION 3.16)

# Read project version from VERSION file
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" FETCH_VERSION_RAW LIMIT_COUNT 1)
string(STRIP "${FETCH_VERSION_RAW}" FETCH_VERSION_STRING)
if(NOT FETCH_VERSION_STRING)
  set(FETCH_VERSION_STRING "0.1.0")
endif()

project(fetch
  VERSION ${FETCH_VERSION_STRING}
  DESCRIPTION "Real-time 3D ASCII terminal system information tool"
  LANGUAGES C
)

# Standard C11, extensions disabled for portability
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Platform and architecture identifiers
set(FETCH_OS "${CMAKE_SYSTEM_NAME}")
set(FETCH_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
if(FETCH_ARCH MATCHES "^(AMD64|amd64|x86_64)$")
  set(FETCH_ARCH "x86_64")
endif()
# Suffix marks this as the native Windows port rather than upstream fetch.
# VERSION stays numeric because CMake's project(VERSION) rejects suffixes.
set(FETCH_CODENAME "Overclocked ASCII (win)")

# Sensible warning flags across compilers (no aggressive -Werror)
function(fetch_apply_warnings target_name)
  if(MSVC)
    target_compile_options(${target_name} PRIVATE /W3)
  else()
    target_compile_options(${target_name} PRIVATE -Wall -Wextra)
  endif()
endfunction()

# Explicit source modules (no blind globbing)
set(CORE_SRCS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/common.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/core/common.h"
)

set(RENDERER_SRCS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/renderer.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/renderer.h"
)

set(CONFIG_SRCS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/config/config.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/config/config.h"
)

set(LOGO_SRCS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/logo/logo.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/logo/logo.h"
)

# Math library (required on GCC/Clang/POSIX; not used/available on MSVC)
set(MATH_LIBRARY "")
if(NOT MSVC)
  find_library(LIBM_LOCATION m)
  if(LIBM_LOCATION)
    set(MATH_LIBRARY "${LIBM_LOCATION}")
  else()
    set(MATH_LIBRARY m)
  endif()
endif()

# Platform backend selection and system library dependencies
if(WIN32)
  set(PLATFORM_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/platform_win.c")
  # Win32 system dependencies: Winsock, DXGI adapter query, IP Helper API
  set(PLATFORM_LIBS ws2_32 dxgi iphlpapi)
elseif(APPLE)
  set(PLATFORM_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/platform_posix.c")
  set(PLATFORM_LIBS "-framework IOKit" "-framework CoreFoundation")
else()
  # Linux and general POSIX
  set(PLATFORM_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/platform_posix.c")
  set(PLATFORM_LIBS "")
endif()

# Main executable target
add_executable(fetch
  fetch.c
  ${CORE_SRCS}
  ${RENDERER_SRCS}
  ${CONFIG_SRCS}
  ${LOGO_SRCS}
  ${PLATFORM_SRC}
)

target_include_directories(fetch PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
if(WIN32)
  target_include_directories(fetch PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests/include")
endif()

target_compile_definitions(fetch PRIVATE
  FETCH_VERSION="${FETCH_VERSION_STRING}"
  FETCH_CODENAME="${FETCH_CODENAME}"
  FETCH_ARCH="${FETCH_ARCH}"
  FETCH_OS="${FETCH_OS}"
)

target_link_libraries(fetch PRIVATE ${PLATFORM_LIBS} ${MATH_LIBRARY})
fetch_apply_warnings(fetch)

# Installation rules
include(GNUInstallDirs)

install(TARGETS fetch
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(FILES README.md LICENSE
  DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/fetch"
  OPTIONAL
)

# Testing configuration
include(CTest)

if(BUILD_TESTING)
  # Target: test_baseline (Phase 0)
  add_executable(test_baseline
    tests/test_baseline.c
    ${CORE_SRCS}
    ${RENDERER_SRCS}
    ${CONFIG_SRCS}
    ${LOGO_SRCS}
    ${PLATFORM_SRC}
  )
  target_include_directories(test_baseline PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  if(WIN32)
    target_include_directories(test_baseline PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests/include")
  endif()
  target_compile_definitions(test_baseline PRIVATE
    FETCH_VERSION="${FETCH_VERSION_STRING}"
    FETCH_CODENAME="${FETCH_CODENAME}"
    FETCH_ARCH="${FETCH_ARCH}"
    FETCH_OS="${FETCH_OS}"
  )
  target_link_libraries(test_baseline PRIVATE ${PLATFORM_LIBS} ${MATH_LIBRARY})
  fetch_apply_warnings(test_baseline)
  add_test(NAME baseline COMMAND test_baseline)

  # Target: test_phase1 (Core and Renderer unit tests)
  add_executable(test_phase1
    tests/test_phase1.c
    ${CORE_SRCS}
    ${RENDERER_SRCS}
  )
  target_include_directories(test_phase1 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(test_phase1 PRIVATE ${MATH_LIBRARY})
  fetch_apply_warnings(test_phase1)
  add_test(NAME phase1 COMMAND test_phase1)

  # Target: test_phase2 (Config and Logo unit tests)
  add_executable(test_phase2
    tests/test_phase2.c
    ${CORE_SRCS}
    ${RENDERER_SRCS}
    ${CONFIG_SRCS}
    ${LOGO_SRCS}
  )
  target_include_directories(test_phase2 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(test_phase2 PRIVATE ${MATH_LIBRARY})
  fetch_apply_warnings(test_phase2)
  add_test(NAME phase2 COMMAND test_phase2)

  # Target: test_phase3 (POSIX Platform abstraction unit tests)
  add_executable(test_phase3
    tests/test_phase3.c
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/platform_posix.c"
  )
  target_include_directories(test_phase3 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  if(WIN32)
    target_include_directories(test_phase3 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests/include")
  endif()
  target_compile_definitions(test_phase3 PRIVATE FETCH_TESTING)
  target_link_libraries(test_phase3 PRIVATE ${MATH_LIBRARY})
  fetch_apply_warnings(test_phase3)
  add_test(NAME phase3 COMMAND test_phase3)

  # Windows-only platform unit and smoke tests
  if(WIN32)
    # Target: test_phase4 (Windows terminal abstraction unit tests)
    add_executable(test_phase4
      tests/test_phase4.c
      "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/platform_win.c"
    )
    target_include_directories(test_phase4 PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/include"
    )
    target_compile_definitions(test_phase4 PRIVATE FETCH_TESTING)
    target_link_libraries(test_phase4 PRIVATE ${PLATFORM_LIBS} ${MATH_LIBRARY})
    fetch_apply_warnings(test_phase4)
    add_test(NAME phase4 COMMAND test_phase4)

    # Target: test_phase5 (Windows system information unit tests)
    add_executable(test_phase5
      tests/test_phase5.c
      "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/platform_win.c"
    )
    target_include_directories(test_phase5 PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/include"
    )
    target_compile_definitions(test_phase5 PRIVATE FETCH_TESTING)
    target_link_libraries(test_phase5 PRIVATE ${PLATFORM_LIBS} ${MATH_LIBRARY})
    fetch_apply_warnings(test_phase5)
    add_test(NAME phase5 COMMAND test_phase5)

    # Target: test_phase6 (Windows paths, logo, and package managers unit tests)
    add_executable(test_phase6
      tests/test_phase6.c
      ${CORE_SRCS}
      ${RENDERER_SRCS}
      ${CONFIG_SRCS}
      ${LOGO_SRCS}
      "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/platform_win.c"
    )
    target_include_directories(test_phase6 PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/include"
    )
    target_compile_definitions(test_phase6 PRIVATE FETCH_TESTING)
    target_link_libraries(test_phase6 PRIVATE ${PLATFORM_LIBS} ${MATH_LIBRARY})
    fetch_apply_warnings(test_phase6)
    add_test(NAME phase6 COMMAND test_phase6)

    # Target: smoke_win (Native Windows console & lifecycle smoke suite)
    add_executable(smoke_win
      tests/smoke_win.c
      "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/platform_win.c"
    )
    target_include_directories(smoke_win PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/include"
    )
    target_compile_definitions(smoke_win PRIVATE FETCH_TESTING)
    target_link_libraries(smoke_win PRIVATE ${PLATFORM_LIBS} ${MATH_LIBRARY})
    fetch_apply_warnings(smoke_win)
  endif()

  # CTest-safe CLI smoke tests (headless, non-interactive)
  add_test(NAME cli_help COMMAND fetch --help)
  add_test(NAME cli_version COMMAND fetch --version)
  add_test(NAME cli_frames COMMAND fetch --frames 5)
  add_test(NAME cli_no_info_frames COMMAND fetch --no-info --frames 1)

  # Custom target: smoke (mirrors `make smoke` plus CLI verification)
  if(WIN32)
    add_custom_target(smoke
      COMMAND $<TARGET_FILE:fetch> --help
      COMMAND $<TARGET_FILE:fetch> --version
      COMMAND $<TARGET_FILE:fetch> --frames 5
      COMMAND $<TARGET_FILE:fetch> --no-info --frames 1
      COMMAND $<TARGET_FILE:smoke_win>
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
      DEPENDS fetch smoke_win
      COMMENT "Running smoke test suite"
    )
  else()
    add_custom_target(smoke
      COMMAND $<TARGET_FILE:fetch> --help
      COMMAND $<TARGET_FILE:fetch> --version
      COMMAND $<TARGET_FILE:fetch> --frames 5
      COMMAND $<TARGET_FILE:fetch> --no-info --frames 1
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
      DEPENDS fetch
      COMMENT "Running smoke test suite"
    )
  endif()
endif()
