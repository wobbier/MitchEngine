#pragma once
#include "Engine/Core.h"
#include <btBulletDynamicsCommon.h>

#include "LinearMath/btIDebugDraw.h"


class GLDebugDrawer : public btIDebugDraw
{
	int m_debugMode = 0;

	int LineVAO;
public:

	GLDebugDrawer() = default;
	virtual ~GLDebugDrawer() = default;

	virtual void	drawLine(const btVector3& from, const btVector3& to, const btVector3& fromColor, const btVector3& toColor);

	virtual void	drawLine(const btVector3& from, const btVector3& to, const btVector3& color);

	virtual void	drawSphere(const btVector3& p, btScalar radius, const btVector3& color);

	virtual void	drawTriangle(const btVector3& a, const btVector3& b, const btVector3& c, const btVector3& color, btScalar alpha);

	virtual void	drawContactPoint(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector3& color);

	virtual void	reportErrorWarning(const char* warningString);

	virtual void	draw3dText(const btVector3& location, const char* textString);

	virtual void	setDebugMode(int debugMode);

	virtual int		getDebugMode() const { return m_debugMode; }

};

class PhysicsCore : public Core<PhysicsCore>
{
	friend class Core<PhysicsCore>;
public:
	btDiscreteDynamicsWorld* PhysicsWorld;
	btVector3 Gravity;

	PhysicsCore();
	~PhysicsCore();

	// Separate init from construction code.
	virtual void Init() final;

	// Each core must update each loop
	virtual void Update(float dt) final;

	virtual void OnEntityAdded(Entity& NewEntity) final;
};
