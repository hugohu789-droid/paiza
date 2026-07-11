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
project(PaizaMongolianIme VERSION 0.1.0 LANGUAGES C CXX)

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

# -------------------------------------------------- third-party: SQLite ----
# Vendored amalgamation (public domain), used by core/user_dictionary for
# offline persistence only — never on the typing path.
add_library(paiza_sqlite STATIC third_party/sqlite/sqlite3.c)
target_include_directories(paiza_sqlite PUBLIC .)
target_compile_definitions(paiza_sqlite PRIVATE
  SQLITE_OMIT_LOAD_EXTENSION   # No dlopen/-ldl dependency.
  SQLITE_DQS=0
)
if(MSVC)
  target_compile_options(paiza_sqlite PRIVATE /w)
else()
  target_compile_options(paiza_sqlite PRIVATE -w)
endif()

# ------------------------------------------------------- portable: common ---
add_library(paiza_common STATIC
  src/common/logging.cc
  src/common/mapped_file.cc
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
  src/core/dawg.cc
  src/core/engine_c_api.cc
  src/core/equivalence_table.cc
  src/core/input_session.cc
  src/core/font_stem_config.cc
  src/core/key_mapping.cc
  src/core/key_mapping_store.cc
  src/core/levenshtein_automaton.cc
  src/core/lexicon_binary.cc
  src/core/lexicon_compiler.cc
  src/core/local_dictionary_source.cc
  src/core/localization.cc
  src/core/mapping_preview.cc
  src/core/scorer.cc
  src/core/static_lexicon.cc
  src/core/transliterator.cc
  src/core/user_dictionary.cc
)
target_link_libraries(paiza_core PUBLIC paiza_common)
target_link_libraries(paiza_core PRIVATE paiza_sqlite)

# ----------------------------------------------- offline lexicon compiler ---
# Build-time tool: compiles data/dictionary.txt (+ optional data/bigrams.txt)
# into the binary lexicon.bin that StaticLexicon memory-maps at runtime.
add_executable(compile_lexicon tools/compile_lexicon/main.cc)
target_link_libraries(compile_lexicon PRIVATE paiza_core)

# Developer tools: performance harness (Phase 3 P99 acceptance) and a
# scoring-weight evaluator. Built on demand, not part of ALL.
add_executable(lexicon_bench EXCLUDE_FROM_ALL tools/lexicon_bench/main.cc)
target_link_libraries(lexicon_bench PRIVATE paiza_core)
add_executable(tune_scoring EXCLUDE_FROM_ALL tools/tune_scoring/main.cc)
target_link_libraries(tune_scoring PRIVATE paiza_core)
# Headless query REPL: type a key sequence, get the candidate list back.
add_executable(paiza_query EXCLUDE_FROM_ALL tools/paiza_query/main.cc)
target_link_libraries(paiza_query PRIVATE paiza_core)

set(PAIZA_LEXICON_BIN ${CMAKE_BINARY_DIR}/lexicon.bin)
set(PAIZA_COMPILE_ARGS
  --out ${PAIZA_LEXICON_BIN}
  --dict ${CMAKE_SOURCE_DIR}/data/dictionary.txt)
set(PAIZA_LEXICON_DEPS
  compile_lexicon ${CMAKE_SOURCE_DIR}/data/dictionary.txt)
if(EXISTS ${CMAKE_SOURCE_DIR}/data/bigrams.txt)
  list(APPEND PAIZA_COMPILE_ARGS --bigram ${CMAKE_SOURCE_DIR}/data/bigrams.txt)
  list(APPEND PAIZA_LEXICON_DEPS ${CMAKE_SOURCE_DIR}/data/bigrams.txt)
endif()
add_custom_command(OUTPUT ${PAIZA_LEXICON_BIN}
  COMMAND compile_lexicon ${PAIZA_COMPILE_ARGS}
  DEPENDS ${PAIZA_LEXICON_DEPS}
  COMMENT "Compiling static lexicon -> lexicon.bin"
  VERBATIM)
