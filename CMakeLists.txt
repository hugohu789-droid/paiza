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
  src/core/font_stem_config.cc
  src/core/key_mapping.cc
  src/core/key_mapping_store.cc
  src/core/local_dictionary_source.cc
  src/core/localization.cc
  src/core/mapping_preview.cc
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

  set(PAIZA_MAC src/platform/macos)
  add_executable(PaizaIMK MACOSX_BUNDLE
    ${PAIZA_MAC}/main.mm
    ${PAIZA_MAC}/paiza_input_controller.mm
    ${PAIZA_MAC}/candidate_panel.mm
    ${PAIZA_MAC}/settings_store.mm
    ${PAIZA_MAC}/settings_window.mm
    data/dictionary.txt
    ${PAIZA_MAC}/resources/MenuIcon.tiff
    ${PAIZA_MAC}/en.lproj/InfoPlist.strings
    ${PAIZA_MAC}/zh-Hans.lproj/InfoPlist.strings
  )
  set_target_properties(PaizaIMK PROPERTIES
    OUTPUT_NAME "PaizaMongolian"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/src/platform/macos/Info.plist
  )
  # Bundle resources: dictionary + candidate-window icon at Resources root,
  # the InfoPlist.strings under their .lproj folders (localized name).
  set_source_files_properties(
    data/dictionary.txt
    ${PAIZA_MAC}/resources/MenuIcon.tiff
    PROPERTIES MACOSX_PACKAGE_LOCATION Resources
  )
  set_source_files_properties(${PAIZA_MAC}/en.lproj/InfoPlist.strings
    PROPERTIES MACOSX_PACKAGE_LOCATION Resources/en.lproj)
  set_source_files_properties(${PAIZA_MAC}/zh-Hans.lproj/InfoPlist.strings
    PROPERTIES MACOSX_PACKAGE_LOCATION Resources/zh-Hans.lproj)
  target_include_directories(PaizaIMK PRIVATE src/platform/macos)
  target_compile_options(PaizaIMK PRIVATE -fobjc-arc)
  target_link_libraries(PaizaIMK PRIVATE
    paiza_core
    "-framework Cocoa"
    "-framework InputMethodKit"
    "-framework CoreText"
  )

  # Developer tool: offscreen candidate-panel preview (PNG snapshots, no
  # install needed). See docs/macos-port.md "Developer commands".
  add_executable(paiza_panel_preview
    tools/macos/panel_preview.mm
    ${PAIZA_MAC}/candidate_panel.mm
  )
  target_include_directories(paiza_panel_preview PRIVATE src/platform/macos)
  target_compile_options(paiza_panel_preview PRIVATE -fobjc-arc)
  target_link_libraries(paiza_panel_preview PRIVATE
    paiza_core
    "-framework Cocoa"
    "-framework CoreText"
  )
endif()

# --------------------------------------------------------- Linux platform ---
# IBus engine + GTK settings dialog; see docs/linux-port.md. Auto-skipped
# when the dev packages are missing so plain core builds keep working
# (CI's ubuntu job installs libibus-1.0-dev + libgtk-3-dev).
if(UNIX AND NOT APPLE)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(IBUS IMPORTED_TARGET ibus-1.0)
    pkg_check_modules(GTK3 IMPORTED_TARGET gtk+-3.0)
  endif()
  if(IBUS_FOUND AND GTK3_FOUND)
    set(PAIZA_LINUX src/platform/linux)
    set(PAIZA_INSTALL_BIN_DIR ${CMAKE_INSTALL_PREFIX}/bin)
    set(PAIZA_INSTALL_DATA_DIR ${CMAKE_INSTALL_PREFIX}/share/ibus-paiza)

    add_executable(ibus-engine-paiza
      ${PAIZA_LINUX}/main.cc
      ${PAIZA_LINUX}/engine.cc
      ${PAIZA_LINUX}/settings_store.cc
    )
    target_include_directories(ibus-engine-paiza PRIVATE ${PAIZA_LINUX})
    target_compile_definitions(ibus-engine-paiza PRIVATE
      PAIZA_DATA_DIR="${PAIZA_INSTALL_DATA_DIR}")
    target_link_libraries(ibus-engine-paiza PRIVATE
      paiza_core PkgConfig::IBUS)

    add_executable(paiza-ime-settings
      ${PAIZA_LINUX}/settings_main.cc
      ${PAIZA_LINUX}/settings_store.cc
    )
    target_include_directories(paiza-ime-settings PRIVATE ${PAIZA_LINUX})
    target_link_libraries(paiza-ime-settings PRIVATE
      paiza_core PkgConfig::GTK3)

    # Component XML with the install paths baked in.
    configure_file(${PAIZA_LINUX}/paiza.xml.in
                   ${CMAKE_BINARY_DIR}/paiza.xml @ONLY)

    # Build-tree runs find the dictionary next to the binary.
    add_custom_command(TARGET ibus-engine-paiza POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${CMAKE_SOURCE_DIR}/data/dictionary.txt
              $<TARGET_FILE_DIR:ibus-engine-paiza>/dictionary.txt
    )

    install(TARGETS ibus-engine-paiza paiza-ime-settings
            RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_BINARY_DIR}/paiza.xml
            DESTINATION share/ibus/component)
    install(FILES data/dictionary.txt
                  ${PAIZA_LINUX}/resources/paiza-mn.png
            DESTINATION share/ibus-paiza)
  else()
    message(STATUS
      "Paiza: ibus-1.0/gtk+-3.0 dev packages not found — "
      "skipping the Linux IME front-end (core + tests still build)")
  endif()
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
  # The Linux INI settings store is pure standard C++, so its tests run
  # everywhere too — Windows/macOS CI guards the Linux settings logic.
  set(PAIZA_TEST_SOURCES
    src/platform/linux/settings_store.cc
    tests/candidate_engine_test.cc
    tests/input_session_test.cc
    tests/key_mapping_test.cc
    tests/linux_settings_store_test.cc
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
  target_include_directories(paiza_tests PRIVATE src/platform/linux)
  target_link_libraries(paiza_tests PRIVATE paiza_core gtest gtest_main)
  if(WIN32)
    target_include_directories(paiza_tests PRIVATE src/platform/windows)
    target_link_libraries(paiza_tests PRIVATE paiza_ipc advapi32)
  endif()

  include(GoogleTest)
  gtest_discover_tests(paiza_tests)
endif()
