#pragma once
#include "Game.h"
namespace ma {
	class BitBuster :
		public Game {
	public:
		BitBuster();
		virtual ~BitBuster() override;

		virtual void Initialize() override;

		virtual void Update(float DeltaTime) override;

		virtual void End() override;
	};
}