cmake_minimum_required(VERSION 3.19)
project(ChatServer VERSION 1.0.0)

enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Please use a separate build directory.")
endif()

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Network
    WebSockets
    Sql
    Test
    Concurrent
)

set(SODIUM_ROOT "C:/libsodium/libsodium-win64" CACHE PATH "libsodium install root")

find_path(SODIUM_INCLUDE_DIR
    NAMES sodium.h
    HINTS ${SODIUM_ROOT}/include
    REQUIRED
)

find_library(SODIUM_LIBRARY
    NAMES libsodium.a
    HINTS ${SODIUM_ROOT}/lib
    REQUIRED
)

message(STATUS "libsodium include: ${SODIUM_INCLUDE_DIR}")
message(STATUS "libsodium library: ${SODIUM_LIBRARY}")

set(SOURCES
    src/main.cpp
    src/server/chatserver.cpp
    src/server/clientconnection.cpp
    src/server/roommanager.cpp
    src/server/messageprotocol.cpp
    src/server/presencemanager.cpp
    src/server/ratelimiter.cpp
    src/server/healthserver.cpp
    src/database/databasemanager.cpp
    src/database/databaseworker.cpp
    src/database/messagerepository.cpp
    src/database/privatemessagerepository.cpp
    src/database/transactionguard.cpp
    src/auth/authmanager.cpp
    src/auth/passwordhasher.cpp
    src/config/serverconfig.cpp
    src/logging/filelogger.cpp
    src/compat/memset_explicit.c
)

set(HEADERS
    src/server/chatserver.h
    src/server/clientconnection.h
    src/server/roommanager.h
    src/server/messageprotocol.h
    src/server/presencemanager.h
    src/server/ratelimiter.h
    src/server/healthserver.h
    src/database/databasemanager.h
    src/database/IMessageRepository.h
    src/database/IPrivateMessageRepository.h
    src/database/databaseworker.h
    src/database/messagerepository.h
    src/database/privatemessagerepository.h
    src/database/transactionguard.h
    src/database/SqlDateHelper.h
    src/auth/AuthResult.h
    src/auth/IAuthenticator.h
    src/auth/authmanager.h
    src/auth/passwordhasher.h
    src/models/User.h
    src/models/Message.h
    src/models/PrivateMessage.h
    src/models/ChatRoom.h
    src/config/serverconfig.h
    src/logging/filelogger.h
)

add_executable(chat-server ${SOURCES} ${HEADERS})

target_include_directories(chat-server PRIVATE
    src
    src/server
    src/database
    src/auth
    src/models
    src/config
    src/logging
    ${SODIUM_INCLUDE_DIR}
)

target_link_libraries(chat-server
    Qt6::Core
    Qt6::Network
    Qt6::WebSockets
    Qt6::Sql
    Qt6::Concurrent
    ${SODIUM_LIBRARY}
)

if(MSVC)
    target_compile_options(chat-server PRIVATE /W4)
else()
    target_compile_options(chat-server PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(TARGET chat-server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/config
        $<TARGET_FILE_DIR:chat-server>/config
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/certs
        $<TARGET_FILE_DIR:chat-server>/certs
)


add_executable(test-message-protocol
    tests/testmessageprotocol.cpp
    src/server/messageprotocol.cpp
)

target_include_directories(test-message-protocol PRIVATE
    src
    src/server
)

target_link_libraries(test-message-protocol
    Qt6::Core
    Qt6::Test
)

add_test(NAME message_protocol COMMAND test-message-protocol)

add_executable(test-password-hasher
    tests/test_passwordhasher.cpp
    src/auth/passwordhasher.cpp
    src/compat/memset_explicit.c
)

target_include_directories(test-password-hasher PRIVATE
    src
    src/auth
    ${SODIUM_INCLUDE_DIR}
)

target_link_libraries(test-password-hasher
    Qt6::Core
    Qt6::Test
    ${SODIUM_LIBRARY}
)

add_test(NAME password_hasher COMMAND test-password-hasher)



