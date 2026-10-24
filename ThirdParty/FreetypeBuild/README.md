# FreetypeBuild

One shim `.c` per FreeType compilation unit, so Sharpmake can glob this directory instead of
the `freetype` submodule (whose `src/` tree mixes umbrella files with their `#include`d parts —
globbing it directly would compile everything twice). The module set mirrors the stock
`ftmodule.h` (all default drivers + gzip/lzw streams), and `FT2_BUILD_LIBRARY` comes from
`Freetype.sharpmake.cs`. Bump the submodule freely; add a shim only when FreeType adds a module.
