#include "logmanager.h"

#ifdef DLOG_DLL_EXIT_PROBE


#include <fstream>
#include <future>
#include <string>


namespace
{
    std::future<void> task;
}


extern "C" __declspec(dllexport) void ProbeSubmit(const char *prefix)
{
    auto &manager = dlog::LogManager::GetInstance();
    const std::string base(prefix);
    auto logger = dlog::Logger::BuildLogger<dlog::FileFlush>(base + ".log");
    manager.registerLogger("dll-exit", logger);
    logger->message("dll.cpp", 1, dlog::LogLevel::INFO).stream() << "dll log at exit";
    task = manager.getThreadPool()->enqueue([base]
                                            {
        std::ofstream file(base + ".task", std::ios::binary);
        file << "dll task at exit"; });
}

extern "C" __declspec(dllexport) void ProbeFlush()
{
    task.get();
    dlog::LogManager::GetInstance().getLogger("dll-exit")->stop();
}


#else


dlog::LogManager &GetInstanceFromAnotherSourceFile() { return dlog::LogManager::GetInstance(); }

void ConfigureThreadPoolFromAnotherSourceFile(size_t threadCount) { dlog::LogManager::ConfigureThreadPool(threadCount); }


#endif
