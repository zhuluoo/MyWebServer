add_executable(server.o)

target_sources(server.o
  PRIVATE
    main.cpp
    pool/thread_pool.cpp
    http/http_conn.cpp
    server/web_server.cpp
)

target_include_directories(server.o
  PRIVATE
    ${PROJECT_SOURCE_DIR}/include/
)