# For CLion (PC build only). The PS5 payload is built with `make ps5`.
cmake_minimum_required(VERSION 3.20)
project(ProsperoGallery C)

set(CMAKE_C_STANDARD 11)

set(GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
set(EMBEDDED)

# Turn a file into a C array header, the same way the Makefile does.
foreach(file web/index.html web/connect.html assets/icon0.png)
    string(REGEX REPLACE "[/.]" "_" name ${file})
    add_custom_command(
            OUTPUT ${GEN}/${name}.h
            COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN}
            COMMAND xxd -i ${file} > ${GEN}/${name}.h
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${file})
    list(APPEND EMBEDDED ${GEN}/${name}.h)
endforeach()

file(GLOB SOURCES src/*.c src/*.h)
add_executable(ProsperoGallery ${SOURCES}
        third_party/qrcodegen/qrcodegen.c
        third_party/qrcodegen/qrcodegen.h
        ${EMBEDDED})
target_include_directories(ProsperoGallery PRIVATE src third_party/qrcodegen ${GEN})

find_package(Threads REQUIRED)
target_link_libraries(ProsperoGallery Threads::Threads)
