# Compiler / linker flags shared by every target.
# Release builds optimise for SIZE, not speed: the whole point of this rewrite
# is a small self-contained exe and deb.

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /EHsc /MP /utf-8)
    add_compile_options($<$<CONFIG:Release>:/O1>)
    add_link_options($<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_compile_options(-ffunction-sections -fdata-sections)
    add_compile_options($<$<CONFIG:Release>:-Os>)
    add_link_options(-Wl,--gc-sections -static-libstdc++ -static-libgcc)
endif()

# LTO removes template instantiations that header-only JSON/Beast code would
# otherwise leave behind. Disabled in Debug to keep iteration fast.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
