project(keystone-eapp C ASM)

set(CFLAGS      "-Wall -Werror")
set(LDFLAGS     "-static")

set(SOURCE_FILES
  crt.S
  encret.s
  string.c
  syscall.c
  tiny-malloc.c
  )

set(INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/include/app)

set(CMAKE_C_FLAGS          "${CMAKE_C_FLAGS} ${CFLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${LDFLAGS}")

include_directories(${INCLUDE_DIRS})

add_library(${PROJECT_NAME} STATIC ${SOURCE_FILES})
# These archives are linked into freestanding Eyrie/native enclave binaries.
target_compile_options(${PROJECT_NAME} PRIVATE
  -fno-stack-protector -ffreestanding -fno-builtin)
# GCC must not lower our libc implementation's byte loops to libc calls:
# an unaligned memset loop would otherwise become a recursive memset call.
set_source_files_properties(string.c PROPERTIES COMPILE_OPTIONS
  "$<$<C_COMPILER_ID:GNU>:-fno-tree-loop-distribute-patterns>")
set_target_properties(${PROJECT_NAME} PROPERTIES DEFINE_SYMBOL "")

install(TARGETS ${PROJECT_NAME} DESTINATION ${out_dir}/lib)
install(DIRECTORY ${INCLUDE_DIRS} DESTINATION ${out_dir}/include)
