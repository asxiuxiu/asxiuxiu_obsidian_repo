
add_executable(day04_group_anagrams
    solution.cpp
    main.cpp
)

target_include_directories(day04_group_anagrams PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_test(NAME day04_group_anagrams COMMAND day04_group_anagrams)
