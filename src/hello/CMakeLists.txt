# Module: hello — Windows Hello unlock (UserConsentVerifier + DPAPI)

target_sources(passvault PRIVATE
    windows_hello_unlock.h
    windows_hello_unlock.cc
)

set(_passvault_hello_use_winrt OFF)

if(WIN32)
    include(CheckIncludeFileCXX)
    check_include_file_cxx(
        "winrt/Windows.Security.Credentials.UI.h"
        PASSVAULT_HELLO_HAS_WINRT_HEADERS
    )

    if(PASSVAULT_HELLO_HAS_WINRT_HEADERS)
        find_library(PASSVAULT_HELLO_WINDOWSAPP_LIB
            NAMES windowsapp WindowsApp
        )
        if(PASSVAULT_HELLO_WINDOWSAPP_LIB)
            set(_passvault_hello_use_winrt ON)
        endif()
    endif()
endif()

if(_passvault_hello_use_winrt)
    target_sources(passvault PRIVATE windows_hello_unlock_winrt.cc)
    target_link_libraries(passvault PRIVATE
        "${PASSVAULT_HELLO_WINDOWSAPP_LIB}"
    )
    message(STATUS
        "hello module: C++/WinRT UserConsentVerifier backend"
        " (${PASSVAULT_HELLO_WINDOWSAPP_LIB})"
    )
else()
    target_sources(passvault PRIVATE windows_hello_unlock_stub.cc)
    message(STATUS
        "hello module: stub backend (WinRT headers or windowsapp lib missing)"
    )
endif()
