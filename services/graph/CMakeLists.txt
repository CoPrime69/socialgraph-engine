# graph-service: profiles and the connection graph (contracts §5, §10.3, §10.5).

add_library(sg_graph_service_core STATIC
  src/sg/graph_service/app/graph_service.cpp
  src/sg/graph_service/config.cpp
  src/sg/graph_service/domain/affinity.cpp
  src/sg/graph_service/domain/events.cpp
  src/sg/graph_service/domain/profile.cpp
  src/sg/graph_service/domain/taxonomy.cpp
  src/sg/graph_service/domain/user_query.cpp
  src/sg/graph_service/http/routes.cpp
  src/sg/graph_service/infra/identity_events_consumer.cpp
  src/sg/graph_service/infra/postgres_graph_repository.cpp
  src/sg/graph_service/infra/seed_loader.cpp)
target_include_directories(sg_graph_service_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(sg_graph_service_core PUBLIC sg::common sg::graphcore)
sg_embed_migrations(sg_graph_service_core ${CMAKE_CURRENT_SOURCE_DIR}/migrations sg::graph_service::infra)
sg_set_warnings(sg_graph_service_core)

add_executable(graph-service src/main.cpp)
target_link_libraries(graph-service PRIVATE sg_graph_service_core)
sg_set_warnings(graph-service)
install(TARGETS graph-service RUNTIME DESTINATION bin)

if(SG_BUILD_TESTS)
  add_subdirectory(tests)
endif()
