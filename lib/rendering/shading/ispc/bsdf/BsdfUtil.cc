// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file BsdfUtil.cc

#include <scene_rdl2/scene/rdl2/LightSet.h>
#include <scene_rdl2/scene/rdl2/Material.h>

#include <iostream>

namespace moonray {
namespace shading {
namespace internal {

extern "C"
void CPP_printBsdfHeader(const scene_rdl2::rdl2::Material * const material,
                         const scene_rdl2::rdl2::Bsdfv * const bsdfv)
{
    std::cout << "\n";
    std::cout << "==========================================================\n";
    std::cout << "BSDF @ " << bsdfv << " for "
        << material->getSceneClass().getName() << ": '"
        << material->getName() << "'\n";
    std::cout << "==========================================================\n";
    std::cout << "\n";
}

extern "C"
void CPP_printLightSetInfo(const scene_rdl2::rdl2::LightSet * const lightSet)
{
    std::cout << "<LightSet> : '" << lightSet->getName() << "'\n";
}

} // namespace internal
} // namespace shading
} // namespace moonray


