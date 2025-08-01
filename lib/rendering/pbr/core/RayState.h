// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

//
//
#pragma once
#include <moonray/rendering/pbr/Types.h>
#include "RayState.hh"

#include <moonray/rendering/shading/Intersection.h>
#include <moonray/rendering/mcrt_common/Ray.h>
#include <moonray/rendering/pbr/core/PbrTLState.h>
#include <moonray/rendering/shading/Types.h>

namespace moonray {
namespace pbr {

//
// TODO: It's worth noting that the current version of RayState is a dumping
//       ground for all data we need to get vectorization phase 2 up and running.
//       For the initial implementation, we're not paying undue attention to the
//       size of the RayState structure. As a second part of the phase 2
//       implementation, we'll need to shrink down its size and pay attention
//       to which data goes on which cachelines.
//

// Subpixel and PathVertex were moved from PathIntegrator to here.

// Node structure for recording each bounce of the ray tree for a given Subpixel. Deferred rendering passes
// will then consume the data they record and use them to do radiance computations.
struct DeferredNode
{
    scene_rdl2::math::Color     mThroughput;
    int                         mNonMirrorDepth;
    const shading::BsdfLobe    *mParentLobe;
    const shading::BsdfLobe   **mLobePtrs;
    int                         mLobeCount;
    bool                        mBsdfIsSpherical;
    shading::BsdfSlice          mSlice;
    float                       mRayEpsilon;
    unsigned int                mSequenceID;
    float                       mRayDirFootprint;
    scene_rdl2::math::Color     mAlbedo;
    shading::Intersection       mIntersection;
    DeferredNode               *mNext;
};

// Identifies where the primary ray comes from
struct Subpixel {
    SUBPIXEL_MEMBERS;

    // HVD validation.
    static uint32_t hvdValidation(bool verbose) { SUBPIXEL_VALIDATION(VLEN); }
};

// Keep track of state along the path recursion, specifically at the ray
// origin of the current ray being processed.
struct PathVertex {
    PATH_VERTEX_MEMBERS;

    // HVD validation.
    static uint32_t hvdValidation(bool verbose) { PATH_VERTEX_VALIDATION(VLEN); }
};

// This class keeps track of the node lightsets encountered along a path.
// Node lightsets also apply to indirect illumination, so any light paths
// that collect indirect illumination must remember all bsdf node lightsets
// encountered along the way and apply them.
struct Rdl2LightSetList {
    RDL2_LIGHTSET_LIST_MEMBERS;

    Rdl2LightSetList() : mNumLightSets(0) {}

    bool allLightSetsContain(const scene_rdl2::rdl2::Light *rdlLight) const {
        // If there are no lightsets in the Rdl2LightSetList, that is the same
        // as having a lightset that contains all lights.
        // Else, all the lightsets in the list must contain the rdlLight.
        for (int i = 0; i < mNumLightSets; i++) {
            intptr_t lightSetPtr = (((intptr_t) mLightSetsPtrHi[i]) << 32) | (intptr_t) mLightSetsPtrLo[i];
            const scene_rdl2::rdl2::LightSet* lightSet =
                reinterpret_cast<const scene_rdl2::rdl2::LightSet*>(lightSetPtr);
            if (!lightSet->contains(rdlLight)) {
                return false;
            }
        }
        return true;
    }
    void append(const scene_rdl2::rdl2::LightSet* lightSet) {
        // Note that only non-null lightsets should be appended.
        // We could check for duplicates as a potential optimization. That would
        // add more processing here and save processing checking the lightsets later.
        // It is not clear if this would be more efficient overall.
        if (mNumLightSets < MAX_LIGHTSET_LIST_LIGHTSETS) {
            intptr_t lightSetsPtr = reinterpret_cast<intptr_t>(lightSet);
            mLightSetsPtrLo[mNumLightSets] = (uint32_t)(lightSetsPtr);
            mLightSetsPtrHi[mNumLightSets] = (uint32_t)(lightSetsPtr >> 32);
            mNumLightSets++;
        } // else just throw it away, it probably doesn't make much difference
          // after mMaxLightSets bounces
    }

    // HVD validation.
    static uint32_t hvdValidation(bool verbose) { RDL2_LIGHTSET_LIST_VALIDATION(VLEN); }
};

// Structure which encapsulates the state of a ray as it flows through
// the pipeline.
// TODO: shrink the information required to queue a ray.
struct CACHE_ALIGN RayState
{
    RAY_STATE_MEMBERS;

    // HVD validation.
    static uint32_t hvdValidation(bool verbose) { RAY_STATE_VALIDATION(VLEN); }
};

struct CACHE_ALIGN RayStatev
{
    uint8_t mPlaceholder[sizeof(RayState) * VLEN];
};

MNRY_STATIC_ASSERT(sizeof(RayState) * VLEN == sizeof(RayStatev));

inline bool
isRayStateValid(const pbr::TLState* tls, const RayState *rs)
{
    auto checkMain = [](unsigned rayStatePoolSize,
                        const RayState* baseRayState,
                        const RayState* rs) {
        return (rs && baseRayState && (baseRayState <= rs && rs < baseRayState + rayStatePoolSize));
    };

    MNRY_ASSERT(checkMain(tls->mRayStatePool.getActualPoolSize(), tls->getBaseRayState(), rs));
    MNRY_ASSERT(rs->mRay.isValid());
    // TODO: add various validation criteria here...
    return true;
}

// Verify data is valid.
bool isRayStateListValid(const pbr::TLState* tls, const unsigned numEntries, RayState** entries);

inline RayState **
decodeRayStatesInPlace(const pbr::TLState* pbrTls, const unsigned numEntries, shading::SortedRayState* srcDst)
{
    RayState* baseRayState = pbrTls->getBaseRayState();
    RayState** dst = reinterpret_cast<RayState**>(srcDst);

    for (unsigned i = 0; i < numEntries; ++i) {
        dst[i] = baseRayState + srcDst[i].mRsIdx;
    }

    return dst;
}

} // namespace pbr
} // namespace moonray

