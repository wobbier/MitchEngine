#pragma once
#include "Dementia.h"

#if USING( ME_SCRIPTING )

extern "C" {
    typedef struct _MonoClass MonoClass;
    typedef struct _MonoDomain MonoDomain;
    typedef struct _MonoImage MonoImage;
    typedef struct _MonoObject MonoObject;
    typedef struct _MonoType MonoType;
    typedef struct _MonoMethod MonoMethod;
    typedef struct _MonoClassField MonoClassField;
    typedef struct _MonoProperty MonoProperty;
    typedef struct _MonoAssembly MonoAssembly;
    typedef union _MonoError MonoError;
    typedef struct _MonoString MonoString;
}

#endif