#pragma once
#include "Engine/Core.h"

class FlyingCameraCore : public Core<FlyingCameraCore>
{
public:
	FlyingCameraCore();
	~FlyingCameraCore();

	// Separate init from construction code.
	virtual void Init() final;

	// Each core must update each loop
	virtual void Update(float dt) final;

private:
	int LastX = 0;
	int LastY = 0;
	bool FirstUpdate = true;
};
