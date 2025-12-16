// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file TestMaterial.ispc

// this should be <rendering/shading/Shading.isph>
// for anything other than the unittest
#include <moonray/rendering/shading/ispc/Shading.isph>
#include <moonray/rendering/shading/ispc/BsdfComponent.isph>
#include "attributes.isph"

static void
shade(const uniform Material *      uniform  me,
            uniform ShadingTLState *uniform  tls,
      const varying State                    &state,
            varying BsdfBuilder              &bsdfBuilder)
{
    // evaluate color
    const varying Color albedo = evalAttrColor(me, tls, state);
    LambertianBRDF lambertian;
    LambertianBRDF_init(lambertian, state.mN, albedo);
    BsdfBuilder_addLambertianBRDF(bsdfBuilder,
                                  lambertian,
                                  1.0f, // weight
                                  BSDFBUILDER_PHYSICAL,
                                  0); // label
}

DEFINE_MATERIAL_SHADER(TestMaterial, shade)

