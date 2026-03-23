add_executable(day01_two_sum
    solution.cpp
    main.cpp
)

target_include_directories(day01_two_sum PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_test(NAME day01_two_sum COMMAND day01_two_sum)
