find_package(GTest CONFIG REQUIRED)
find_package(Qt6 6.5 COMPONENTS Core Gui Network Test Widgets Svg REQUIRED)

add_executable(passvault_tests
    test_main.cc
    smoke_test.cc
    key_derivation_test.cc
    crypto_service_test.cc
    cloud_crypto_service_test.cc
    database_test.cc
    password_dao_test.cc
    category_dao_test.cc
    master_password_store_test.cc
    master_password_manager_test.cc
    session_manager_test.cc
    auto_lock_timer_test.cc
    csv_writer_test.cc
    csv_reader_test.cc
    csv_validator_test.cc
    csv_exporter_importer_test.cc
    password_generator_test.cc
    password_strength_test.cc
    pkce_test.cc
    loopback_http_server_test.cc
    token_store_test.cc
    merge_algorithm_test.cc
    sync_scheduler_test.cc
    sync_manager_test.cc
    windows_hello_unlock_test.cc
    toast_test.cc
    generator_dialog_test.cc
    preferences_page_test.cc
    master_password_dialog_test.cc
    editor_panel_test.cc
    detail_panel_test.cc
    main_window_test.cc
    import_preview_dialog_test.cc
    "${CMAKE_SOURCE_DIR}/src/hello/windows_hello_unlock.cc"
    "${CMAKE_SOURCE_DIR}/src/hello/windows_hello_unlock_stub.cc"
    "${CMAKE_SOURCE_DIR}/src/crypto/secure_bytes.cc"
    "${CMAKE_SOURCE_DIR}/src/crypto/random.cc"
    "${CMAKE_SOURCE_DIR}/src/crypto/sha256.cc"
    "${CMAKE_SOURCE_DIR}/src/crypto/key_derivation.cc"
    "${CMAKE_SOURCE_DIR}/src/crypto/session_key.cc"
    "${CMAKE_SOURCE_DIR}/src/crypto/crypto_service.cc"
    "${CMAKE_SOURCE_DIR}/src/model/cloud_backup_format.cc"
    "${CMAKE_SOURCE_DIR}/src/model/sync_payload_v2.cc"
    "${CMAKE_SOURCE_DIR}/src/sync/cloud_crypto_service.cc"
    "${CMAKE_SOURCE_DIR}/src/sync/google_drive_provider.cc"
    "${CMAKE_SOURCE_DIR}/src/sync/sync_manager.cc"
    "${CMAKE_SOURCE_DIR}/src/sync/sync_scheduler.cc"
    "${CMAKE_SOURCE_DIR}/src/storage/database.cc"
    "${CMAKE_SOURCE_DIR}/src/storage/schema.cc"
    "${CMAKE_SOURCE_DIR}/src/storage/statement.cc"
    "${CMAKE_SOURCE_DIR}/src/storage/password_dao.cc"
    "${CMAKE_SOURCE_DIR}/src/storage/category_dao.cc"
    "${CMAKE_SOURCE_DIR}/src/master_password/master_password_store.cc"
    "${CMAKE_SOURCE_DIR}/src/master_password/master_password_manager.cc"
    "${CMAKE_SOURCE_DIR}/src/session/session_manager.cc"
    "${CMAKE_SOURCE_DIR}/src/session/auto_lock_timer.cc"
    "${CMAKE_SOURCE_DIR}/src/csv/csv_writer.cc"
    "${CMAKE_SOURCE_DIR}/src/csv/csv_reader.cc"
    "${CMAKE_SOURCE_DIR}/src/csv/csv_validator.cc"
    "${CMAKE_SOURCE_DIR}/src/csv/csv_exporter.cc"
    "${CMAKE_SOURCE_DIR}/src/csv/csv_importer.cc"
    "${CMAKE_SOURCE_DIR}/src/generator/password_generator.cc"
    "${CMAKE_SOURCE_DIR}/src/oauth/pkce.cc"
    "${CMAKE_SOURCE_DIR}/src/oauth/loopback_http_server.cc"
    "${CMAKE_SOURCE_DIR}/src/oauth/token_store.cc"
    "${CMAKE_SOURCE_DIR}/src/ui/toast.cc"
    "${CMAKE_SOURCE_DIR}/src/ui/generator_dialog.cc"
    "${CMAKE_SOURCE_DIR}/src/ui/preferences_page.cc"
    "${CMAKE_SOURCE_DIR}/src/ui/master_password_dialog.cc"
    "${CMAKE_SOURCE_DIR}/src/ui/editor_panel.cc"
    "${CMAKE_SOURCE_DIR}/src/ui/detail_panel.cc"
    "${CMAKE_SOURCE_DIR}/src/ui/main_window.cc"
    "${CMAKE_SOURCE_DIR}/src/ui/import_preview_dialog.cc"
    "${CMAKE_SOURCE_DIR}/src/ui/theme_manager.cc"
    "${CMAKE_SOURCE_DIR}/src/ui/icon_loader.cc"
    "${CMAKE_SOURCE_DIR}/src/ui/clipboard_manager.cc"
)

# Mirror resources/CMakeLists.txt so tests can load :/qss, :/fonts, :/icons via ThemeManager and QFontDatabase.
set(_test_qss_files
    ${CMAKE_SOURCE_DIR}/resources/qss/tokens_dark.qss
    ${CMAKE_SOURCE_DIR}/resources/qss/tokens_light.qss
    ${CMAKE_SOURCE_DIR}/resources/qss/components.qss
    ${CMAKE_SOURCE_DIR}/resources/qss/app.qss
)
file(GLOB _test_icon_files CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/resources/icons/*.svg")
file(GLOB _test_font_files CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/resources/fonts/*.ttf")
qt_add_resources(passvault_tests "passvault_tests_resources"
    PREFIX "/"
    BASE   "${CMAKE_SOURCE_DIR}/resources"
    FILES  ${_test_qss_files} ${_test_icon_files} ${_test_font_files}
)

target_include_directories(passvault_tests
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${PASSVAULT_GENERATED_DIR}"
)

target_link_libraries(passvault_tests
    PRIVATE
        GTest::gtest
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Test
        Qt6::Widgets
        Qt6::Svg
        OpenSSL::Crypto
        sqlite3_amalgamation
)

if(WIN32)
    target_link_libraries(passvault_tests PRIVATE bcrypt crypt32)
endif()

# Qt Test is only linked by the test target, so the application deployment
# step does not copy it. Test discovery runs the executable immediately after
# linking and therefore needs the DLL beside the executable.
add_custom_command(TARGET passvault_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Qt6::Test>"
        "$<TARGET_FILE_DIR:passvault_tests>/"
    VERBATIM)

# GUI tests run under QApplication with the offscreen platform. Deploy the
# offscreen plugin next to the test binary so it can start headless during
# both test discovery and execution.
if(TARGET Qt6::QOffscreenIntegrationPlugin)
    add_custom_command(TARGET passvault_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:passvault_tests>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>"
            "$<TARGET_FILE_DIR:passvault_tests>/platforms/"
        VERBATIM)
endif()

include(GoogleTest)
gtest_discover_tests(passvault_tests)
