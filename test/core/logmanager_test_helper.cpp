#include "logmanager.h"


dlog::LogManager &GetInstanceFromAnotherSourceFile() { return dlog::LogManager::GetInstance(); }
