#include "Event.h"
#include "Types/AssetType.h"

namespace Moonlight { class Texture; }

class PickingEvent
    : public Event<PickingEvent>
{
public:
    PickingEvent()
        : Event()
    {
    }
    uint32_t RawEntityID = 0;
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