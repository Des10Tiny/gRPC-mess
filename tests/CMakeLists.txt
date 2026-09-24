include(FetchContent)

fetchcontent_declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest
    GIT_TAG v2.5.2
)
fetchcontent_makeavailable(doctest)

set(TEST_SOURCES
    doctest_main.cpp
    test_client.cpp
    test_server.cpp
)

include("${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")

if(BUILD_TESTS)
    add_executable(messengerTests ${TEST_SOURCES})
    target_compile_definitions(messengerTests PRIVATE UNIT_TEST)
    target_include_directories(messengerTests PRIVATE ${CMAKE_SOURCE_DIR}/client ${CMAKE_SOURCE_DIR}/server)

    target_compile_definitions(messengerTests PRIVATE
        SERVER_EXEC="$<TARGET_FILE:messenger_server>"
        CLIENT_EXEC="$<TARGET_FILE:messenger_client>")

    target_link_libraries(messengerTests PRIVATE
        messenger_proto
        Threads::Threads
        nlohmann_json::nlohmann_json
        doctest::doctest
    )

    doctest_discover_tests(messengerTests TEST_SUFFIX "_Base")
endif()

if(BUILD_TESTS_SANITIZED)
    add_executable(messengerTestsSanitize ${TEST_SOURCES})
    target_compile_definitions(messengerTestsSanitize PRIVATE UNIT_TEST)
    target_include_directories(messengerTestsSanitize PRIVATE ${CMAKE_SOURCE_DIR}/client ${CMAKE_SOURCE_DIR}/server)

    target_compile_options(messengerTestsSanitize PRIVATE -fsanitize=address,undefined -g -fno-omit-frame-pointer)
    target_link_options(messengerTestsSanitize PRIVATE -fsanitize=address,undefined)

    target_compile_definitions(messengerTestsSanitize PRIVATE
        SERVER_EXEC="$<TARGET_FILE:messenger_server>"
        CLIENT_EXEC="$<TARGET_FILE:messenger_client>"
    )

    target_link_libraries(messengerTestsSanitize PRIVATE
        messenger_proto
        Threads::Threads
        nlohmann_json::nlohmann_json
        doctest::doctest
    )

    doctest_discover_tests(messengerTestsSanitize TEST_SUFFIX "_BaseWithSanitizer")
endif()
