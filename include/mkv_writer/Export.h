#pragma once

#if defined(_WIN32) && defined(MKV_WRITER_SHARED)
#    if defined(MKV_WRITER_BUILDING_LIBRARY)
#        define MKV_WRITER_API __declspec(dllexport)
#    else
#        define MKV_WRITER_API __declspec(dllimport)
#    endif
#else
#    define MKV_WRITER_API
#endif
