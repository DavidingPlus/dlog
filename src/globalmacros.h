#ifndef _DLOG_GLOBALMACROS_H_
#define _DLOG_GLOBALMACROS_H_


#ifdef _WIN32
#define OS_WIN32
#elif __unix__
#define OS_LINUX
#else
#define OS_UNKNOWN
#endif


#define CLASS_NONCOPYABLE(ClassName)                       \
                                                           \
private:                                                   \
                                                           \
    ClassName(const ClassName &other) = delete;            \
    ClassName(ClassName &&other) = delete;                 \
    ClassName &operator=(const ClassName &other) = delete; \
    ClassName &operator=(ClassName &&other) = delete;


#endif
