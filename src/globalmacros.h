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


// Windows DLL 的导出/导入宏：
// 1. D_BUILD_SHARED 由目标及其使用方共同定义，表示当前使用的是 DLL；
// 2. D_DLL_EXPORT 只在编译 DLL 本身时定义，此时导出符号；
// 3. 使用方不会定义 D_DLL_EXPORT，因此导入 DLL 符号；
// 4. 静态库构建和非 Windows 平台不需要 DLL 修饰。
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
