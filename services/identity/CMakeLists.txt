# identity-service: registration, password verification and Ed25519 access tokens (contracts §4, §10.4).

add_library(sg_identity_core STATIC
  src/sg/identity/app/account_service.cpp
  src/sg/identity/config.cpp
  src/sg/identity/domain/registration.cpp
  src/sg/identity/http/routes.cpp
  src/sg/identity/infra/demo_seed.cpp
  src/sg/identity/infra/graph_taxonomy_client.cpp
  src/sg/identity/infra/postgres_account_repository.cpp)
target_include_directories(sg_identity_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(sg_identity_core PUBLIC sg::common)
sg_embed_migrations(sg_identity_core ${CMAKE_CURRENT_SOURCE_DIR}/migrations sg::identity::infra)
sg_set_warnings(sg_identity_core)

add_executable(identity-service src/main.cpp)
target_link_libraries(identity-service PRIVATE sg_identity_core)
sg_set_warnings(identity-service)
sg_use_allocator(identity-service)
install(TARGETS identity-service RUNTIME DESTINATION bin)

if(SG_BUILD_TESTS)
  add_subdirectory(tests)
endif()
