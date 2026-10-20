# Memory allocator of the service binaries. mimalloc replaces glibc malloc process-wide (it is
# linked so that its malloc/free interpose libc's); request handling allocates many short-lived
# strings, JSON nodes and result rows across threads, where it measured faster than glibc and
# jemalloc on the mixed load test (docs/performance.md). Sanitizer builds keep the system
# allocator, which the sanitizers replace themselves.
set(SG_ALLOCATOR "auto" CACHE STRING "Allocator for the services: auto (mimalloc when found), mimalloc or system")
set_property(CACHE SG_ALLOCATOR PROPERTY STRINGS auto mimalloc system)

set(_sg_use_mimalloc OFF)
if(NOT SG_ALLOCATOR STREQUAL "system" AND NOT SG_SANITIZE)
  if(SG_ALLOCATOR STREQUAL "mimalloc")
    find_package(mimalloc 2.0 CONFIG REQUIRED)
  else()
    find_package(mimalloc 2.0 CONFIG QUIET)
  endif()
  if(TARGET mimalloc)
    set(_sg_use_mimalloc ON)
  endif()
endif()
if(_sg_use_mimalloc)
  message(STATUS "Service allocator: mimalloc ${mimalloc_VERSION}")
else()
  message(STATUS "Service allocator: system malloc")
endif()

# Links the chosen allocator into an executable. --no-as-needed keeps the library although no
# symbol of it is referenced by name: it is needed for the malloc it provides.
function(sg_use_allocator target)
  if(_sg_use_mimalloc)
    target_link_libraries(${target} PRIVATE "-Wl,--push-state,--no-as-needed" mimalloc "-Wl,--pop-state")
  endif()
endfunction()
