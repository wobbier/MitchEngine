#pragma once
#include "Game.h"

class MitchGame :
	public Game
{
public:
	MitchGame();
	virtual ~MitchGame() override;

	virtual void Initialize() override;

	virtual void Update(float DeltaTime) override;

	virtual void End() override;
};
