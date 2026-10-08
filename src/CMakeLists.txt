add_executable(myserver)

target_compile_features(myserver
  PRIVATE
    cxx_std_20
)

target_include_directories(myserver
  PRIVATE
    ${PROJECT_SOURCE_DIR}/include/
)

target_sources(myserver
  PRIVATE
    main.cpp
    config/global_config.cpp
    http/http_conn.cpp
    pool/thread_pool.cpp
    server/web_server.cpp
    utils/resource_utils.cpp
    utils/base64.cpp
    logger/logger.cpp
)

add_custom_command(
  TARGET myserver
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/resources
          $<TARGET_FILE_DIR:myserver>/resources
  COMMENT "Copying resources next to server executable"
  VERBATIM
)

# Specify installation rules (copying files to system directories on make install).
install(TARGETS myserver DESTINATION bin)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/resources/ DESTINATION bin/resources)
