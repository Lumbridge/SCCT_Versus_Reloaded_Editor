#pragma once

// UObject::LoadPackage ends the load it began when a package has no linker, so a
// map whose own package (MyLevel) holds a sound no longer crashes the editor when it
// is opened again. See PackageLoadFixModel.h.
namespace PackageLoadFix
{
    void Initialize();
}
