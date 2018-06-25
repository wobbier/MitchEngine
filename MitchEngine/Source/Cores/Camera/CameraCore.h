#pragma once
#include "Engine/Core.h"

class CameraCore : public Core<CameraCore>
{
public:
	CameraCore();
	~CameraCore();

	// Separate init from construction code.
	virtual void Init() final;

	// Each core must update each loop
	virtual void Update(float dt) final;
};
