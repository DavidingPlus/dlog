#ifndef _DLOG_GLOBALMACROS_H_
#define _DLOG_GLOBALMACROS_H_


#ifdef _WIN32
#define D_OS_WIN32
#elif __unix__
#define D_OS_LINUX
#else
#define D_OS_UNKNOWN
#endif


#define D_CLASS_NONCOPYABLE(ClassName)                     \
                                                           \
private:                                                   \
                                                           \
    ClassName(const ClassName &other) = delete;            \
    ClassName(ClassName &&other) = delete;                 \
    ClassName &operator=(const ClassName &other) = delete; \
    ClassName &operator=(ClassName &&other) = delete;


#if (defined D_OS_WIN32) && (defined D_BUILD_SHARED)
#ifdef D_DLL_EXPORT
#define D_API_EXPORTED __declspec(dllexport)
#else
#define D_API_EXPORTED __declspec(dllimport)
#endif
#else
#define D_API_EXPORTED
#endif


#endif
