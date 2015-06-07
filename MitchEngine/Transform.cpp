#include "Transform.h"

using namespace ma;

Transform::Transform() : Position(glm::vec3(0,0,0)), Rotation(0), Scale(glm::vec3(1,1,1)){
}

Transform::~Transform() {
}

void Transform::Update(float dt) {

}

void Transform::Init() {

}

void Transform::SendMessage(class Message* message) {

}
