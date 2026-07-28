target_sources(passvault PRIVATE
    pkce.h
    pkce.cc
    loopback_http_server.h
    loopback_http_server.cc
    token_store.h
    token_store.cc
    google_oauth_client.h
    google_oauth_client.cc
)

target_link_libraries(passvault PRIVATE Qt6::Network)
