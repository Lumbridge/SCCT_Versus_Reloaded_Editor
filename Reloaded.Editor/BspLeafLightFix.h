#pragma once
#include <string>

class BspLeafLightFix
{
public:
    static void Initialize();
    static bool Validate(void* model, std::string& error);
};
