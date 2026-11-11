cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

project(cmdb-lite
  VERSION 0.5.0
  DESCRIPTION "Local-first encrypted Configuration Management Database"
  LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC AND DEFINED VCPKG_TARGET_TRIPLET AND
   VCPKG_TARGET_TRIPLET MATCHES "static")
  set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
endif()

include(GNUInstallDirs)
include(CTest)

option(CMDB_BUILD_UI "Build the desktop webview application" ON)
option(CMDB_BUILD_TESTS "Build the test suite" ${BUILD_TESTING})
option(CMDB_ENABLE_ASAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(CMDB_WERROR "Treat compiler warnings as errors" OFF)
option(CMDB_FETCH_WEBVIEW "Fetch the pinned webview dependency" ON)
set(CMDB_WEBVIEW_SOURCE_DIR "" CACHE PATH
  "Use a local webview source tree instead of downloading it")
option(CMDB_BUILD_FUZZERS "Build libFuzzer targets (Clang only)" OFF)
option(CMDB_STATIC_CRYPTO
  "Link SQLCipher and libsodium statically on Unix when available" ON)

if(MSVC)
  set(CMDB_WARNINGS /W4)
  if(CMDB_WERROR)
    list(APPEND CMDB_WARNINGS /WX)
  endif()
else()
  set(CMDB_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wformat=2
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wcast-qual
    -Wwrite-strings
    -Wundef
    -Wvla
    -Wpointer-arith
    -Wcast-align
    -Wstrict-aliasing=2)
  if(CMDB_WERROR)
    list(APPEND CMDB_WARNINGS -Werror)
  endif()
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(
    -fstack-protector-strong
    -fno-common)
  if(UNIX)
    add_link_options(-Wl,-z,relro -Wl,-z,now)
  endif()
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(
      -O2
      -D_FORTIFY_SOURCE=3
      -fstack-clash-protection)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86")
      add_compile_options(-fcf-protection=full)
    endif()
  endif()
  if(CMDB_ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

if(MSVC)
  add_compile_options(/W4 /GS /guard:cf /sdl)
  add_link_options(/guard:cf /DYNAMICBASE /NXCOMPAT)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

if(WIN32)
  find_package(sqlcipher CONFIG REQUIRED)
  find_package(unofficial-sodium CONFIG REQUIRED)
  find_package(expat CONFIG REQUIRED)
  set(CMDB_CRYPTO_LIBS
    sqlcipher::sqlcipher
    unofficial-sodium::sodium
    advapi32
    bcrypt
    crypt32
    ws2_32)
  set(CMDB_EXPAT_LIBS expat::expat)
else()
  find_package(PkgConfig REQUIRED)
  find_package(Threads REQUIRED)
  find_package(ZLIB REQUIRED)
  find_package(EXPAT REQUIRED)
  set(CMDB_EXPAT_LIBS EXPAT::EXPAT)
  pkg_check_modules(SQLCIPHER REQUIRED IMPORTED_TARGET sqlcipher)
  pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
  set(CMDB_CRYPTO_LIBS
    PkgConfig::SQLCIPHER
    PkgConfig::SODIUM)
  set(CMDB_CRYPTO_INCLUDE_DIRS
    ${SQLCIPHER_INCLUDE_DIRS}
    ${SODIUM_INCLUDE_DIRS})

  if(CMDB_STATIC_CRYPTO)
    find_file(CMDB_SQLCIPHER_STATIC
      NAMES libsqlcipher.a
      PATHS ${SQLCIPHER_LIBRARY_DIRS}
      NO_DEFAULT_PATH)
    find_file(CMDB_SODIUM_STATIC
      NAMES libsodium.a
      PATHS ${SODIUM_LIBRARY_DIRS}
      NO_DEFAULT_PATH)
    find_file(CMDB_CRYPTO_SHARED
      NAMES libcrypto.so.3 libcrypto.so
      PATHS ${SQLCIPHER_LIBRARY_DIRS}
      NO_DEFAULT_PATH)
    if(CMDB_SQLCIPHER_STATIC AND CMDB_SODIUM_STATIC AND CMDB_CRYPTO_SHARED)
      set(CMDB_CRYPTO_LIBS
        ${CMDB_SQLCIPHER_STATIC}
        ${CMDB_SODIUM_STATIC}
        ${CMDB_CRYPTO_SHARED}
        ZLIB::ZLIB
        Threads::Threads
        ${CMAKE_DL_LIBS}
        m)
      message(STATUS "Linking SQLCipher/libsodium statically")
    else()
      message(STATUS
        "Static SQLCipher/libsodium libraries not found; using shared libraries")
    endif()
  endif()
endif()

add_library(cjson STATIC
  third_party/cjson/cJSON.c)
target_include_directories(cjson PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cjson)
set_target_properties(cjson PROPERTIES
  C_STANDARD 99
  POSITION_INDEPENDENT_CODE ON)

add_library(cmdb_core STATIC
  src/core/app.c
  src/core/api.c
  src/core/audit.c
  src/core/categories.c
  src/core/ci.c
  src/core/crypto.c
  src/core/db.c
  src/core/diagram.c
  src/core/drawio.c
  src/core/error.c
  src/core/export.c
  src/core/graph.c
  src/core/import.c
  src/core/json_util.c
  src/core/meta.c
  src/core/relationships.c
  src/core/scan.c
  src/core/scan_manager.c
  src/core/scan_store.c
  src/core/schema.c
  src/core/settings.c
  src/core/types.c
  src/core/util.c
  src/core/uuid.c)

target_include_directories(cmdb_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMDB_CRYPTO_INCLUDE_DIRS})
# `<sqlcipher/sqlite3.h>` must resolve for both the Debian layout
# (/usr/include/sqlcipher/sqlite3.h) and custom prefixes.
foreach(_sqlcipher_include ${SQLCIPHER_INCLUDE_DIRS})
  get_filename_component(_sqlcipher_parent "${_sqlcipher_include}" DIRECTORY)
  target_include_directories(cmdb_core PUBLIC "${_sqlcipher_parent}")
endforeach()
if(TARGET sqlcipher::sqlcipher)
  get_target_property(_sqlcipher_target_includes
    sqlcipher::sqlcipher INTERFACE_INCLUDE_DIRECTORIES)
  if(_sqlcipher_target_includes)
    foreach(_sqlcipher_include ${_sqlcipher_target_includes})
      if(IS_ABSOLUTE "${_sqlcipher_include}" AND
         NOT "${_sqlcipher_include}" MATCHES "\\$<")
        get_filename_component(_sqlcipher_parent
          "${_sqlcipher_include}" DIRECTORY)
        target_include_directories(cmdb_core PUBLIC "${_sqlcipher_parent}")
      endif()
    endforeach()
  endif()
endif()
set_target_properties(cmdb_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(cmdb_core PRIVATE ${CMDB_WARNINGS})
target_compile_definitions(cmdb_core PRIVATE
  CMDB_VERSION="${PROJECT_VERSION}")
target_compile_definitions(cmdb_core PUBLIC SQLITE_HAS_CODEC)
target_link_libraries(cmdb_core PUBLIC
  ${CMDB_CRYPTO_LIBS}
  ${CMDB_EXPAT_LIBS}
  cjson)
if(NOT WIN32)
  target_link_libraries(cmdb_core PUBLIC Threads::Threads)
endif()

if(NOT WIN32)
  target_compile_definitions(cmdb_core PRIVATE _POSIX_C_SOURCE=200809L)
endif()

if(CMDB_BUILD_UI)
  enable_language(CXX)
  if(WIN32)
    enable_language(RC)
  endif()
  if(CMDB_WEBVIEW_SOURCE_DIR OR CMDB_FETCH_WEBVIEW)
    set(WEBVIEW_BUILD ON CACHE BOOL "" FORCE)
    set(WEBVIEW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(WEBVIEW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(WEBVIEW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(WEBVIEW_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
    set(WEBVIEW_BUILD_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
    set(WEBVIEW_INSTALL_TARGETS OFF CACHE BOOL "" FORCE)
    set(WEBVIEW_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
    set(WEBVIEW_ENABLE_CHECKS OFF CACHE BOOL "" FORCE)
    set(WEBVIEW_ENABLE_PACKAGING OFF CACHE BOOL "" FORCE)
    set(WEBVIEW_IS_CI OFF CACHE BOOL "" FORCE)
    set(WEBVIEW_WEBKITGTK_API "4.1" CACHE STRING "" FORCE)
    if(WIN32 AND CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "MultiThreaded")
      set(WEBVIEW_USE_STATIC_MSVC_RUNTIME ON CACHE BOOL "" FORCE)
    endif()
    if(CMDB_WEBVIEW_SOURCE_DIR)
      add_subdirectory("${CMDB_WEBVIEW_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/webview")
    else()
      include(FetchContent)
      # Pinned to the 0.12.0 release commit; update deliberately.
      FetchContent_Declare(webview
        URL https://github.com/webview/webview/archive/3ab4b5d722438fc8a13e6ca830c5e2372d19a01d.tar.gz
        URL_HASH SHA256=3668dca249ce182803fe886c9c9e8e27d2c278a52a647198b947ed9d415019d5)
      FetchContent_MakeAvailable(webview)
    endif()
  elseif(NOT TARGET webview::core_static)
    message(FATAL_ERROR
      "CMDB_FETCH_WEBVIEW=OFF requires an existing webview::core_static target")
  endif()

  add_executable(cmdb
    src/ui/main.c
    src/ui/ui_common.c
    src/web_bundle.c)
  # Keep the native/GTK/webview shell warning-clean without making third-party
  # system-header warnings fatal.
  set(CMDB_UI_WARNINGS ${CMDB_WARNINGS})
  list(REMOVE_ITEM CMDB_UI_WARNINGS -Werror /WX)
  target_compile_options(cmdb PRIVATE ${CMDB_UI_WARNINGS})
  target_compile_definitions(cmdb PRIVATE
    CMDB_VERSION="${PROJECT_VERSION}"
    CMDB_WEB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/web"
    CMDB_INSTALL_WEB_DIR="${CMAKE_INSTALL_FULL_DATADIR}/cmdb-lite/web")
  target_link_libraries(cmdb PRIVATE cmdb_core)
  set_target_properties(cmdb PROPERTIES LINKER_LANGUAGE CXX)

  if(WIN32)
    configure_file(
      packaging/windows/cmdb-lite.rc.in
      ${CMAKE_CURRENT_BINARY_DIR}/cmdb-lite.rc
      @ONLY)
    configure_file(
      packaging/windows/cmdb-lite.ico
      ${CMAKE_CURRENT_BINARY_DIR}/cmdb-lite.ico
      COPYONLY)
    target_sources(cmdb PRIVATE
      src/ui/platform_dialog_win32.c
      ${CMAKE_CURRENT_BINARY_DIR}/cmdb-lite.rc)
    set_target_properties(cmdb PROPERTIES WIN32_EXECUTABLE ON)
    target_link_libraries(cmdb PRIVATE
      webview::core_static
      comdlg32
      ole32
      shell32
      shlwapi
      user32
      version)
  else()
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
    target_sources(cmdb PRIVATE src/ui/platform_dialog_gtk.c)
    target_link_libraries(cmdb PRIVATE
      webview::core_static
      PkgConfig::GTK3
      dl)
  endif()

  if(WIN32)
    install(TARGETS cmdb RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  else()
    install(TARGETS cmdb RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/cmdb-lite.desktop
      DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/icons/cmdb-lite.png
      DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
      RENAME cmdb-lite.png)
  endif()
  install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

if(CMDB_BUILD_TESTS)
  add_executable(cmdb_tests
    tests/test_main.c
    tests/test_api.c
    tests/test_ci.c
    tests/test_crypto.c
    tests/test_csv.c
    tests/test_drawio.c
    tests/test_export.c
    tests/test_graph.c
    tests/test_import.c
    tests/test_relationships.c
    tests/test_scan.c
    tests/test_types.c)
  target_compile_options(cmdb_tests PRIVATE ${CMDB_WARNINGS})
  target_link_libraries(cmdb_tests PRIVATE cmdb_core)
  if(NOT WIN32)
    target_compile_definitions(cmdb_tests PRIVATE _POSIX_C_SOURCE=200809L)
  endif()
  add_test(NAME cmdb_tests COMMAND cmdb_tests)
  set_tests_properties(cmdb_tests PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

if(CMDB_BUILD_FUZZERS)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "CMDB_BUILD_FUZZERS requires Clang")
  endif()
  add_executable(fuzz_csv fuzz/fuzz_csv.c)
  target_compile_options(fuzz_csv PRIVATE
    -fsanitize=fuzzer,address,undefined
    -fno-omit-frame-pointer)
  target_link_options(fuzz_csv PRIVATE
    -fsanitize=fuzzer,address,undefined)
  target_link_libraries(fuzz_csv PRIVATE cmdb_core)
endif()

if(CMDB_BUILD_UI)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_custom_target(web-bundle
      COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/packaging/embed_web.py
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Regenerating embedded web UI")
  endif()

  set(CPACK_PACKAGE_NAME "cmdb-lite")
  set(CPACK_PACKAGE_VENDOR "Roger Bergling")
  set(CPACK_PACKAGE_CONTACT "github@bergling.net")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Local-first encrypted Configuration Management Database")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "cmdb-lite")
  set(CPACK_PACKAGE_EXECUTABLES "cmdb" "cmdb-lite")

  if(WIN32)
    set(CPACK_PACKAGE_FILE_NAME
      "cmdb-lite-${PROJECT_VERSION}-windows-x64")
    set(CPACK_GENERATOR "WIX;ZIP")
    set(CPACK_WIX_UPGRADE_GUID
      "{B82FCBFC-5F09-4813-9140-371DE9B6B3AF}")
    set(CPACK_WIX_PRODUCT_ICON
      "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/cmdb-lite.ico")
    set(CPACK_WIX_PROGRAM_MENU_FOLDER "cmdb-lite")
    set(CPACK_WIX_INSTALL_SCOPE "perMachine")
  else()
    set(CPACK_PACKAGE_FILE_NAME
      "cmdb-lite-${PROJECT_VERSION}-linux-${CMAKE_SYSTEM_PROCESSOR}")
    set(CPACK_GENERATOR "TGZ;DEB;RPM")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER
      "Roger Bergling <github@bergling.net>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

    # RPM packaging for Fedora/RHEL. Update the license when the project
    # license is selected.
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
    set(CPACK_RPM_PACKAGE_DESCRIPTION
      "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}")
    set(CPACK_RPM_PACKAGE_REQUIRES
      "gtk3, webkit2gtk4.1, expat, openssl-libs, glib2, sqlcipher, libsodium")
    set(CPACK_RPM_PACKAGE_AUTOREQ ON)
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
  endif()

  include(CPack)
endif()
