include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)

FetchContent_MakeAvailable(Catch2)

add_executable(te_tests
    test_jsonc.cpp
)

target_link_libraries(te_tests PRIVATE
    Catch2::Catch2WithMain
    te_sdk
)

if(MINGW)
    target_link_options(te_tests PRIVATE -static -static-libgcc -static-libstdc++)
endif()

include(CTest)
include(Catch)
catch_discover_tests(te_tests)
