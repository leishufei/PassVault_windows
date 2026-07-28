qt_add_executable(passvault WIN32
    main.cpp
)

target_include_directories(passvault
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${PASSVAULT_GENERATED_DIR}"
)

target_link_libraries(passvault
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
        Qt6::Svg
        OpenSSL::Crypto
        OpenSSL::SSL
        sqlite3_amalgamation
)

if(WIN32)
    target_link_libraries(passvault
        PRIVATE
            crypt32
            bcrypt
            ncrypt
    )
endif()

set_target_properties(passvault PROPERTIES
    OUTPUT_NAME "PassVault"
    WIN32_EXECUTABLE ON
)

add_subdirectory(model)
add_subdirectory(crypto)
add_subdirectory(storage)
add_subdirectory(master_password)
add_subdirectory(session)
add_subdirectory(csv)
add_subdirectory(generator)
add_subdirectory(oauth)
add_subdirectory(sync)
add_subdirectory(hello)
add_subdirectory(ui)
add_subdirectory(app)

if(WIN32)
    add_custom_command(TARGET passvault POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_RUNTIME_DLLS:passvault>"
            "$<TARGET_FILE_DIR:passvault>"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )

    if(NOT DEFINED PASSVAULT_WINDEPLOYQT)
        if(TARGET Qt6::qmake)
            get_target_property(_passvault_qmake_bin Qt6::qmake IMPORTED_LOCATION)
            get_filename_component(_passvault_qt_bin_dir "${_passvault_qmake_bin}" DIRECTORY)
        endif()
        find_program(PASSVAULT_WINDEPLOYQT
            NAMES windeployqt windeployqt.exe
            HINTS "${_passvault_qt_bin_dir}"
        )
    endif()

    if(PASSVAULT_WINDEPLOYQT)
        add_custom_command(TARGET passvault POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E env
                "PATH=${_passvault_qt_bin_dir};$ENV{PATH}"
                "${PASSVAULT_WINDEPLOYQT}"
                    --release
                    --compiler-runtime
                    --no-translations
                    --no-opengl-sw
                    --no-system-d3d-compiler
                    "$<TARGET_FILE:passvault>"
            COMMENT "windeployqt: deploying Qt runtime next to PassVault.exe"
            VERBATIM
        )
    else()
        message(WARNING
            "windeployqt not found. Set -DPASSVAULT_WINDEPLOYQT=<path> "
            "or run it manually after each build.")
    endif()
endif()
