macro(setup_project)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/")

    if(MSVC)
        add_definitions(-D_CRT_SECURE_NO_WARNINGS)
        add_compile_options(/nologo /wd4275 /wd4146 /wd4251 /wd4005 /wd4305 /wd4100)
        set(CMAKE_CXX_FLAGS_DEBUG "/Zi /external:W0")
        set(CMAKE_SHARED_LINKER_FLAGS "/SAFESEH:NO")
        set(CMAKE_EXE_LINKER_FLAGS "/SAFESEH:NO")

        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DFW_DEBUG")
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    endif()

    add_subdirectory(vendors)
    add_subdirectory(code)

    if(UNIX OR APPLE)
        set(DISABLED_WARNS "-Wno-pedantic -Wno-unused-parameter -Wno-unused-result"
            "-Wno-deprecated-declarations" "-Wno-invalid-offsetof")
        add_compile_options(${DISABLED_WARNS})
        set(CMAKE_CXX_FLAGS "-std=c++20 ${CMAKE_CXX_FLAGS}")
    endif()
endmacro()
