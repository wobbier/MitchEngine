#include "Event.h"
#include "Path.h"
#include "Pointers.h"
#include <functional>
#include "Types/AssetType.h"
#include <string>
#include <vector>

namespace Moonlight { class Texture; }

class PickingEvent
    : public Event<PickingEvent>
{
public:
    PickingEvent()
        : Event()
    {
    }
    uint64_t RawEntityID = 0;
};

// Files under the watched asset roots changed on disk (fired once per batch, main thread).
class AssetsChangedEvent
    : public Event<AssetsChangedEvent>
{
public:
    AssetsChangedEvent()
        : Event()
    {
    }
    std::vector<std::string> Paths;
};

class PreviewResourceEvent
    : public Event<PreviewResourceEvent>
{
public:
    PreviewResourceEvent()
        : Event()
    {
    }
    SharedPtr<Moonlight::Texture> Subject;
};

class RequestAssetSelectionEvent
    : public Event<RequestAssetSelectionEvent>
{
public:
    RequestAssetSelectionEvent() = delete;
    RequestAssetSelectionEvent( std::function<void( Path )> cb, AssetType forcedFilter = AssetType::Unknown, bool isRequestingSaving = false )
        : Event()
        , Callback( cb )
        , ForcedFilter( forcedFilter )
        , IsRequestingSave( isRequestingSaving )
    {

    }

    std::function<void( Path )> Callback;
    AssetType ForcedFilter;
    bool IsRequestingSave = false;
};

// Opens a shader graph (project path) in the editor's Shader Graph window.
class OpenShaderGraphEvent
    : public Event<OpenShaderGraphEvent>
{
public:
    OpenShaderGraphEvent()
        : Event()
    {
    }
    std::string GraphPath;
};

// Opens a MitchTree recipe (project path to a .mtree) in the editor's tree editor.
class OpenTreeEvent
    : public Event<OpenTreeEvent>
{
public:
    OpenTreeEvent()
        : Event()
    {
    }
    std::string TreePath;
};

// Opens a source terrain asset in Havana's terrain authoring panel.
class OpenTerrainEvent : public Event<OpenTerrainEvent>
{
public:
    std::string AssetPath;
};

class OpenSequenceEvent : public Event<OpenSequenceEvent>
{
public:
    std::string ClipPath;
    uint64_t PlayerGUID = 0;
};
