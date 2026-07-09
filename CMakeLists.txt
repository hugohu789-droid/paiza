# Paiza Mongolian IME - root build script.
#
# Layout:
#   src/common, src/core        - platform-neutral (build on Win/mac/Linux)
#   src/platform/windows        - TSF text service, D2D/DWrite UI, named-pipe
#                                 IPC, candidate service process
#   src/platform/macos, linux   - future InputMethodKit / IBus front-ends
#   tests                       - GoogleTest; portable tests run everywhere,
#                                 Windows-specific tests only on Windows
cmake_minimum_required(VERSION 3.20)
project(PaizaMongolianIme VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PAIZA_BUILD_TESTS "Build GoogleTest unit tests" ON)

if(WIN32)
  add_compile_definitions(
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
  )
endif()
if(MSVC)
  add_compile_options(/utf-8 /W4 /permissive- /EHsc)
else()
  add_compile_options(-Wall -Wextra)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(NOT WIN32)
  find_package(Threads REQUIRED)
endif()

# ------------------------------------------------------- portable: common ---
add_library(paiza_common STATIC
  src/common/logging.cc
  src/common/paths.cc
  src/common/string_utils.cc
)
target_include_directories(paiza_common PUBLIC src)
if(WIN32)
  target_link_libraries(paiza_common PRIVATE shell32 ole32)
else()
  target_link_libraries(paiza_common PUBLIC Threads::Threads)
endif()

# --------------------------------------------------------- portable: core ---
add_library(paiza_core STATIC
  src/core/candidate_engine.cc
  src/core/cloud_candidate_source.cc
  src/core/input_session.cc
  src/core/key_mapping.cc
  src/core/key_mapping_store.cc
  src/core/local_dictionary_source.cc
  src/core/localization.cc
  src/core/transliterator.cc
)
target_link_libraries(paiza_core PUBLIC paiza_common)

# ------------------------------------------------------- Windows platform ---
if(WIN32)
  set(PAIZA_WIN src/platform/windows)

  add_library(paiza_ipc STATIC
    ${PAIZA_WIN}/ipc/message.cc
    ${PAIZA_WIN}/ipc/named_pipe_client.cc
    ${PAIZA_WIN}/ipc/named_pipe_server.cc
    ${PAIZA_WIN}/ipc/ime_client.cc
  )
  target_include_directories(paiza_ipc PUBLIC ${PAIZA_WIN})
  target_link_libraries(paiza_ipc PUBLIC paiza_common)
  target_link_libraries(paiza_ipc PRIVATE advapi32)

  add_library(paiza_ui STATIC
    ${PAIZA_WIN}/ui/button.cc
    ${PAIZA_WIN}/ui/candidate_list_view.cc
    ${PAIZA_WIN}/ui/candidate_window.cc
    ${PAIZA_WIN}/ui/dropdown.cc
    ${PAIZA_WIN}/ui/event_dispatcher.cc
    ${PAIZA_WIN}/ui/font_stem_config.cc
    ${PAIZA_WIN}/ui/image_view.cc
    ${PAIZA_WIN}/ui/label.cc
    ${PAIZA_WIN}/ui/render_context.cc
    ${PAIZA_WIN}/ui/vertical_tooltip.cc
    ${PAIZA_WIN}/ui/widget.cc
    ${PAIZA_WIN}/ui/window.cc
  )
  target_include_directories(paiza_ui PUBLIC ${PAIZA_WIN})
  target_link_libraries(paiza_ui PUBLIC paiza_common)
  target_link_libraries(paiza_ui PRIVATE
    d2d1 dwrite windowscodecs user32 gdi32 dwmapi
  )

  add_library(PaizaTsf SHARED
    ${PAIZA_WIN}/tsf/class_factory.cc
    ${PAIZA_WIN}/tsf/composition_controller.cc
    ${PAIZA_WIN}/tsf/dll_main.cc
    ${PAIZA_WIN}/tsf/engine_proxy.cc
    ${PAIZA_WIN}/tsf/guids.cc
    ${PAIZA_WIN}/tsf/key_event_sink.cc
    ${PAIZA_WIN}/tsf/registration.cc
    ${PAIZA_WIN}/tsf/text_service.cc
    ${PAIZA_WIN}/tsf/paiza_tsf.def
  )
  set_target_properties(PaizaTsf PROPERTIES OUTPUT_NAME "PaizaTsf")
  target_link_libraries(PaizaTsf PRIVATE
    paiza_core paiza_ipc paiza_ui
    ole32 oleaut32 uuid advapi32 user32 shell32
  )

  add_executable(PaizaService WIN32
    ${PAIZA_WIN}/service/main.cc
    ${PAIZA_WIN}/service/candidate_service.cc
    ${PAIZA_WIN}/service/settings_store.cc
    ${PAIZA_WIN}/service/settings_window.cc
  )
  target_link_libraries(PaizaService PRIVATE
    paiza_core paiza_ipc paiza_ui
    user32 advapi32 shell32
  )

  # Copy the sample dictionary next to the binaries so the service finds it.
  add_custom_command(TARGET PaizaService POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/data/dictionary.txt
            $<TARGET_FILE_DIR:PaizaService>/dictionary.txt
  )
endif()

# --------------------------------------------------------- macOS platform ---
# InputMethodKit front-end (.app bundle for ~/Library/Input Methods).
# Thin ObjC++ adapter around paiza::InputSession; see docs/macos-port.md.
if(APPLE)
  enable_language(OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 20)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

  add_executable(PaizaIMK MACOSX_BUNDLE
    src/platform/macos/main.mm
    src/platform/macos/paiza_input_controller.mm
    src/platform/macos/candidate_panel.mm
    data/dictionary.txt
  )
  set_target_properties(PaizaIMK PROPERTIES
    OUTPUT_NAME "PaizaMongolian"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/src/platform/macos/Info.plist
  )
  set_source_files_properties(data/dictionary.txt PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources
  )
  target_include_directories(PaizaIMK PRIVATE src/platform/macos)
  target_compile_options(PaizaIMK PRIVATE -fobjc-arc)
  target_link_libraries(PaizaIMK PRIVATE
    paiza_core
    "-framework Cocoa"
    "-framework InputMethodKit"
    "-framework CoreText"
  )
endif()

# ------------------------------------------------------------------ tests ---
if(PAIZA_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  )
  # Use the shared CRT to match the rest of the project (MSVC only).
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()

  # Portable tests: run on every platform in CI (feature-parity guard).
  set(PAIZA_TEST_SOURCES
    tests/candidate_engine_test.cc
    tests/input_session_test.cc
    tests/key_mapping_test.cc
    tests/local_dictionary_source_test.cc
    tests/localization_test.cc
    tests/string_utils_test.cc
    tests/transliterator_test.cc
  )
  # Windows-only tests: named-pipe IPC, wire protocol, registry settings.
  if(WIN32)
    list(APPEND PAIZA_TEST_SOURCES
      src/platform/windows/service/settings_store.cc
      tests/ipc_roundtrip_test.cc
      tests/message_test.cc
      tests/settings_store_test.cc
    )
  endif()

  add_executable(paiza_tests ${PAIZA_TEST_SOURCES})
  target_link_libraries(paiza_tests PRIVATE paiza_core gtest gtest_main)
  if(WIN32)
    target_include_directories(paiza_tests PRIVATE src/platform/windows)
    target_link_libraries(paiza_tests PRIVATE paiza_ipc advapi32)
  endif()

  include(GoogleTest)
  gtest_discover_tests(paiza_tests)
endif()
