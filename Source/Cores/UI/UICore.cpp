#include "PCH.h"
#include "UICore.h"
#include "Components/Transform.h"
#include "ECS/ComponentFilter.h"
#include "Logger.h"
#include "Graphics/ShaderCommand.h"
#include "Resource/Resource.h"

#include <iostream>
#include "Resource/ResourceCache.h"
#include "RenderCommands.h"
#include "Engine/Engine.h"
#include "Components/UI/Text.h"
#include "Utils/StringUtils.h"

UICore::UICore()
	: Base(ComponentFilter().Requires<Transform>().Requires<Text>())
{
	IsSerializable = false;
	m_renderer = &GetEngine().GetRenderer();
}

UICore::~UICore()
{
	Logger::GetInstance().Log(Logger::LogType::Debug, "UICore Destroyed...");
}

void UICore::Init()
{
	Logger::GetInstance().Log(Logger::LogType::Debug, "UICore Initialized...");
	m_renderer->ClearUIText();
}

void UICore::OnEntityAdded(Entity& NewEntity)
{
	Moonlight::TextCommand command;
	Text& textComponent = NewEntity.GetComponent<Text>();
	command.SourceText = StringUtils::ToWString(textComponent.SourceText);
	textComponent.RenderId = m_renderer->PushUIText(command);
}

void UICore::OnEntityRemoved(Entity& InEntity)
{
	Text& textComponent = InEntity.GetComponent<Text>();
	m_renderer->PopUIText(textComponent.RenderId);
}

void UICore::Update(float dt)
{
	OPTICK_CATEGORY("UICore::Update", Optick::Category::Rendering)

	auto Renderables = GetEntities();
	for (auto& InEntity : Renderables)
	{
		Transform& transform = InEntity.GetComponent<Transform>();
		Text& textComponent = InEntity.GetComponent<Text>();

		Moonlight::TextCommand command;
		//if (textComponent.HasChanged)
		{
			command.SourceText = StringUtils::ToWString(textComponent.SourceText);
			command.ScreenPosition = Vector2(transform.Position.X(), transform.Position.Y());
			command.Anchor = textComponent.Anchor;
			m_renderer->UpdateText(textComponent.RenderId, command);
		}
	}
}

void UICore::OnStop()
{
	m_renderer->ClearUIText();
}
