#include "logstream.h"

#include <iostream>
#include <cassert>
#include <string_view>


int main()
{
    dlog::LogStream stream;
    std::string payload(5000, 'A');

    std::cout << "payload.size(): " << payload.size() << ", initial buffer size: " << dlog::Buffer::kInitialSize << std::endl;

    stream << "payload=" << payload << ", done\n";

    const dlog::Buffer &buffer = stream.buffer();
    assert(std::string_view(buffer.readableBegin(), buffer.readableBytes()) == "payload=" + payload + ", done\n");
    std::cout << "stream readable bytes: " << buffer.readableBytes() << '\n';
    std::cout.write(buffer.readableBegin(), buffer.readableBytes());
}
