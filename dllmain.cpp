#include "SafeDllMain.hpp"

#include <print>

void SafeDllMain()
{
    std::println("SafeDllMain called");

    MessageBoxA(
        nullptr,
        "SafeDllMain called",
        "SafeDllMain",
        MB_OK | MB_ICONINFORMATION);

    int* p = nullptr;
    *p = 0xDEADBEEF;
}