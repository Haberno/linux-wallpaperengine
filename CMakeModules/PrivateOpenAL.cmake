# Private, dynamically linked OpenAL Soft1.21.1. Host versions contain a different
# HRTF dataset and callback ABI. The archive pin and local patch are distributed.
include(FetchContent)
FetchContent_Declare(lwe_openal
    URL https://codeload.github.com/kcat/openal-soft/tar.gz/refs/tags/1.21.1
    URL_HASH SHA256=8ac17e4e3b32c1af3d5508acfffb838640669b4274606b7892aa796ca9d7467f)
FetchContent_GetProperties(lwe_openal)
if(NOT lwe_openal_POPULATED)
    FetchContent_Populate(lwe_openal)
endif()
set(LWE_OPENAL_SOURCE "${lwe_openal_SOURCE_DIR}")
include("${CMAKE_CURRENT_LIST_DIR}/PatchPrivateOpenAL.cmake")

function(lwe_add_private_openal)
    # The pinned release predates CMake4's removed compatibility floor.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    set(LIBTYPE SHARED)
    set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
    set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ALSOFT_INSTALL OFF CACHE BOOL "" FORCE)
    set(ALSOFT_INSTALL_CONFIG OFF CACHE BOOL "" FORCE)
    set(ALSOFT_INSTALL_HRTF_DATA OFF CACHE BOOL "" FORCE)
    set(ALSOFT_INSTALL_AMBDEC_PRESETS OFF CACHE BOOL "" FORCE)
    set(ALSOFT_UPDATE_BUILD_VERSION OFF CACHE BOOL "" FORCE)
    set(ALSOFT_EMBED_HRTF_DATA ON CACHE BOOL "" FORCE)
    foreach(backend ALSA OSS SOLARIS SNDIO PULSEAUDIO JACK PORTAUDIO SDL2 WAVE SOUNDIO)
        set(ALSOFT_BACKEND_${backend} OFF CACHE BOOL "" FORCE)
    endforeach()
    add_subdirectory("${lwe_openal_SOURCE_DIR}" "${lwe_openal_BINARY_DIR}" EXCLUDE_FROM_ALL)
    target_compile_definitions(OpenAL PRIVATE LWE_PRIVATE_OPENAL)
    target_compile_definitions(common PRIVATE LWE_PRIVATE_OPENAL)
    set_property(TARGET OpenAL APPEND_STRING PROPERTY LINK_FLAGS " -Wl,-Bsymbolic-functions")
    set_target_properties(OpenAL PROPERTIES OUTPUT_NAME lwe-openal
        LIBRARY_OUTPUT_DIRECTORY "${TARGET_OUTPUT_DIRECTORY}")
endfunction()
lwe_add_private_openal()

add_library(lwe-sound-spatial INTERFACE)
target_include_directories(lwe-sound-spatial INTERFACE "${lwe_openal_SOURCE_DIR}/include")
target_link_libraries(lwe-sound-spatial INTERFACE ${CMAKE_DL_LIBS})
add_dependencies(lwe-sound-spatial OpenAL)
install(TARGETS OpenAL LIBRARY DESTINATION .)
# Include the complete corresponding component source alongside its dynamic
# library; downstream binary packages can keep both in their normal split.
install(DIRECTORY "${lwe_openal_SOURCE_DIR}/" DESTINATION licenses/openal-soft/source
    PATTERN ".git" EXCLUDE)
install(FILES "${lwe_openal_SOURCE_DIR}/COPYING"
    DESTINATION licenses/openal-soft RENAME COPYING)
install(FILES "${CMAKE_CURRENT_LIST_DIR}/../licenses/openal-soft/NOTICE"
    "${CMAKE_CURRENT_LIST_DIR}/openal-soft-1.21.1.patch"
    DESTINATION licenses/openal-soft)
