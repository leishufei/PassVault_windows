if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sqlite3.c")
    message(FATAL_ERROR
        "sqlite3 amalgamation not found at ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3.c. "
        "Download sqlite-amalgamation from https://www.sqlite.org/download.html "
        "and unpack sqlite3.c + sqlite3.h into third_party/sqlite3/.")
endif()

enable_language(C)

add_library(sqlite3_amalgamation STATIC EXCLUDE_FROM_ALL
    sqlite3.c
)

set_target_properties(sqlite3_amalgamation PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

target_include_directories(sqlite3_amalgamation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_definitions(sqlite3_amalgamation PUBLIC
    SQLITE_THREADSAFE=1
    SQLITE_ENABLE_FTS5
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
    SQLITE_ENABLE_STAT4
    SQLITE_MAX_EXPR_DEPTH=0
    SQLITE_OMIT_DEPRECATED
    SQLITE_OMIT_SHARED_CACHE
    SQLITE_USE_URI=1
)

if(MSVC)
    target_compile_options(sqlite3_amalgamation PRIVATE /W0)
else()
    target_compile_options(sqlite3_amalgamation PRIVATE -w)
endif()
