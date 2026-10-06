# Exact official source archive, with the accompanying reviewable patch.
file(SHA256 "${LWE_OPENAL_SOURCE}/common/aloptional.h" current_hash)
if(NOT current_hash STREQUAL "a6f1495bc925f567dfe7b0f6b16093147c33c90e68682223014fea2527a7c5af")
    if(NOT current_hash STREQUAL "16181ee59a9ec9098523010e5707492a7867c4f19653a808731992100ef8e3a3")
        message(FATAL_ERROR "Unexpected OpenAL1.21.1 source: common/aloptional.h")
    endif()
    file(READ "${LWE_OPENAL_SOURCE}/common/aloptional.h" content)
    string(REPLACE [==[const T& operator*() const& { return this->mValue; }]==] [==[const T& operator*() const& { return mStore.mValue; }]==] content "${content}")
    file(WRITE "${LWE_OPENAL_SOURCE}/common/aloptional.h" "${content}")
    file(SHA256 "${LWE_OPENAL_SOURCE}/common/aloptional.h" current_hash)
    if(NOT current_hash STREQUAL "a6f1495bc925f567dfe7b0f6b16093147c33c90e68682223014fea2527a7c5af")
        message(FATAL_ERROR "OpenAL1.21.1 patch failed: common/aloptional.h")
    endif()
endif()
file(SHA256 "${LWE_OPENAL_SOURCE}/common/strutils.cpp" current_hash)
if(NOT current_hash STREQUAL "2d6a0fe00e5246f733f23cc83a800b493bdcaa16cdc76ee9c0b5e5d678847dba")
    if(NOT current_hash STREQUAL "d3d250f357006adcad3a46ba9c044ae35c3f5d3c5bdb0d2629f55c07b5ef60e4")
        message(FATAL_ERROR "Unexpected OpenAL1.21.1 source: common/strutils.cpp")
    endif()
    file(READ "${LWE_OPENAL_SOURCE}/common/strutils.cpp" content)
    string(REPLACE [==[#include <cstdlib>]==] [==[#include <cstdlib>
#include <cstring>]==] content "${content}")
    string(REPLACE [==[al::optional<std::string> getenv(const char *envname)
{]==] [==[al::optional<std::string> getenv(const char *envname)
{
#ifdef LWE_PRIVATE_OPENAL
    // This private loopback component does not consume another application's
    // OpenAL preferences, nor change process-wide environment variables.
    if(std::strncmp(envname, "ALSOFT_", 7) == 0 || std::strncmp(envname, "__ALSOFT_", 9) == 0)
        return al::nullopt;
#endif]==] content "${content}")
    file(WRITE "${LWE_OPENAL_SOURCE}/common/strutils.cpp" "${content}")
    file(SHA256 "${LWE_OPENAL_SOURCE}/common/strutils.cpp" current_hash)
    if(NOT current_hash STREQUAL "2d6a0fe00e5246f733f23cc83a800b493bdcaa16cdc76ee9c0b5e5d678847dba")
        message(FATAL_ERROR "OpenAL1.21.1 patch failed: common/strutils.cpp")
    endif()
endif()
file(SHA256 "${LWE_OPENAL_SOURCE}/alc/alc.cpp" current_hash)
if(NOT current_hash STREQUAL "3ac2f8b80857b910ed593bcf9b179d8be68ee19f03245c59224bbfda3b13ac90")
    if(NOT current_hash STREQUAL "1181497e21bf55de9a824dc61c37db0219d80d0ac889ebdc9b3fdd0f6ec6f7a4")
        message(FATAL_ERROR "Unexpected OpenAL1.21.1 source: alc/alc.cpp")
    endif()
    file(READ "${LWE_OPENAL_SOURCE}/alc/alc.cpp" content)
    string(REPLACE [==[    ReadALConfig();]==] [==[#ifndef LWE_PRIVATE_OPENAL
    ReadALConfig();
#endif]==] content "${content}")
    file(WRITE "${LWE_OPENAL_SOURCE}/alc/alc.cpp" "${content}")
    file(SHA256 "${LWE_OPENAL_SOURCE}/alc/alc.cpp" current_hash)
    if(NOT current_hash STREQUAL "3ac2f8b80857b910ed593bcf9b179d8be68ee19f03245c59224bbfda3b13ac90")
        message(FATAL_ERROR "OpenAL1.21.1 patch failed: alc/alc.cpp")
    endif()
endif()
file(SHA256 "${LWE_OPENAL_SOURCE}/alc/hrtf.cpp" current_hash)
if(NOT current_hash STREQUAL "915bf3b904955f1cf5a0a8832ea1ae6ef48a9fdc382bc0f233526c21ca6e5092")
    if(NOT current_hash STREQUAL "8f3a0e716617083407f2fa258b7ae0e23a63e5d830d98a5c61bdde3bce9ead29")
        message(FATAL_ERROR "Unexpected OpenAL1.21.1 source: alc/hrtf.cpp")
    endif()
    file(READ "${LWE_OPENAL_SOURCE}/alc/hrtf.cpp" content)
    string(REPLACE [==[    bool usedefaults{true};]==] [==[#ifdef LWE_PRIVATE_OPENAL
    if(!GetResource(IDR_DEFAULT_HRTF_MHR).empty())
        AddBuiltInEntry("Built-In HRTF", IDR_DEFAULT_HRTF_MHR);
#else
    bool usedefaults{true};]==] content "${content}")
    string(REPLACE [==[    al::vector<std::string> list;
    list.reserve(EnumeratedHrtfs.size());]==] [==[#endif
    al::vector<std::string> list;
    list.reserve(EnumeratedHrtfs.size());]==] content "${content}")
    file(WRITE "${LWE_OPENAL_SOURCE}/alc/hrtf.cpp" "${content}")
    file(SHA256 "${LWE_OPENAL_SOURCE}/alc/hrtf.cpp" current_hash)
    if(NOT current_hash STREQUAL "915bf3b904955f1cf5a0a8832ea1ae6ef48a9fdc382bc0f233526c21ca6e5092")
        message(FATAL_ERROR "OpenAL1.21.1 patch failed: alc/hrtf.cpp")
    endif()
endif()
