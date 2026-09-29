#pragma once
#include <Windows.h>
#include <chrono>
#include "GameStructs.h"
#include <cstdint>
#include <string>
#include <vector>

class Rendering
{
public:
	static void Initialize();
	// Copies the top-left width x height of the device back buffer (a viewport
	// drawn with Blit=0, before Present) as 32-bit BGRA rows, top row first.
	static bool ReadBackBuffer(int width, int height, std::vector<uint32_t>& pixels, std::string& error);
};

#define D3DX_PI    (3.14159265358979323846)
const double degToRadConversionFactor = D3DX_PI / 180.0;
const double radToDegConversionFactor = 180.0 / D3DX_PI;
