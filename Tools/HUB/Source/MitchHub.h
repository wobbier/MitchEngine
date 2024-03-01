#pragma once
#include <Pointers.h>
#include "ProjectCache.h"
#include "Tool.h"

namespace Moonlight {
    class Texture;
}

class MitchHub
    : public Tool
{
public:
    MitchHub( ToolCreationFlags& inToolCreationFlags );

    void OnUpdate() final;

    void OnStart() override;

private:
    SharedPtr<Moonlight::Texture> logo;
    SharedPtr<Moonlight::Texture> closeIcon;
    SharedPtr<Moonlight::Texture> minimizeIcon;
    SharedPtr<Moonlight::Texture> vsIcon;
    SharedPtr<Moonlight::Texture> genIcon;

    std::size_t SelectedProjectIndex = 0;
    ProjectCache Cache;
    const float SystemButtonSize = 30.f;
};
