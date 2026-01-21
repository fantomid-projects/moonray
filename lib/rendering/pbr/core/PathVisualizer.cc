// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "PathVisualizer.h"

#include <moonray/common/time/Timer.h>
#include <moonray/rendering/drawing/drawing.h>

#include <moonray/rendering/mcrt_common/Frustum.h>
#include <moonray/rendering/mcrt_common/Ray.h>

#include <moonray/rendering/pbr/camera/Camera.h>
#include <moonray/rendering/pbr/camera/PerspectiveCamera.h>
#include <moonray/rendering/pbr/core/Scene.h>

#include <moonray/rendering/rndr/RenderContext.h>
#include <moonray/rendering/rndr/Util.h>

#include <moonray/rendering/rt/EmbreeAccelerator.h>

#include <scene_rdl2/common/grid_util/VectorPacket.h>
#include <scene_rdl2/common/rec_time/RecTime.h>
#include <scene_rdl2/render/cache/ValueContainerEnqueue.h>
#include <scene_rdl2/render/util/StrUtil.h>

using RenderTimer = moonray::time::RAIITimerAverageDouble;
using scene_rdl2::math::Vec3f;
using scene_rdl2::math::Vec2f;
using scene_rdl2::math::Vec2i;

namespace moonray {
namespace pbr {

//----------------------------------------------------------------------------------------------------------------------

std::ostream& operator<<(std::ostream& os, const State& state) {
    switch(state) {
        case State::NONE:             return os << "NONE";
        case State::READY:            return os << "READY";
        case State::START_RECORD:     return os << "START_RECORD";
        case State::RECORD:           return os << "RECORD";
        case State::STOP_RECORD:      return os << "STOP_RECORD";
        case State::REQUEST_DRAW:     return os << "REQUEST_DRAW";
        case State::GENERATE_LINES:   return os << "GENERATE_LINES";
        case State::DRAW:             return os << "DRAW";
        default:                      return os << "UNKNOWN FLAG";
    }
}

std::ostream& operator<<(std::ostream& os, const PathVisualizer::Flags& flag) {
    switch(flag) {
        case PathVisualizer::Flags::NONE:            return os << "NONE";
        case PathVisualizer::Flags::CAMERA:          return os << "CAMERA";
        case PathVisualizer::Flags::INACTIVE:        return os << "INACTIVE";
        case PathVisualizer::Flags::DIFFUSE:         return os << "DIFFUSE";
        case PathVisualizer::Flags::SPECULAR:        return os << "SPECULAR";
        case PathVisualizer::Flags::BSDF_SAMPLE:     return os << "BSDF_SAMPLE";
        case PathVisualizer::Flags::LIGHT_SAMPLE:    return os << "LIGHT_SAMPLE";
        default:                                     return os << "UNKNOWN FLAG";
    }
}

std::ostream& operator<<(std::ostream& os, const PathVisualizer::LineSegment& line) {
    os << "\nLineSegment {\n";
    os << "  mPx1: (" << line.mPx1.x << ", " << line.mPx1.y << "),\n";
    os << "  mPx2: (" << line.mPx2.x << ", " << line.mPx2.y << "),\n";
    os << "  mFlags: " << line.mFlags << ",\n";
    os << "  mDrawEndpoint: " << line.mDrawEndpoint << ",\n";
    os << "  mAlpha: " << line.mAlpha << ",\n";
    os << "}\n";
    return os;
}

//----------------------------------------------------------------------------------------------------------------------

std::string
PathVisualizerParams::show() const
{
    using scene_rdl2::str_util::boolStr;

    std::ostringstream ostr;
    ostr << "PathVisualizerParams {\n"
         << "  mPixelX:" << mPixelX << '\n'
         << "  mPixelY:" << mPixelY << '\n'
         << "  mMaxDepth:" << mMaxDepth << '\n'
         << "  mPixelSamples:" << mPixelSamples << '\n'
         << "  mLightSamples:" << mLightSamples << '\n'
         << "  mBsdfSamples:" << mBsdfSamples << '\n'
         << "  mUseSceneSamples:" << boolStr(mUseSceneSamples) << '\n'
         << "  mOcclusionRaysOn:" << boolStr(mOcclusionRaysOn) << '\n'
         << "  mSpecularRaysOn:" << boolStr(mSpecularRaysOn) << '\n'
         << "  mDiffuseRaysOn:" << boolStr(mDiffuseRaysOn) << '\n'
         << "  mBsdfSamplesOn:" << boolStr(mBsdfSamplesOn) << '\n'
         << "  mLightSamplesOn:" << boolStr(mLightSamplesOn) << '\n'
         << "  mCameraRayColor:" << mCameraRayColor << '\n'
         << "  mSpecularRayColor:" << mSpecularRayColor << '\n'
         << "  mDiffuseRayColor:" << mDiffuseRayColor << '\n'
         << "  mBsdfSampleColor:" << mBsdfSampleColor << '\n'
         << "  mLightSampleColor:" << mLightSampleColor << '\n'
         << "  mLineWidth:" << mLineWidth << '\n'
         << "  mHiddenLineOpacity:" << mHiddenLineOpacity << '\n'
         << "  mPixelXmin:" << mPixelXmin << '\n'
         << "  mPixelYmin:" << mPixelYmin << '\n'
         << "  mPixelXmax:" << mPixelXmax << '\n'
         << "  mPixelYmax:" << mPixelYmax << '\n'
         << "}";
    return ostr.str();
}

std::string
PathVisualizerParams::showPixel() const
{
    using scene_rdl2::str_util::boolStr;
    
    std::ostringstream ostr;
    ostr << "pixel {\n"
         << "  mOn:" << boolStr(mOn) << '\n'
         << "  mPixelX:" << mPixelX << '\n'
         << "  mPixelY:" << mPixelY << '\n'
         << "  mPixelXmin:" << mPixelXmin << '\n'
         << "  mPixelYmin:" << mPixelYmin << '\n'
         << "  mPixelXmax:" << mPixelXmax << '\n'
         << "  mPixelYmax:" << mPixelYmax << '\n'
         << "}";
    return ostr.str();
}

void
PathVisualizerParams::parserConfigure()
{
    auto setSingleUInt = [](Arg& arg, const bool active, unsigned& dest, const std::string& msg) {
        if (!active) {
            arg++;
            return arg.msg("skip\n");
        }
        std::ostringstream ostr;
        dest = (arg++).as<unsigned>(0);
        ostr << msg << dest;
        return arg.msg(ostr.str() + '\n');
    };
    auto setDoubleUInt = [](Arg& arg, const bool active, unsigned& destA, unsigned& destB, const std::string& msg) {
        if (!active) {
            arg += 2;
            return arg.msg("skip\n");
        }
        std::ostringstream ostr;
        destA = (arg++).as<unsigned>(0);
        destB = (arg++).as<unsigned>(0);
        ostr << msg << destA << ',' << destB;
        return arg.msg(ostr.str() + '\n');
    };
    auto setSingleFloat = [](Arg& arg, const bool active, float& dest, const std::string& msg) {
        if (!active) {
            arg++;
            return arg.msg("skip\n");
        }
        std::ostringstream ostr;
        dest = (arg++).as<float>(0);
        ostr << msg << dest;
        return arg.msg(ostr.str() + '\n');
    };
    auto setSingleBool = [](Arg& arg, const bool active, bool& dest, const std::string& msg) {
        if (!active) {
            arg++;
            return arg.msg("skip\n");
        }
        std::ostringstream ostr;
        dest = (arg++).as<bool>(0);
        ostr << msg << scene_rdl2::str_util::boolStr(dest);
        return arg.msg(ostr.str() + '\n');
    };
    auto setColorArg = [](Arg& arg, const bool active, scene_rdl2::math::Color& destCol, const std::string& msg) {
        if (!active) {
            arg += 3;
            return arg.msg("skip\n");
        }
        destCol.r = (arg++).as<float>(0);
        destCol.g = (arg++).as<float>(0);
        destCol.b = (arg++).as<float>(0);
        std::ostringstream ostr;
        ostr << msg << destCol;
        return arg.msg(ostr.str() + '\n');
    };
    auto setDeltaPixArg = [](Arg& arg, const bool active, unsigned& pix,
                             const int min, const int max, const std::string& msg) {
        if (!active) {
            arg++;
            return arg.msg("skip\n");
        }
        const int delta = (arg++).as<int>(0);
        pix = static_cast<unsigned>(std::clamp(static_cast<int>(pix) + delta, std::min(min, 0), max));
        std::ostringstream ostr;
        ostr << msg << pix;
        return arg.msg(ostr.str() + '\n');
    };
    auto setDeltaSampleArg = [](Arg& arg, const bool active, unsigned& sample, const std::string& msg) {
        if (!active) {
            arg++;
            return arg.msg("skip\n");
        }
        const int delta = (arg++).as<int>(0);
        sample = static_cast<unsigned>(std::max(static_cast<int>(sample) + delta, 1));
        std::ostringstream ostr;
        ostr << msg << sample;
        return arg.msg(ostr.str() + '\n');
    };
    auto setToggleArg = [](Arg& arg, const bool active, bool& flag, const std::string& msg) {
        if (!active) {
            arg++;
            return arg.msg("skip\n");
        }
        flag = !flag;
        std::ostringstream ostr;
        ostr << msg << scene_rdl2::str_util::boolStr(flag);
        return arg.msg(ostr.str() + '\n');
    };
        
    mParser.description("PathVisualizerParams command");

    mParser.opt("pixel", "<x> <y>", "set pixel X Y",
                [&](Arg& arg) { return setDoubleUInt(arg, mOn, mPixelX, mPixelY, "pixel="); });
    mParser.opt("pixelX", "<x>", "set pixel X",
                [&](Arg& arg) { return setSingleUInt(arg, mOn, mPixelX, "pixelX="); });
    mParser.opt("pixelY", "<y>", "set pixel Y",
                [&](Arg& arg) { return setSingleUInt(arg, mOn, mPixelY, "pixelY="); });
    mParser.opt("maxDepth", "<depth>", "set max depth",
                [&](Arg& arg) { return setSingleUInt(arg, mOn, mMaxDepth, "maxDepth="); }); 
    mParser.opt("pixelSamples", "<n>", "set pixel samples",
                [&](Arg& arg) { return setSingleUInt(arg, mOn, mPixelSamples, "pixelSamples="); });
    mParser.opt("lightSamples", "<n>", "set light samples",
                [&](Arg& arg) { return setSingleUInt(arg, mOn, mLightSamples, "lightSamples="); });
    mParser.opt("bsdfSamples", "<n>", "set BSDF samples",
                [&](Arg& arg) { return setSingleUInt(arg, mOn, mBsdfSamples, "bsdfSamples="); });

    mParser.opt("useSceneSamplesSw", "<on|off>", "set useSceneSamples condition",
                [&](Arg& arg) { return setSingleBool(arg, mOn, mUseSceneSamples, "useSceneSamplesSw="); });
    mParser.opt("occlusionRaysSw", "<on|off>", "set occlusionRays condition",
                [&](Arg& arg) { return setSingleBool(arg, mOn, mOcclusionRaysOn, "occlusionRaysSw="); });
    mParser.opt("specularRaysSw", "<on|off>", "set specularRays condition",
                [&](Arg& arg) { return setSingleBool(arg, mOn, mSpecularRaysOn, "specularRaysSw="); });
    mParser.opt("diffuseRaysSw", "<on|off>", "set diffuseRays condition",
                [&](Arg& arg) { return setSingleBool(arg, mOn, mDiffuseRaysOn, "diffuseRaysSw="); });
    mParser.opt("bsdfSamplesSw", "<on|off>", "set bsdfSamples condition",
                [&](Arg& arg) { return setSingleBool(arg, mOn, mBsdfSamplesOn, "bsdfSamplesSw="); });
    mParser.opt("lightSamplesSw", "<on|off>", "set lightSamples condition",
                [&](Arg& arg) { return setSingleBool(arg, mOn, mLightSamplesOn, "lightSamplesSw="); });

    mParser.opt("cameraRayColor", "<r> <g> <b>", "set cameraRayColor normalized 0~1 col value",
                [&](Arg& arg) { return setColorArg(arg, mOn, mCameraRayColor, "cameraRayColor="); });
    mParser.opt("specularRayColor", "<r> <g> <b>", "set specularRayColor normalized 0~1 col value",
                [&](Arg& arg) { return setColorArg(arg, mOn, mSpecularRayColor, "specularRayColor="); });
    mParser.opt("diffuseRayColor", "<r> <g> <b>", "set diffuseRayColor normalized 0~1 col value",
                [&](Arg& arg) { return setColorArg(arg, mOn, mDiffuseRayColor, "diffuseRayColor="); });
    mParser.opt("bsdfSampleColor", "<r> <g> <b>", "set bsdfSampleColor normalized 0~1 col value",
                [&](Arg& arg) { return setColorArg(arg, mOn, mBsdfSampleColor, "bsdfSampleColor="); });
    mParser.opt("lightSampleColor", "<r> <g> <b>", "set lightSampleColor normalized 0~1 col value",
                [&](Arg& arg) { return setColorArg(arg, mOn, mLightSampleColor, "lightSampleColor="); });

    mParser.opt("lineWidth", "<w>", "set line width",
                [&](Arg& arg) { return setSingleFloat(arg, mOn, mLineWidth, "lineWidth="); });
    mParser.opt("hiddenLineOpacity", "<opacity>", "set hidden line opacity",
                [&](Arg& arg) { return setSingleFloat(arg, mOn, mHiddenLineOpacity, "hiddenLineOpacity="); });

    mParser.opt("show", "", "show info",
                [&](Arg& arg) { return arg.msg(show() + '\n'); });
    mParser.opt("showPixel", "", "show pixel info",
                [&](Arg& arg) { return arg.msg(showPixel() + '\n'); });

    mParser.opt("deltaPixelX", "<delta>", "delta move pixelX or pixelY",
                [&](Arg& arg) { return setDeltaPixArg(arg, mOn, mPixelX, mPixelXmin, mPixelXmax, "pixelX="); });
    mParser.opt("deltaPixelY", "<delta>", "delta move pixelY",
                [&](Arg& arg) { return setDeltaPixArg(arg, mOn, mPixelY, mPixelYmin, mPixelYmax, "pixelY="); });
    mParser.opt("deltaPixelSamples", "<delta>", "delta add pixel samples",
                [&](Arg& arg) { return setDeltaSampleArg(arg, mOn, mPixelSamples, "pixelSamples="); });
    mParser.opt("deltaLightSamples", "<delta>", "delta add light samples",
                [&](Arg& arg) { return setDeltaSampleArg(arg, mOn, mLightSamples, "lightSamples="); });
    mParser.opt("deltaBsdfSamples", "<delta>", "delta add bsdf samples",
                [&](Arg& arg) { return setDeltaSampleArg(arg, mOn, mBsdfSamples, "bsdfSamples="); });
    mParser.opt("deltaMaxDepth", "<delta>", "delta add max depth",
                [&](Arg& arg) { return setDeltaSampleArg(arg, mOn, mMaxDepth, "maxDepth="); });

    mParser.opt("toggleUseSceneSamples", "", "toggle useSceneSamples",
                [&](Arg& arg) { return setToggleArg(arg, mOn, mUseSceneSamples, "useSceneSamples="); });
    mParser.opt("toggleOcclusionRays", "", "toggle occlusionRays",
                [&](Arg& arg) { return setToggleArg(arg, mOn, mOcclusionRaysOn, "occlusionRays="); });
    mParser.opt("toggleSpecularRays", "", "toggle specularRays",
                [&](Arg& arg) { return setToggleArg(arg, mOn, mSpecularRaysOn, "specularRays="); });
    mParser.opt("toggleDiffuseRays", "", "toggle diffuseRays",
                [&](Arg& arg) { return setToggleArg(arg, mOn, mDiffuseRaysOn, "diffuseRays="); });
    mParser.opt("toggleBsdfSamples", "", "toggle bsdfSamples",
                [&](Arg& arg) { return setToggleArg(arg, mOn, mBsdfSamplesOn, "bsdfSamples="); });
    mParser.opt("toggleLightSamples", "", "toggle lightSamples",
                [&](Arg& arg) { return setToggleArg(arg, mOn, mLightSamplesOn, "lightSamples="); });
}

//----------------------------------------------------------------------------------------------------------------------

PathVisualizer::PathVisualizer() 
    : mState(State::NONE), 
      mCameraIsectIndex(-1), 
      mMaxRayLength(-1.f), 
      mWidth(0), 
      mHeight(0), 
      mParams(nullptr),
      mLightSampleRayCount(0),
      mBsdfSampleRayCount(0),
      mDiffuseRayCount(0),
      mSpecularRayCount(0)
{
    parserConfigure();
}

PathVisualizer::~PathVisualizer() {}

// static function
scene_rdl2::grid_util::VectorPacketLineStatus::RayType
PathVisualizer::flagsToRayType(const uint8_t& flags)
//
// This function is used when the backend computation transfers Path Visualizer information and needs to
// determine the rayType based on the line's attribute flags. The core issue here is that the flag
// representing the rayType in PathVisualizer is not exposed as a public enum. Therefore, this function
// currently assumes that the uint8_t flags argument represents the PathVisualizer::Flags and performs
// the conversion based on that assumption.
// Ultimately, the flags representing the RayType should be made public. This is one of the improvements
// planned for the next Path Visualizer update.
//
{
    return flagsToRayType(static_cast<Flags>(flags));
}

// static function
scene_rdl2::grid_util::VectorPacketLineStatus::RayType
PathVisualizer::flagsToRayType(const Flags& flags)
{
    using RayType = scene_rdl2::grid_util::VectorPacketLineStatus::RayType;

    switch (flags) {
    case Flags::CAMERA : return RayType::CAMERA;
    case Flags::INACTIVE : return RayType::INACTIVE;
    case Flags::DIFFUSE : return RayType::DIFFUSE;
    case Flags::SPECULAR : return RayType::SPECULAR;
    case Flags::BSDF_SAMPLE : return RayType::BSDF_SAMPLE;
    case Flags::LIGHT_SAMPLE : return RayType::LIGHT_SAMPLE;
    case Flags::NONE :
    default : return RayType::NONE;
    }
}

scene_rdl2::math::Color
PathVisualizer::getColorByFlags(const uint8_t& flags) const
//
// This function is used when the backend computation transfers Path Visualizer information and needs to
// determine the color based on the line's attribute flags. The core issue here is that the flag
// representing the rayType in PathVisualizer is not exposed as a public enum. Therefore, this function
// currently assumes that the uint8_t flags argument represents the PathVisualizer::Flags and performs
// the conversion based on that assumption.
// Ultimately, the flags representing the RayType should be made public. This is one of the improvements
// planned for the next Path Visualizer update.
//
{
    return getRayColor(static_cast<Flags>(flags));
}

void PathVisualizer::initialize(const unsigned int width, const unsigned int height, 
                                const PathVisualizerParams* params, const float sceneSize) 
{
    MNRY_ASSERT(width != 0 && height != 0);

    mWidth = width;
    mHeight = height;
    mParams = params;
    mMaxRayLength = sceneSize;
    mState = State::READY;
}

// -------------------------------------------- BUILDING ---------------------------------------------------------------

void PathVisualizer::reset()
{
    mNodes.clear();
    mVertexBuffer.clear();
}

bool PathVisualizer::setUpFrustum(const Camera& cam)
{
    if (!cam.hasFrustum()) { return false; }

    mFrustum = std::make_unique<mcrt_common::Frustum>();
    cam.computeFrustum(mFrustum.get(), 0, true);  // frustum at shutter open
    // Our points are in world space, so transform the clipping planes
    mFrustum->transformClipPlanes(cam.getCamera2World());

    return true;
}

void PathVisualizer::recordOcclusionRay(const mcrt_common::Ray& ray, const Scene& scene, 
                                        const uint32_t pixel, const bool lightSampleFlag, const bool occlusionFlag)
{
    recordRay(ray, scene, pixel, /* lobeType */ 0, lightSampleFlag, occlusionFlag);
}

void PathVisualizer::recordRegularRay(const mcrt_common::Ray& ray, const Scene& scene,
                                      const uint32_t pixel, const int lobeType)
{
    recordRay(ray, scene, pixel, lobeType, /*lightSampleFlag*/ false, /*occlusionFlag*/ false);
}

void PathVisualizer::recordRay(const mcrt_common::Ray& ray, const Scene& scene, 
                               const uint32_t pixel, const int lobeType, 
                               const bool lightSampleFlag, const bool occlusionFlag)
{
    MNRY_ASSERT(mState == State::RECORD);
    RenderTimer timer(mInRenderingTime);

    // Only record the ray if it matches the user parameters
    if (!matchesParams(lobeType, lightSampleFlag, ray.getDepth())) {
        return;
    }

    // Calculate the pixel ID
    const uint32_t pixelID = uint32ToPixelY(pixel) * mWidth + uint32ToPixelX(pixel);

    // Calculate ray endpoints (render space)
    const float tfar = std::min(ray.tfar, mMaxRayLength);
    const Vec3f rayOrigin = ray.getOrigin();
    const Vec3f rayEnd = rayOrigin + scene_rdl2::math::normalize(ray.getDirection()) * tfar;

    // Compute world-space coordinates (bc we want these points to stay consistent even if the camera transform changes)
    const Vec3f rayOriginWorld = transformPoint(scene.getRender2World(), rayOrigin);
    const Vec3f rayEndWorld    = transformPoint(scene.getRender2World(), rayEnd);

    // Set the flags
    Flags flags;
    const bool diffuseFlag  = lobeType & shading::BsdfLobe::DIFFUSE;
    const bool specularFlag = (lobeType & shading::BsdfLobe::GLOSSY) | (lobeType & shading::BsdfLobe::MIRROR);
    const bool cameraFlag = ray.getDepth() == 0;
    setFlags(flags, diffuseFlag, specularFlag, lightSampleFlag, cameraFlag);

    {
        std::lock_guard<std::mutex> lock(mWriteLock);
        // Add vertex to our vertex list (or find it, if it already exists) and return the index
        const int rayOriginIndex  = addVertex(rayOriginWorld);
        const int rayEndIndex     = addVertex(rayEndWorld);

        int rayIsectIndex = -1;
        if (occlusionFlag) { 
            // If a shadow ray is occluded, we want to find the first object it intersects
            const Vec3f isectPt = findSceneIsect(ray, scene);
            rayIsectIndex = addVertex(isectPt);
        }
        addNode(rayOriginIndex, rayEndIndex, rayIsectIndex, ray.getDepth(), flags);
    }
}

// ---------------------------------------------- FILTERING ------------------------------------------------------------

bool PathVisualizer::matchesParams(const int lobeType, const bool lightSampleFlag, const int depth) const
{
    bool recordRay = depth <= mParams->mMaxDepth;
    recordRay = recordRay && matchesFlags(lobeType, lightSampleFlag, depth);
    return recordRay;
}

// -------------------------------------- DRAWING ----------------------------------------------------------------------

bool PathVisualizer::pixelIsOccluded(const PixelCoordU& p, const PixelCoordI& p1,
                                     const pbr::Scene* scene, const float totalDistance,
                                     const float invDepth1, const float invDepthDiff) const
{
    // Find t value in pixel space
    const float distanceToPixel = scene_rdl2::math::distance(Vec2f(p.x, p.y), Vec2f(p1.x, p1.y));
    const float t = distanceToPixel / totalDistance;

    // Find depth of this point along the line using
    // rasterization-based depth interpolation
    const float depth = 1.f / (invDepth1 + t*invDepthDiff);

    /// TODO: There should be a way to do this more efficiently using an occlusion ray,
    /// by setting tfar to the depth above; however, this depth is in camera space, and
    /// the occlusion test happens in render space, so the depth somehow needs to be in render space

    // Cast ray to find the first scene intersection
    mcrt_common::RayDifferential ray;
    scene->getCamera()->createRay(&ray, p.x + 0.5f, p.y + 0.5f, 0.f, 0.5f, 0.5f, false);
    scene->getEmbreeAccelerator()->intersect(ray);

    if (isinf(ray.tfar)) {
        // if there is no scene intersection, then the pixel is not occluded
        return false;
    } else {
        // if there IS a scene intersection, find its depth and compare to the ray visualization's depth
        const Vec3f isect = ray.getOrigin() + ray.getDirection() * ray.tfar;
        const scene_rdl2::math::Vec4f isect4d(isect.x, isect.y, isect.z, 1.f);
        const float sceneDepth = -(float) dot(isect4d, scene->getCamera()->getRender2Camera().col2());

        return sceneDepth < depth;
    }
}

uint8_t PathVisualizer::clipPoints(const int nodeIndex, Vec3f* outPoints, bool* clipStatus) const
{
    uint8_t numPoints = 0;

    // world-space starting point
    outPoints[numPoints++] = getRayOrigin(nodeIndex);

    // If the ray is an occlusion ray that's failed the occlusion test,
    // we found the world-space closest intersection in recordRay()
    // We now need to convert it to screen space for drawing.
    const bool hasIsect = mNodes[nodeIndex].mRayIsectIndex != -1;
    if (hasIsect) { 
        outPoints[numPoints++] = getRayIsect(nodeIndex);
    }

    // world-space ending point
    outPoints[numPoints++] = getRayEndpoint(nodeIndex);

    // ----- Clip world-space points to viewing frustum -----
    auto clipLine = [&] (Vec3f& p1, Vec3f& p2) {
        Vec3f raySegmentClipped[2];
        const bool lineClipped = mFrustum->clipLine(p1, p2, raySegmentClipped[0], raySegmentClipped[1]);
        if (!lineClipped) {
            return false;
        }
        p1 = raySegmentClipped[0];
        p2 = raySegmentClipped[1];
        return true;
    };

    // ----- Clip the line(s) -----
    bool clippedLine = false;
    for (uint8_t i = 0; i < numPoints - 1; ++i) {
        const bool clippedCurrentLine = clipLine(outPoints[i], outPoints[i+1]);
        clippedLine = clippedLine || clippedCurrentLine;
    }

    /// If the isect and/or startpoint/endpoint have been clipped, flag it
    if (hasIsect) {
        clipStatus[0] = outPoints[0] != getRayOrigin(nodeIndex);
        clipStatus[1] = outPoints[1] != getRayIsect(nodeIndex);
        clipStatus[2] = outPoints[2] != getRayEndpoint(nodeIndex);
    } else {
        clipStatus[0] = outPoints[0] != getRayOrigin(nodeIndex);
        clipStatus[1] = outPoints[1] != getRayEndpoint(nodeIndex);
    }
   
    return clippedLine ? numPoints : 0;
}

void PathVisualizer::addLineSegment(const unsigned nodeIndex,
                                    const PosType startPosType,
                                    const PosType endPosType,
                                    const PixelCoordU& start, const PixelCoordU& end, const Flags& flags, 
                                    const bool drawEndpoint, const bool isOccluded)
{
    std::lock_guard<std::mutex> lock(mWriteLock);

    // If the line has no length, exit the function without adding a new line segment
    if (start.x == end.x && start.y == end.y) {
        return;
    }
    float lineOpacity = isOccluded ? mParams->mHiddenLineOpacity : 1.f;
    mLines.push_back({start, end, flags, drawEndpoint, lineOpacity, nodeIndex, startPosType, endPosType});
}

void PathVisualizer::traceLine(const unsigned nodeIndex,
                               const PosType startPosType,
                               const PosType endPosType,
                               const PixelCoordI& start, const PixelCoordI& end, 
                               const std::function<bool(const PixelCoordU&)>& isOccludedFunc,
                               const Flags flags, const bool endpointClipped)
{
    const PixelCoordI lineStart = start;
    const PixelCoordI lineEnd = end;

    /* Uses Wu's line drawing algorithm to trace the line until it finds where it's occluded.
     * Once it does, it creates a new line segment. It continues creating new segments
     * until the line is fully traced.
     */

    const uint32_t dx = abs(end.x - start.x);     // change in x
    const uint32_t dy = abs(end.y - start.y);     // change in y

    if (dx == 0 && dy == 0) { return; }

    int sx = start.x < end.x ? 1 : -1;      // step in the x-direction
    int sy = start.y < end.y ? 1 : -1;      // step in the y-direction

    bool steep = false;                      // is the slope > 1 ?
    int x0 = start.x;                        // starting point x
    int x1 = end.x;                          // ending point x
    int y0 = start.y;                        // starting point y
    int y1 = end.y;                          // ending point y

    // Certain values need to be doubles, since using a float leads to precision
    // issues, which causes the line to sometimes stop one pixel short
    double slope;                            // slope/gradient of line  

    // We always want to be drawing a line with
    // a gradual slope, so if it's a steep line, swap the x and y
    // coordinates so we can draw a line with slope < 1
    if (dy > dx) {
        steep = true;
        slope = (double) dx / dy;
        std::swap(x0, y0);
        std::swap(x1, y1);
        std::swap(sx, sy);
    } else {
        slope = (double) dy / dx; 
    }

    PixelCoordU segmentStart = {0,0};
    PixelCoordU segmentEnd = {0,0};
    bool isFirstSegment = true;
    bool prevIsOccluded = false;

    auto calcPosType = [&](const PixelCoordI& orig, const PixelCoordU& p, const PosType posType) -> PosType {
        if (orig.x == static_cast<int>(p.x) && orig.y == static_cast<int>(p.y)) return posType;
        return PosType::UNKNOWN;
    };

    double yIntersect = y0;
    // This is a gradually increasing line, so
    // we always increase in x, and conditionally increase in y
    for (int x = x0; x != x1+sx; x += sx, yIntersect += slope * sy) {
        // find the integer part of yIntersect
        // which is the new y coordinate
        const int y = (int) yIntersect;
        const PixelCoordI candidate = steep ? PixelCoordI{y, x} : PixelCoordI{x, y};

        if (!isInBounds(candidate)) {
            continue; 
        }

        const PixelCoordU out = {static_cast<uint32_t>(candidate.x), static_cast<uint32_t>(candidate.y)};

        if (isFirstSegment) {
            // when we find a pixel that's in bounds, set it as the start point
            segmentStart = out;
            isFirstSegment = false;
        }

        // update the endpoint until we reach the last in-bounds pixel
        segmentEnd = out;

        // if the previous pixel was occluded, but now isn't, OR
        // if the previous pixel was NOT occluded, but now is
        // end the current line segment, and start a new one
        const bool currIsOccluded = isOccludedFunc(out);
        if (currIsOccluded != prevIsOccluded) {
            // Check if the segment start/end point is the same as the line start/end point.
            // If it isn't, the point is interior on the line, or clipped by view frustum,
            // and its position type is UNKNOWN.
            const PosType sPosType = calcPosType(lineStart, segmentStart, startPosType);
            const PosType ePosType = calcPosType(lineEnd, segmentEnd, endPosType);

            addLineSegment(nodeIndex, sPosType, ePosType,
                           segmentStart, segmentEnd, flags, /* draw endpoint */ !endpointClipped, prevIsOccluded);
            prevIsOccluded = currIsOccluded;
            segmentStart = out;
        }
    }
    // add the last line segment
    const PosType sPosType = calcPosType(lineStart, segmentStart, startPosType);
    const PosType ePosType = calcPosType(lineEnd, segmentEnd, endPosType);
    addLineSegment(nodeIndex, sPosType, ePosType,
                   segmentStart, segmentEnd, flags, /* draw endpoint */ !endpointClipped, prevIsOccluded);
}

void PathVisualizer::generateLine(const int nodeIndex, const Scene* scene)
{
    MNRY_ASSERT(nodeIndex >= 0 && nodeIndex < mNodes.size());

    const pbr::Camera* cam = scene->getCamera();

    /// ---- Save the position of the pixel focus ----------------------
    if (isCameraRay(nodeIndex) && mCameraIsectIndex == -1) {
        mCameraIsectIndex = mNodes[nodeIndex].mRayEndpointIndex;
    }

     /// ---- Clip the world-space points using the viewing frustum ------
    // Can have 2-3 points per line:
    //  - rayOrigin
    //  - rayEndpoint
    //  - scene intersection (if occlusion ray)
    Vec3f clippedPoints[3];
    bool clipStatus[3] = {false, false, false};
    uint8_t numPoints = clipPoints(nodeIndex, clippedPoints, clipStatus);
    if (numPoints == 0) { return; }

    /// ---- Transform the points into screen-space ---------------------
    PixelCoordI pixelEndpoints[3];
    float inverseDepths[3];

    for (uint8_t i = 0; i < numPoints; ++i) {
        const Vec3f& clippedPoint = clippedPoints[i];

        // Transform the point to screen-space
        Vec2f p = transformPointWorld2Screen(clippedPoint, cam);
        pixelEndpoints[i] = {static_cast<int>(std::round(p.x)), static_cast<int>(std::round(p.y))};

        // Calculate the inverse depth
        /// NOTE: this assumes we're using a Perspective camera
        scene_rdl2::math::Vec4d clippedPoint4d(clippedPoint.x, clippedPoint.y, clippedPoint.z, 1.f);
        const float cpz = -(float) dot(clippedPoint4d, cam->getWorld2Camera().col2());
        inverseDepths[i] = 1.f / cpz;
    }

    // ---- Draw line(s) ----------------------------------------------
    for (uint8_t i = 0; i < numPoints - 1; ++i) {

        const float inverseDepthDiff = inverseDepths[i+1] - inverseDepths[i];
        const float distance = scene_rdl2::math::distance(Vec2f(pixelEndpoints[i].x, pixelEndpoints[i].y), 
                                                          Vec2f(pixelEndpoints[i+1].x, pixelEndpoints[i+1].y));
        auto isOccludedFunc = [&] (const PixelCoordU& p) {
            return pixelIsOccluded(p, pixelEndpoints[i], scene, distance, inverseDepths[i], inverseDepthDiff);
        };

        // if the line has been split, the second part is the "inactive"
        // part of an occlusion ray
        const Flags flags = i > 0 ? Flags::INACTIVE : mNodes[nodeIndex].mFlags;

        PosType startPosType {PosType::UNKNOWN};
        PosType endPosType {PosType::UNKNOWN};
        if (numPoints == 2) {
            startPosType = PosType::START;
            endPosType = PosType::END;
        } else if (numPoints == 3) {
            if (i == 0) {
                startPosType = PosType::START;
                endPosType = PosType::ISECT;
            } else {
                startPosType = PosType::ISECT;
                endPosType = PosType::END;
            }
        }
        if (clipStatus[i]) startPosType = PosType::UNKNOWN;
        if (clipStatus[i+1]) endPosType = PosType::UNKNOWN; 

        traceLine(static_cast<unsigned>(nodeIndex), startPosType, endPosType,
                  pixelEndpoints[i], pixelEndpoints[i+1], isOccludedFunc, flags, clipStatus[i+1]);
    }
}

void PathVisualizer::generateLines(const Scene* scene)
{
    mLines.clear();
    /// ---- Set up the camera frustum -------------------------------------
    if (!setUpFrustum(*scene->getCamera())) {
        std::cout << "Camera must have a valid frustum\n";
        return;
    }

    if (mNodes.size() == 0) {
        std::cout << "No rays match user parameters at (" << mParams->mPixelX << ", " << mParams->mPixelY << ")\n";
        return;
    }

    resetCameraIsectIndex();

    rndr::simpleLoop (/*parallel*/ true, 0u, (unsigned) mNodes.size(), [&](int nodeIndex) {
        generateLine(nodeIndex, scene);
    });
}

void PathVisualizer::draw(scene_rdl2::fb_util::RenderBuffer* renderBuffer, const Scene* scene)
{
    /// ---- Create a function that will write to the render buffer --------
    auto writeToRenderBuffer = [&] (const PixelCoordU& p, const scene_rdl2::math::Color& color, float a) {
        if (!isInBounds(p)) { return; }
        a = scene_rdl2::math::clamp(a, 0.f, 1.f);

        auto& renderColor = renderBuffer->getPixel(p.x, p.y);
        renderColor.x = a * color.r + (1.f - a) * renderColor.x;
        renderColor.y = a * color.g + (1.f - a) * renderColor.y;
        renderColor.z = a * color.b + (1.f - a) * renderColor.z;
        renderColor.w = 1;
    };

    /// ---- For each line, draw it on top of the render buffer ---------------
    using Color = scene_rdl2::math::Color;

    crawlAllLines([&](const Vec2i& px1,
                      const Vec2i& px2,
                      const uint8_t& flags,
                      const float alpha,
                      const float width,
                      const bool drawEndpoint,
                      const unsigned nodeId,
                      const PosType startPosType,
                      const PosType endPosType) {
        scene_rdl2::math::Color c = getRayColor(static_cast<Flags>(flags));
        auto writeFunc = [&] (int px, int py, float a) {
            writeToRenderBuffer({static_cast<uint32_t>(px), static_cast<uint32_t>(py)}, c, alpha * a);
        };

        const unsigned uWidth = static_cast<unsigned>(width);
        drawing::drawLineWu(px1.x, px1.y, px2.x, px2.y, uWidth, writeFunc);
        
        if (drawEndpoint) {
            drawing::drawCircle(px2.x, px2.y, width + 2, writeFunc);
        }
    });
}

/// --------------------------------------------- Utilities ------------------------------------------------------------

Vec3f PathVisualizer::findSceneIsect(const mcrt_common::Ray& ray, const Scene& scene) const
{
    // Offset the pt to prevent self-intersection
    /// TODO: We are still getting many false negatives (i.e. finding 
    /// intersections very close to the ray origin). 
    /// Explore a more stable alternative.
    const Vec3f offsetPt = ray.getOrigin() + 0.0001f * ray.getDirection();
    mcrt_common::Ray testRay(offsetPt, ray.getDirection());
    scene.getEmbreeAccelerator()->intersect(testRay); 
    MNRY_ASSERT(testRay.geomID != RTC_INVALID_GEOMETRY_ID);

    // Compute world-space point
    const Vec3f isectPt = testRay.getOrigin() + testRay.tfar * testRay.getDirection();
    return transformPoint(scene.getRender2World(), isectPt);
}

Vec2f 
PathVisualizer::transformPointWorld2Screen(const Vec3f& p, const pbr::Camera* cam) const
{
    const scene_rdl2::math::Vec4d camP = transformPoint(cam->getWorld2Camera(), p);
    Vec3f result = mFrustum->projectToViewport(Vec3f(camP.x, camP.y, camP.z));
    result.x -= cam->getRegionToApertureOffsetX();
    result.y -= cam->getRegionToApertureOffsetY();
    return Vec2f(result.x, result.y);
}

/// -------- Getters ---------

inline bool PathVisualizer::matchesFlag(const int nodeIndex, const Flags& flag) const 
{
    return mNodes[nodeIndex].mFlags == flag;
}

inline bool PathVisualizer::matchesFlag(const int lobeType, const int flag) const 
{
    return static_cast<uint8_t>(lobeType) & static_cast<uint8_t>(flag);
}

inline bool PathVisualizer::matchesFlags(const int lobeType, const bool lightSampleFlag, const int depth) const
{
    if (depth == 0) {
        // there are no filters for camera rays
        return true;
    }

    bool matches = true;
    if (matchesFlag(lobeType, shading::BsdfLobe::GLOSSY) || 
        matchesFlag(lobeType, shading::BsdfLobe::MIRROR)) {
        matches = matches && mParams->mSpecularRaysOn;
    } else if (matchesFlag(lobeType, shading::BsdfLobe::DIFFUSE)) {
        matches = matches && mParams->mDiffuseRaysOn;
    } else if (lightSampleFlag) {
        matches = matches && mParams->mLightSamplesOn && mParams->mOcclusionRaysOn;
    } else {
        matches = matches && mParams->mBsdfSamplesOn && mParams->mOcclusionRaysOn;
    }
    return matches;
}

inline int PathVisualizer::getRayDepth(const int nodeIndex) const
{
    return mNodes[nodeIndex].mDepth;
}

inline bool PathVisualizer::isCameraRay(const int nodeIndex) const
{
    return getRayDepth(nodeIndex) == 0;
}

inline Vec3f PathVisualizer::getRayOrigin(const int nodeIndex) const
{
    const int rayOriginIndex = mNodes[nodeIndex].mRayOriginIndex;

    MNRY_ASSERT(rayOriginIndex >= 0 && rayOriginIndex < mVertexBuffer.size());
    return mVertexBuffer[rayOriginIndex];
}

inline Vec3f PathVisualizer::getRayEndpoint(const int nodeIndex) const
{
    const int rayEndpointIndex = mNodes[nodeIndex].mRayEndpointIndex;

    MNRY_ASSERT(rayEndpointIndex >= 0 && rayEndpointIndex < mVertexBuffer.size());
    return mVertexBuffer[rayEndpointIndex];
}

inline Vec3f PathVisualizer::getRayIsect(const int nodeIndex) const
{
    const int rayIsectIndex = mNodes[nodeIndex].mRayIsectIndex;

    if (rayIsectIndex == -1) { return Vec3f(0.f); }

    MNRY_ASSERT(rayIsectIndex >= 0 && rayIsectIndex < mVertexBuffer.size());
    return mVertexBuffer[rayIsectIndex];
}

inline scene_rdl2::math::Color PathVisualizer::getRayColor(const Flags& flags) const
{
    switch(flags) {
        case Flags::CAMERA:         return mParams->mCameraRayColor;
        case Flags::INACTIVE:       return scene_rdl2::math::Color(0, 0, 0);
        case Flags::DIFFUSE:        return mParams->mDiffuseRayColor;
        case Flags::SPECULAR:       return mParams->mSpecularRayColor;
        case Flags::LIGHT_SAMPLE:   return mParams->mLightSampleColor;
        case Flags::BSDF_SAMPLE:    return mParams->mBsdfSampleColor;
        default:                    return scene_rdl2::math::Color(1, 1, 1);
    }
}

/// -------- Setters --------

void PathVisualizer::setState(State state)
{
    std::lock_guard<std::mutex> lock(mWriteLock);
    mState = state;
}

inline void PathVisualizer::setFlags(Flags& flags, const bool isDiffuse, const bool isSpecular, 
                                     const bool isLightSample, const bool isCameraRay) const
{
    flags = Flags::NONE;
    if (isCameraRay) {
        flags = Flags::CAMERA;
    }
    else if (isDiffuse) {
        mDiffuseRayCount++;
        flags = Flags::DIFFUSE; 
    } 
    else if (isSpecular) {
        mSpecularRayCount++;
        flags = Flags::SPECULAR; 
    }
    else if (isLightSample) {
        mLightSampleRayCount++;
        flags = Flags::LIGHT_SAMPLE;
    }
    else { 
        mBsdfSampleRayCount++;
        flags = Flags::BSDF_SAMPLE;
    }
}

inline int PathVisualizer::addVertex(const Vec3f& v)
{
    // Check the most recent vertex added. If it's the same, don't add again.
    // This is a minor optimization based on the assumption that rays added 
    // sequentially often share the same start/end points.
    if (mVertexBuffer.size() == 0 || !isEqual(v, mVertexBuffer.back())) {
        // If the search fails, add the vertex
        mVertexBuffer.push_back(v);
    }
    // Return the index of the vert position
    return mVertexBuffer.size() - 1;
}

inline void PathVisualizer::addNode(const int originIndex, const int endpointIndex, const int isectIndex, 
                                    const int depth, const Flags& flags)
{
    MNRY_ASSERT(originIndex >= 0 && originIndex < mVertexBuffer.size());
    MNRY_ASSERT(endpointIndex >= 0 && endpointIndex < mVertexBuffer.size());
    MNRY_ASSERT(isectIndex == -1 || (isectIndex >= 0 && isectIndex < mVertexBuffer.size()));

    mNodes.emplace_back(originIndex, endpointIndex, isectIndex, depth, flags);
}

/// -------- Statistics helpers --------

size_t PathVisualizer::getMemoryFootprint() const
{
    const size_t nodesSize = sizeof(mNodes) + sizeof(Node) * mNodes.size();
    const size_t vertsSize = sizeof(mVertexBuffer) + sizeof(Vec3f) * mVertexBuffer.size();

    const size_t totalSize = 
                    nodesSize                           + /* mNodes */
                    vertsSize                           + /* mVertexBuffer */
                    sizeof(PathVisualizerParams*)       + /* mParams */
                    sizeof(State)                       + /* mState */
                    sizeof(int) * 2                     + /* mWidth, mHeight */
                    sizeof(bool)                        + /* mNeedRenderRefresh */
                    sizeof(int)                         + /* mCameraIsectIndex */
                    sizeof(mcrt_common::Frustum)        + /* mFrustum */
                    sizeof(float)                       + /* mMaxRayLength */
                    sizeof(std::mutex)                  + /* mWriteLock */
                    sizeof(moonray::util::AverageDouble)+ /* mInRenderingTime */
                    sizeof(moonray::util::AverageDouble)  /* mPostRenderingTime */
                    ;

    return totalSize;
}

void PathVisualizer::printTimeStats() const
{
    const double inRenderTime = mInRenderingTime.getSum() / mcrt_common::getNumTBBThreads();
    const double postRenderTime = mPostRenderingTime.getSum() / mcrt_common::getNumTBBThreads();
    std::cout << "In Rendering Time / # threads: " << inRenderTime << " ms\n";
    std::cout << "Post-Rendering Time / # threads: " << postRenderTime << " ms\n";
    std::cout << "Combined Time: " << (inRenderTime + postRenderTime) << " ms\n";
}

void PathVisualizer::printNode(const int nodeIndex) const {
    if (nodeIndex < 0 || nodeIndex >= mNodes.size()) {
        std::cout << "Node at " << nodeIndex << " doesn't exist\n";
    }

    std::cout << "Node {\n";
    std::cout << "  ID: " << nodeIndex << ",\n";
    std::cout << "  mRayOrigin: " << getRayOrigin(nodeIndex) << ",\n";
    std::cout << "  mRayEndpoint: " << getRayEndpoint(nodeIndex) << ",\n";
    std::cout << "  mRayIsect: " << getRayIsect(nodeIndex) << ",\n";
    std::cout << "  mDepth: " << mNodes[nodeIndex].mDepth << ",\n";
    std::cout << "  mFlags: " << mNodes[nodeIndex].mFlags << ",\n";
    std::cout << "}\n";
}

void PathVisualizer::printNodes(const std::vector<int>& nodeList) const
{
    std::cout << "-------- Printing out nodes ---------\n";
    for (const int nodeIndex : nodeList) {
        printNode(nodeIndex);
    }
    std::cout << "\n";
}

void PathVisualizer::printNodes(const int maxEntries) const
{
    std::cout << "-------- Printing out nodes ---------\n";
    for (size_t i = 0; i < mNodes.size(); ++i) {
        if (maxEntries != -1 && static_cast<int>(i) >= maxEntries) { return; }
        printNode(static_cast<int>(i));
    }
    std::cout << "\n";
}

bool
PathVisualizer::getCamPos(scene_rdl2::math::Vec3f& camPos) const
{
    bool foundFlag = false;
    crawlAllNodes(Flags::CAMERA,
                  [&](const Node& node) {
                      foundFlag = true;
                      camPos = mVertexBuffer[node.mRayOriginIndex];
                      return false; // early exit
                  });
    return foundFlag;
}

std::vector<scene_rdl2::math::Vec3f>
PathVisualizer::getCamRayIsectSfPos() const
//
// Returns all the camera ray intersection points with the surface.
//
{
    std::vector<scene_rdl2::math::Vec3f> tbl;
    if (mNodes.empty()) return tbl;
    crawlAllNodes(Flags::CAMERA,
                  [&](const Node& node) {
                      tbl.push_back(mVertexBuffer[node.mRayEndpointIndex]);
                      return true;
                  });
    return tbl;
}

size_t
PathVisualizer::serializeNodeDataAll(std::string& buff) const
//
// Return non-zero data size even if node total and vtx total both are zero,
// because the data size is encoded.
//
{
    scene_rdl2::cache::ValueContainerEnqueue vce(&buff);

    /* for debug
    std::cerr << ">> PathVisualizer.cc serializeNodeDataAll() mNodes.size():" << mNodes.size()
              << " mVertexBuffer.size():" << mVertexBuffer.size() << '\n';
    */

    vce.enqVLSizeT(mNodes.size());
    if (mNodes.size()) {
        for (const auto& node : mNodes) {
            scene_rdl2::grid_util::VectorPacketNode vecPacketNode(node.mRayOriginIndex,
                                                                  node.mRayEndpointIndex,
                                                                  node.mRayIsectIndex,
                                                                  static_cast<int>(node.mDepth),
                                                                  flagsToRayType(node.mFlags));
            vecPacketNode.enq(vce);
        }
    }

    vce.enqVLSizeT(mVertexBuffer.size());
    if (mVertexBuffer.size()) {
        vce.enqVec3fVector(mVertexBuffer);
    }
    return vce.finalize();
}

void
PathVisualizer::parserConfigure()
{
    mParser.description("PathVisualizer command");

    mParser.opt("showFlowCtrlStatus", "", "show current pathVisualizer status related it's flow control",
                [&](Arg& arg) { return arg.msg(showFlowCtrlState() + '\n'); });
    mParser.opt("showLinesInfo", "", "show line data information",
                [&](Arg& arg) { return arg.msg(showLinesInfo() + '\n'); });
    mParser.opt("startSim", "", "start simulation phase and constructs PathVis dataBase",
                [&](Arg& arg) {
                    if (!mOn) return arg.msg("skip\n");
                    mState = State::READY;
                    reset();
                    mState = State::START_RECORD;
                    return arg.msg(showFlowCtrlState() + '\n');
                });
    mParser.opt("showNode", "", "show node information",
                [&](Arg& arg) { return arg.msg(showNodeInfo() + '\n'); });
    mParser.opt("showCamPos", "", "show camera position",
                [&](Arg& arg) { return arg.msg(showCamPos() + '\n'); });
    mParser.opt("showCamRayIsectSfPos", "", "show camera ray intersect surface positions",
                [&](Arg& arg) { return arg.msg(showCamRayIsectSfPos() + '\n'); });
}

std::string
PathVisualizer::showFlowCtrlState() const
{
    std::ostringstream ostr;
    ostr << "flowControlStatus {\n"
         << "  mState:" << mState << '\n'
         << "  mNeedRenderRefresh:" << scene_rdl2::str_util::boolStr(mNeedRenderRefresh) << '\n'
         << "}";
    return ostr.str();
}

std::string
PathVisualizer::showLinesInfo() const
{
    if (!mLines.size()) return "linesInfo (size:0)";

    auto showLineSegment = [](const LineSegment& line) {
        auto showPos = [](const unsigned v) {
            std::ostringstream ostr;
            ostr << std::setw(4) << v;
            return ostr.str();
        };
        auto showPosXY = [&](const unsigned x, const unsigned y) {
            return "(" + showPos(x) + ',' + showPos(y) + ")";
        };
        auto showCol = [](const float c) {
            std::ostringstream ostr;
            ostr << std::setw(5) << std::fixed << std::setprecision(3) << c;
            return ostr.str();
        };

        std::ostringstream ostr;
        ostr << "s" << showPosXY(line.mPx1.x, line.mPx1.y)
             << " e" << showPosXY(line.mPx2.x, line.mPx2.y)
             << " f:" << line.mFlags
             << " a:" << showCol(line.mAlpha)
             << " drawE:" << scene_rdl2::str_util::boolStr(line.mDrawEndpoint)
             << " nId:" << line.mNodeIndex
             << " sPos:" << scene_rdl2::grid_util::VectorPacketLineStatus::posTypeStr(line.mStartPosType)
             << " ePos:" << scene_rdl2::grid_util::VectorPacketLineStatus::posTypeStr(line.mEndPosType);
        return ostr.str();
    };

    const int wi = scene_rdl2::str_util::getNumberOfDigits(mLines.size());

    std::ostringstream ostr;
    ostr << "linesInfo (size:" << mLines.size() << ") mParams->mLineWidth:" << mParams->mLineWidth << " {\n";
    for (size_t i = 0; i < mLines.size(); ++i) {
        ostr << "  i:" << std::setw(wi) << i << ' ' << showLineSegment(mLines[i]) << '\n';
    }
    ostr << "}";
    return ostr.str();
}

std::string
PathVisualizer::showNodeInfo() const
{
    auto showPosId = [&](const int posId) {
        auto showPos = [](const scene_rdl2::math::Vec3f& v) {
            auto showF = [](const float v) {
                std::ostringstream ostr;
                ostr << std::setw(10) << std::fixed << std::setprecision(5) << v;
                return ostr.str();
            };
            std::ostringstream ostr;
            ostr << '(' << showF(v[0]) << ',' << showF(v[1]) << ',' << showF(v[2]) << ')';
            return ostr.str();
        };
        std::ostringstream ostr;
        const int w = scene_rdl2::str_util::getNumberOfDigits(mVertexBuffer.size());
        ostr << std::setw(w) << posId << ':' << showPos(mVertexBuffer[posId]);
        return ostr.str();
    };
    auto showNode = [&](const int nodeId) {
        auto showFlag = [&](const Flags& flag) {
            std::ostringstream ostr;
            ostr << "0x" << std::setw(2) << std::hex << static_cast<int>(flag) << ' ' << flag;
            return ostr.str();
        };
        std::ostringstream ostr;
        const int w = scene_rdl2::str_util::getNumberOfDigits(mNodes.size());
        const Node& node = mNodes[nodeId];
        ostr << "node:" << std::setw(w) << nodeId << " {\n"
             << scene_rdl2::str_util::addIndent("  mRayOriginIndex:" + showPosId(node.mRayOriginIndex)) << '\n'
             << scene_rdl2::str_util::addIndent("mRayEndpointIndex:" + showPosId(node.mRayEndpointIndex)) << '\n'
             << scene_rdl2::str_util::addIndent("   mRayIsectIndex:" + showPosId(node.mRayIsectIndex)) << '\n'
             <<                               "             mDepth:" << static_cast<int>(node.mDepth) << '\n'
             <<                               "             mFlags:" << showFlag(node.mFlags) << '\n'
             << "}";
        return ostr.str();
    };

    std::ostringstream ostr;
    ostr << "node info (size:" << mNodes.size() << ") {\n";
    for (size_t nodeId = 0; nodeId < mNodes.size(); ++nodeId) {
        ostr << scene_rdl2::str_util::addIndent(showNode(nodeId)) << '\n';
    }
    ostr << "}";
    return ostr.str();
}

std::string
PathVisualizer::showCamPos() const
{
    scene_rdl2::math::Vec3f camPos;
    const bool flag = getCamPos(camPos);

    std::ostringstream ostr;
    if (flag) ostr << "camPos:" << camPos;
    else      ostr << "camPos:empty"; 
    return ostr.str();
}

std::string
PathVisualizer::showCamRayIsectSfPos() const
{
    std::vector<scene_rdl2::math::Vec3f> tbl = getCamRayIsectSfPos();

    const int wi = scene_rdl2::str_util::getNumberOfDigits(tbl.size());
    std::ostringstream ostr;
    ostr << "camRayIsectSfPos (size:" << tbl.size() << ") {\n";
    for (size_t i = 0; i < tbl.size(); ++i) {
        ostr << "  i:" << std::setw(wi) << i << tbl[i] << '\n';
    }
    ostr << "}";
    return ostr.str();
}

// static function
std::string
PathVisualizer::stateStr(const State state)
{
    std::ostringstream ostr;
    ostr << state;
    return ostr.str();
}

} // end namespace rndr
} // end namespace moonray
