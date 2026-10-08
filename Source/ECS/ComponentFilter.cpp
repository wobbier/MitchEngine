#include "PCH.h"
#include "ComponentFilter.h"

bool ComponentFilter::PassFilter( const ComponentTypeArray& InComponentTypeArray ) const
{
    // Every Requires<> component must be present.
    if( ( InComponentTypeArray & RequiredComponentsList ) != RequiredComponentsList )
    {
        return false;
    }

    // At least one of the RequiresOneOf<> components (when any were specified).
    if( RequiresOneOfComponentsList.any() && !( InComponentTypeArray & RequiresOneOfComponentsList ).any() )
    {
        return false;
    }

    // None of the Excludes<> components.
    if( ( ExcludeComponentsList & InComponentTypeArray ).any() )
    {
        return false;
    }

    return true;
}

bool ComponentFilter::IsEmpty() const
{
    return RequiredComponentsList.none() && RequiresOneOfComponentsList.none();
}

void ComponentFilter::Clear()
{
    RequiredComponentsList.reset();
    RequiresOneOfComponentsList.reset();
    ExcludeComponentsList.reset();
}