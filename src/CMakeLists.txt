add_executable(server.o)

target_sources(server.o
  PRIVATE
    main.cpp
    config/global_config.cpp
    http/http_conn.cpp
    pool/thread_pool.cpp
    server/web_server.cpp
    utils/resource_utils.cpp
)

target_include_directories(server.o
  PRIVATE
    ${PROJECT_SOURCE_DIR}/include/
)

target_link_libraries(server.o
  PRIVATE
    ws2_32
)

add_custom_command(
  TARGET server.o
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/resources
          $<TARGET_FILE_DIR:server.o>/resources
)

# Specify installation rules (copying files to system directories on make install).
install(TARGETS server.o DESTINATION bin)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/resources/ DESTINATION bin/resources)