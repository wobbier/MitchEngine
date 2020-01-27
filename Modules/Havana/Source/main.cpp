#include "EditorApp.h"
#include "Engine/Engine.h"
#include <SimpleMath.h>

int main()
{
	EditorApp* app = new EditorApp();
	GetEngine().Init(app);
	DirectX::SimpleMath::Quaternion quat = DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(Mathf::Radians(20), 0, 0);
	YIKES(std::to_string(quat.x) + "," + std::to_string(quat.y) + "," + std::to_string(quat.z));
	Transform trans;
	trans.SetRotation(Vector3(20, 0, 0));
	YIKES(std::to_string(trans.InternalRotation.GetInternalVec().x) + "," + std::to_string(trans.InternalRotation.GetInternalVec().y) + "," + std::to_string(trans.InternalRotation.GetInternalVec().z));
	GetEngine().Run();

	return 0;
}