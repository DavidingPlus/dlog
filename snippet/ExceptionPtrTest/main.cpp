#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>


int main()
{
    std::exception_ptr backgroundError;
    std::thread worker([&backgroundError]
                       {
                           try
                           {
                               throw std::runtime_error("background output failed");
                           }
                           catch (...)
                           {
                               backgroundError = std::current_exception();
                           } //
                       });

    // join 后再读取后台保存的异常。
    worker.join();
    if (!backgroundError)
    {
        std::cerr << "Expected a background exception\n";
        return EXIT_FAILURE;
    }

    try
    {
        std::rethrow_exception(backgroundError);
    }
    catch (const std::runtime_error &error)
    {
        std::cout << "Caught on the main thread: " << error.what() << '\n';
        return "background output failed" == std::string_view(error.what()) ? 0 : 1;
    }
}
