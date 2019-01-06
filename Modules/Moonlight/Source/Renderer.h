#pragma once
#include "Device/IDevice.h"

class Renderer
{
	Renderer() = default;
	~Renderer() = default;

private:
	IDevice* m_device;
};