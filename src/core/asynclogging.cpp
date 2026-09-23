#include "asynclogging.h"


AsyncLogging::~AsyncLogging()
{
    if (m_running) stop();
}

void AsyncLogging::append(const char *data, size_t length)
{
}

void AsyncLogging::start()
{
    m_running = true;
    m_thread.start();
}

void AsyncLogging::stop()
{
    m_running = false;
    m_cond.notify_one();
}

void AsyncLogging::threadFunc()
{
}
