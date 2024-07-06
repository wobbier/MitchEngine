/*
 * Copyright 2011-2021 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx#license-bsd-2-clause
 */

#include "bgfx_shader.sh"
#include "ShaderLib.sh"


uniform vec4 u_time;

float GetDelta()
{
	return u_time.x;
}

float GetTotalTime()
{
	return u_time.y;
}