add_custom_target(paiza_lexicon ALL DEPENDS ${PAIZA_LEXICON_BIN})

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
    user32 advapi32 shell32 comdlg32
  )

  # Copy the sample dictionary + compiled lexicon next to the binaries so the
  # service finds them.
  add_dependencies(PaizaService paiza_lexicon)
  add_custom_command(TARGET PaizaService POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/data/dictionary.txt
            $<TARGET_FILE_DIR:PaizaService>/dictionary.txt
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PAIZA_LEXICON_BIN}
            $<TARGET_FILE_DIR:PaizaService>/lexicon.bin
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
    ${PAIZA_LEXICON_BIN}
    ${PAIZA_MAC}/resources/MenuIcon.tiff
    ${PAIZA_MAC}/en.lproj/InfoPlist.strings
    ${PAIZA_MAC}/zh-Hans.lproj/InfoPlist.strings
  )
  add_dependencies(PaizaIMK paiza_lexicon)
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
  set_source_files_properties(${PAIZA_LEXICON_BIN}
    PROPERTIES GENERATED TRUE MACOSX_PACKAGE_LOCATION Resources)
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

    # ibus-daemon scans exactly one directory for component XML: its own
    # pkgdatadir/component (usually /usr/share/ibus/component), or
    # $IBUS_COMPONENT_PATH when set. It does NOT scan
    # <prefix>/share/ibus/component, so with the default /usr/local
    # prefix the engine would never appear in `ibus list-engine` or
    # GNOME Settings. Ask ibus itself where it looks.
    pkg_get_variable(PAIZA_IBUS_PKGDATADIR ibus-1.0 pkgdatadir)
    if(NOT PAIZA_IBUS_PKGDATADIR)
      set(PAIZA_IBUS_PKGDATADIR "/usr/share/ibus")
    endif()
    set(PAIZA_IBUS_COMPONENT_DIR "${PAIZA_IBUS_PKGDATADIR}/component"
        CACHE PATH "Directory ibus-daemon scans for component XML files")

    add_executable(ibus-engine-paiza
      ${PAIZA_LINUX}/main.cc
      ${PAIZA_LINUX}/engine.cc
      ${PAIZA_LINUX}/candidate_renderer.cc
      ${PAIZA_LINUX}/candidate_window.cc
      ${PAIZA_LINUX}/settings_store.cc
    )
    target_include_directories(ibus-engine-paiza PRIVATE ${PAIZA_LINUX})
    target_compile_definitions(ibus-engine-paiza PRIVATE
      PAIZA_DATA_DIR="${PAIZA_INSTALL_DATA_DIR}")
    # GTK3: the v2 custom vertical candidate window (X11; Wayland falls
    # back to the system IBusLookupTable panel — see docs/linux-port.md).
    target_link_libraries(ibus-engine-paiza PRIVATE
      paiza_core PkgConfig::IBUS PkgConfig::GTK3)

    # Offscreen candidate-window preview: renders PNGs with a plain cairo
    # image surface (no display needed), for iterating on the visuals in
    # CI/containers. pangocairo is a GTK3 dependency, so it is present.
    pkg_check_modules(PANGOCAIRO IMPORTED_TARGET pangocairo)
    if(PANGOCAIRO_FOUND)
      add_executable(paiza_candidate_preview
        tools/linux/candidate_preview.cc
        ${PAIZA_LINUX}/candidate_renderer.cc
      )
      target_include_directories(paiza_candidate_preview PRIVATE
        ${PAIZA_LINUX})
      target_link_libraries(paiza_candidate_preview PRIVATE
        paiza_core PkgConfig::PANGOCAIRO)
    endif()

    add_executable(paiza-ime-settings
      ${PAIZA_LINUX}/settings_main.cc
      ${PAIZA_LINUX}/settings_store.cc
    )
    target_include_directories(paiza-ime-settings PRIVATE ${PAIZA_LINUX})
    target_link_libraries(paiza-ime-settings PRIVATE
      paiza_core PkgConfig::GTK3)

    # End-to-end typing smoke test: an IBus client that types through a
    # (private) daemon + the engine process and checks the committed
    # Mongolian text — the no-GUI stand-in for real desktop typing.
    add_executable(paiza_typing_smoke tools/linux/typing_smoke.cc)
    target_link_libraries(paiza_typing_smoke PRIVATE PkgConfig::IBUS)

    # Component XML with the install paths baked in.
    configure_file(${PAIZA_LINUX}/paiza.xml.in
                   ${CMAKE_BINARY_DIR}/paiza.xml @ONLY)

    # Build-tree runs find the dictionary + compiled lexicon next to the
    # binary.
    add_dependencies(ibus-engine-paiza paiza_lexicon)
    add_custom_command(TARGET ibus-engine-paiza POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${CMAKE_SOURCE_DIR}/data/dictionary.txt
              $<TARGET_FILE_DIR:ibus-engine-paiza>/dictionary.txt
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${PAIZA_LEXICON_BIN}
              $<TARGET_FILE_DIR:ibus-engine-paiza>/lexicon.bin
    )

    install(TARGETS ibus-engine-paiza paiza-ime-settings
            RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_BINARY_DIR}/paiza.xml
            DESTINATION ${PAIZA_IBUS_COMPONENT_DIR})
    install(FILES data/dictionary.txt
                  ${PAIZA_LEXICON_BIN}
                  ${PAIZA_LINUX}/resources/paiza-mn.png
            DESTINATION share/ibus-paiza)
    # GNOME Shell extension: vertical Mongolian in the shell's IBus
    # candidate popup (the Wayland rendering path). <prefix>/share is on
    # XDG_DATA_DIRS, so the shell finds it after the next login; per-user
    # installs can copy it to ~/.local/share/gnome-shell/extensions.
    install(DIRECTORY ${PAIZA_LINUX}/gnome-extension/paiza-mongolian@paiza.im
            DESTINATION share/gnome-shell/extensions)
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
    tests/dawg_test.cc
    tests/engine_c_api_test.cc
    tests/equivalence_table_test.cc
    tests/input_session_test.cc
    tests/key_mapping_test.cc
    tests/levenshtein_automaton_test.cc
    tests/linux_settings_store_test.cc
    tests/local_dictionary_source_test.cc
    tests/localization_test.cc
    tests/scorer_test.cc
    tests/static_lexicon_test.cc
    tests/string_utils_test.cc
    tests/transliterator_test.cc
    tests/user_dictionary_test.cc
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
