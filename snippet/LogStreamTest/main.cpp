#include "logstream.h"
#include "fixedbuffer.h"

#include <iostream>


int main()
{
    dlog::LogStream stream;
    std::string payload(5000, 'A');

    std::cout << "payload.size(): " << payload.size() << ", kSmallBufferSize: " << dlog::kSmallBufferSize << std::endl;

    stream << "payload=" << payload << ", done\n";

    std::cout << "stream.buffer().toString(): " << stream.buffer().toString();
}
