#include "EditorApp.h"
#include "Engine/Engine.h"
#include <SimpleMath.h>

int main()
{
	EditorApp* app = new EditorApp();
	GetEngine().Init(app);

	Transform childTransform;
	Transform parentTransform;
	parentTransform.SetPosition(Vector3(1, 0, 0));
	childTransform.SetParent(parentTransform);

	childTransform.SetPosition(Vector3(0, 10, -10));

	parentTransform.UpdateWorldTransform();

	childTransform.GetLocalToWorldMatrix();

	Matrix4 parent;
	if(true)
	{

		DirectX::SimpleMath::Matrix id = DirectX::XMMatrixIdentity();
		DirectX::SimpleMath::Matrix rot = DirectX::SimpleMath::Matrix::CreateFromYawPitchRoll(0, 0, 0);// , Child->Rotation.Y(), Child->Rotation.Z());
		DirectX::SimpleMath::Matrix scale = DirectX::SimpleMath::Matrix::CreateScale(Vector3(1.f).GetInternalVec());
		DirectX::SimpleMath::Matrix pos = XMMatrixTranslationFromVector(Vector3(1, 0, 0).GetInternalVec());
		parent = Matrix4((scale * rot * pos));
	}

	Matrix4 child;
	Matrix4 childLocal;
	{

		DirectX::SimpleMath::Matrix id = DirectX::XMMatrixIdentity();
		DirectX::SimpleMath::Matrix rot = DirectX::SimpleMath::Matrix::CreateFromYawPitchRoll(0, 0, 0);// , Child->Rotation.Y(), Child->Rotation.Z());
		DirectX::SimpleMath::Matrix scale = DirectX::SimpleMath::Matrix::CreateScale(Vector3(1.f).GetInternalVec());
		DirectX::SimpleMath::Matrix pos = XMMatrixTranslationFromVector(Vector3(0, 10, -10).GetInternalVec());
		childLocal = Matrix4((scale * rot * pos));
		child = Matrix4(childLocal.GetInternalMatrix() * parent.GetInternalMatrix());
	}


	DirectX::SimpleMath::Matrix intMat2;
	child.GetInternalMatrix().Invert(intMat2);
	Matrix4 mat2 = Matrix4(intMat2);
	//YIKES("Child World Matrix: \n" + child.ToString());
	//YIKES("Child Local Matrix: \n" + childLocal.ToString());
	YIKES("(Transform) Child GetLocalToWorldMatrix: \n" + childTransform.GetLocalToWorldMatrix().ToString());
	YIKES("(Transform) Child GetWorldToWorldLocal: \n" + childTransform.GetWorldToLocalMatrix().ToString());
	//YIKES(mat2.ToString());

	GetEngine().Run();

	return 0;
}