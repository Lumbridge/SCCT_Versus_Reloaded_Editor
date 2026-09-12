#pragma once
#include <windows.h>
#include <string>
namespace MagicEventWorkbench
{
    constexpr unsigned Command=40927;
    void Open(HWND owner,const std::string& actorPath = {});
}
