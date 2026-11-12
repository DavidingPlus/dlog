#include "logmanager.h"


dlog::LogManager &GetInstanceFromAnotherSourceFile() { return dlog::LogManager::GetInstance(); }

void ConfigureThreadPoolFromAnotherSourceFile(size_t threadCount) { dlog::LogManager::ConfigureThreadPool(threadCount); }
