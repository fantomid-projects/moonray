// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file EvalShader.ispc

#include "EvalShader.isph"
#include "BsdfBuilder.isph"

#include <moonray/rendering/shading/ispc/State.isph>

void
shadeMaterial(const uniform Material * uniform material,
                    uniform ShadingTLState * uniform tls,
              const uniform int nstate,
              const varying State * const uniform state,
                    varying BsdfBuilder * uniform bsdfBuilder)
{
    BsdfBuilder_shadeMaterial(material, tls, nstate, state, bsdfBuilder);
}

