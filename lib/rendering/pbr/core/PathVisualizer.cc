// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "PathVisualizer.h"

#include <moonray/common/time/Timer.h>
#include <moonray/rendering/mcrt_common/Ray.h>
#include <moonray/rendering/pbr/camera/Camera.h>
#include <moonray/rendering/pbr/camera/PerspectiveCamera.h>
#include <moonray/rendering/pbr/core/Scene.h>
#include <moonray/rendering/rndr/RenderContext.h>
#include <moonray/rendering/rndr/Util.h>
#include <moonray/rendering/rt/EmbreeAccelerator.h>
#include <moonray/rendering/mcrt_common/Frustum.h>
#include <moonray/rendering/drawing/drawing.h>

#include <scene_rdl2/common/fb_util/FbTypes.h>

using RenderTimer = moonray::time::RAIITimerAverageDouble;

namespace moonray {
namespace pbr {

PathVisualizer::PathVisualizer(int width, int height, int pixelSamplesSqrt, 
                               const PathVisualizerParams* params, float sceneSize) 
    : mOn(true), 
      mCameraIsectIndex(-1), 
      mMaxRayLength(sceneSize),
      mPixelBuffer(width, height, pixelSamplesSqrt * pixelSamplesSqrt)
{
    // Initialize the parameters pointer (PathVisualizerManager manages these)
    mParams = params;

    // Reserve enough space for all pixels
    int pixelSamples = pixelSamplesSqrt * pixelSamplesSqrt;

    // Reserve a minimum amount of space for the nodes and vertices
    mNodes.reserve(width * height * pixelSamples);
    mVertexBuffer.reserve(width * height * pixelSamples);
}

PathVisualizer::~PathVisualizer() {}

// -------------------------------------------- BUILDING ---------------------------------------------------------------

bool PathVisualizer::setUpFrustum(const Camera& cam)
{
    if (!cam.hasFrustum()) { return false; }

    mFrustum = std::make_unique<mcrt_common::Frustum>();
    cam.computeFrustum(mFrustum.get(), 0, false);  // frustum at shutter open
    // Our points are in world space, so transform the clipping planes
    mFrustum->transformClipPlanes(cam.getCamera2World());

    return true;
}

void PathVisualizer::recordOcclusionRay(const mcrt_common::Ray& ray, const Scene& scene, int pixel, int spIndex,
                                        bool lightSampleFlag, bool occlusionFlag)
{
    recordRay(ray, scene, pixel, spIndex, /* lobeType */ 0, lightSampleFlag, occlusionFlag);
}

void PathVisualizer::recordRegularRay(const mcrt_common::Ray& ray, const Scene& scene, int pixel, 
                                      int spIndex, int lobeType)
{
    recordRay(ray, scene, pixel, spIndex, lobeType, /*lightSampleFlag*/ false, /*occlusionFlag*/ false);
}

void PathVisualizer::recordRay(const mcrt_common::Ray& ray, const Scene& scene, int pixel, int spIndex,
                               int lobeType, bool lightSampleFlag, bool occlusionFlag)
{
    // If we already generated all the ray info, we don't need to record
    // any more rays, unless the user specifies that we should do it again
    if (!mOn) { return; }

    RenderTimer timer(mInRenderingTime);
    MNRY_ASSERT(mPixelBuffer.mWidth != -1 && mPixelBuffer.mHeight != -1);

    // Calculate the pixel ID
    int pixelID = uint32ToPixelY(pixel) * mPixelBuffer.mWidth + uint32ToPixelX(pixel);

    // Calculate ray endpoints (render space)
    float tfar = std::min(ray.tfar, mMaxRayLength);
    scene_rdl2::math::Vec3f rayOrigin = ray.getOrigin();
    scene_rdl2::math::Vec3f rayEnd = rayOrigin + scene_rdl2::math::normalize(ray.getDirection()) * tfar;

    // Compute world-space coordinates (bc we want these points to stay consistent even if the camera transform changes)
    scene_rdl2::math::Vec3f rayOriginWorld = transformPoint(scene.getRender2World(), rayOrigin);
    scene_rdl2::math::Vec3f rayEndWorld    = transformPoint(scene.getRender2World(), rayEnd);

    // Add vertex to our vertex list (or find it, if it already exists) and return the index
    int rayOriginIndex  = addVertex(rayOriginWorld);
    int rayEndIndex     = addVertex(rayEndWorld);

    // Set the flags
    Flags flags;
    bool diffuseFlag  = lobeType & shading::BsdfLobe::DIFFUSE;
    bool specularFlag = (lobeType & shading::BsdfLobe::GLOSSY) | (lobeType & shading::BsdfLobe::MIRROR);
    setFlags(flags, diffuseFlag, specularFlag, lightSampleFlag);

    int rayIsectIndex = -1;
    if (occlusionFlag) { 
        // If a shadow ray is occluded, we want to find the first object it intersects
        scene_rdl2::math::Vec3f isectPt = findSceneIsect(ray, scene);
        rayIsectIndex = addVertex(isectPt);
    }
    addNode(pixelID, rayOriginIndex, rayEndIndex, rayIsectIndex, ray.getDepth(), spIndex, flags);
}

// ---------------------------------------------- FILTERING ------------------------------------------------------------

void PathVisualizer::filter(std::vector<int>& filteredNodes) const
{
    if (mNodes.size() == 0) {
        return;
    }

    RenderTimer timer(mPostRenderingTime);

    /// TODO: Change to pixel range
    for (int px = mParams->mMinPixelX; px <= mParams->mMinPixelX; ++px) {
        for (int py = mParams->mMinPixelY; py <= mParams->mMinPixelY; ++py) {
            // Each pixel contains a collection of subpixel paths
            const Pixel& pixel = mPixelBuffer.getPixelAt(px, py);

            for (const SubpixelPath& path : pixel) {
                /// TODO: check if subpixel index matches user parameter
                // Each subpixel path is a collection of node indices
                for (int nodeIndex : path) {
                    MNRY_ASSERT(nodeIndex >= 0 && nodeIndex < mNodes.size());

                    /// Show ray if less than specified max depth
                    bool showRay = getRayDepth(nodeIndex) <= mParams->mMaxDepth;
                    /// Show ray if it matches the flags
                    showRay = showRay && matchesFlags(nodeIndex);

                    // If ray matches all the user parameters, add to list
                    if (showRay) {
                        filteredNodes.push_back(nodeIndex);
                    }
                }
            }
        }
    }
}

// -------------------------------------- DRAWING ----------------------------------------------------------------------

bool PathVisualizer::pixelIsOccluded(int px, int py, const scene_rdl2::math::Vec2f& p1, const pbr::Scene* scene, 
                                     float totalDistance, float invDepth1, float invDepthDiff) const
{
    // Find t value in pixel space
    float distanceToPixel = scene_rdl2::math::distance(p1, scene_rdl2::math::Vec2f(px, py));
    float t = distanceToPixel / totalDistance;

    // Find depth of this point along the line using
    // rasterization-based depth interpolation
    float depth = 1.f / (invDepth1 + t*invDepthDiff);

    /// TODO: There should be a way to do this more efficiently using an occlusion ray,
    /// by setting tfar to the depth above; however, this depth is in camera space, and
    /// the occlusion test happens in render space, so the depth somehow needs to be in render space

    // Cast ray to find the first scene intersection
    mcrt_common::RayDifferential ray;
    scene->getCamera()->createRay(&ray, px + 0.5f, py + 0.5f, 0.f, 0.5f, 0.5f, false);
    scene->getEmbreeAccelerator()->intersect(ray); 

    if (isinf(ray.tfar)) {
        // if there is no scene intersection, then the pixel is not occluded
        return false;
    } else {
        // if there IS a scene intersection, find its depth and compare to the ray visualization's depth
        scene_rdl2::math::Vec3f isect = ray.getOrigin() + ray.getDirection() * ray.tfar;
        scene_rdl2::math::Vec4f isect4d(isect.x, isect.y, isect.z, 1.f);
        float sceneDepth = -(float) dot(isect4d, scene->getCamera()->getRender2Camera().col2());

        return sceneDepth < depth;
    }
}

uint8_t PathVisualizer::clipPoints(int nodeIndex, scene_rdl2::math::Vec3f* outPoints) const
{
    uint8_t numPoints = 0;

    // world-space starting point
    outPoints[numPoints++] = getRayOrigin(nodeIndex);

    // If the ray is an occlusion ray that's failed the occlusion test,
    // we found the world-space closest intersection in recordRay()
    // We now need to convert it to screen space for drawing.
    bool hasIsect = getNode(nodeIndex).mRayIsectIndex != -1;
    if (hasIsect) { 
        outPoints[numPoints++] = getRayIsect(nodeIndex) ; 
    }

    // world-space ending point
    outPoints[numPoints++] = getRayEndpoint(nodeIndex);

    // ----- Clip world-space points to viewing frustum -----
    auto clipLine = [&] (scene_rdl2::math::Vec3f& p1, scene_rdl2::math::Vec3f& p2) {
        scene_rdl2::math::Vec3f raySegmentClipped[2];
        bool lineClipped = mFrustum->clipLine(p1, p2, raySegmentClipped[0], raySegmentClipped[1]);
        if (!lineClipped) {
            return false;
        }
        p1 = raySegmentClipped[0];
        p2 = raySegmentClipped[1];
        return true;
    };

    // ----- Clip the line(s) -----
    bool clippedLine = false;
    for (int i = 0; i < numPoints - 1; ++i) {
        bool clippedCurrentLine = clipLine(outPoints[i], outPoints[i+1]);
        clippedLine = clippedLine || clippedCurrentLine;
    }
   
    return clippedLine ? numPoints : 0;
}

void PathVisualizer::drawLine(const std::function<void(int, int, scene_rdl2::math::Color&, float)>& writeToRenderBuffer, 
                              int nodeIndex, const pbr::Scene* scene) const
{
    MNRY_ASSERT(nodeIndex >= 0 && nodeIndex < mNodes.size());
    RenderTimer timer(mPostRenderingTime);
    const pbr::Camera* cam = scene->getCamera();

    /// ---- Save the position of the pixel focus ----------------------
    if (isCameraRay(nodeIndex) && mCameraIsectIndex == -1) {
        mCameraIsectIndex = getNode(nodeIndex).mRayEndpointIndex;
    }

    /// ---- Clip the world-space points using the viewing frustum ------
    // Can have 2-3 points per line:
    //  - rayOrigin
    //  - rayEndpoint
    //  - scene intersection (if occlusion ray)
    scene_rdl2::math::Vec3f clippedPoints[3];
    uint8_t numPoints = clipPoints(nodeIndex, clippedPoints);
    if (numPoints == 0) { return; }

    /// ---- Transform the points into screen-space ---------------------
    scene_rdl2::math::Vec2f pixelEndpoints[3];
    float inverseDepths[3];

    for (int i = 0; i < numPoints; ++i) {
        const scene_rdl2::math::Vec3f& clippedPoint = clippedPoints[i];

        // Transform the point to screen-space
        scene_rdl2::math::Vec2f p = transformPointWorld2Screen(clippedPoint, cam);
        pixelEndpoints[i] = scene_rdl2::math::Vec2f(std::round(p.x), std::round(p.y));

        // Calculate the inverse depth
        /// NOTE: this assumes we're using a Perspective camera
        scene_rdl2::math::Vec4d clippedPoint4d(clippedPoint.x, clippedPoint.y, clippedPoint.z, 1.f);
        float cpz = -(float) dot(clippedPoint4d, cam->getWorld2Camera().col2());
        inverseDepths[i] = 1.f / cpz;
    }

    // ---- Draw line(s) ----------------------------------------------
    for (int i = 0; i < numPoints - 1; ++i) {

        float inverseDepthDiff = inverseDepths[i+1] - inverseDepths[i];
        float distance = scene_rdl2::math::distance(pixelEndpoints[i], pixelEndpoints[i+1]);

        bool isOccluded = i > 0;
        scene_rdl2::math::Color rayColor = getRayColor(isOccluded ? -1 : nodeIndex);

        auto writeFunc = [&] (int px, int py, float a) {
            if (pixelIsOccluded(px, py, pixelEndpoints[i], scene, distance, inverseDepths[i], inverseDepthDiff)) {
                a *= 0.1f;
            }
            writeToRenderBuffer(px, py, rayColor, a);
        };

        drawing::drawLineWu(pixelEndpoints[i].x, pixelEndpoints[i].y, 
                            pixelEndpoints[i+1].x, pixelEndpoints[i+1].y,
                            mParams->mLineWidth, writeFunc);
    }
}

void PathVisualizer::drawPixelFocus(
    const std::function<void(int, int, scene_rdl2::math::Color&, float)>& writeToRenderBuffer,
    const pbr::Camera* cam) const
{
    RenderTimer timer(mPostRenderingTime);

    // If we haven't recorded a position for the pixel focus, don't draw
    if (mCameraIsectIndex == -1) { return; }

    /// ------ Get the world-space scene intersection and transform into screen space ---------------
    scene_rdl2::math::Vec2f p = transformPointWorld2Screen(getVert(mCameraIsectIndex), cam);

    /// ------ Draw a square around the chosen pixel ----------------------
    p.x = std::round(p.x);
    p.y = std::round(p.y);

    int width = 10;  /* width of square drawn around the chosen pixel */
    float alpha = 0.8f;
    scene_rdl2::math::Color color(0.475f, 0.84f, 1.f);

    auto writeFunc = [&] (int x, int y, float a) { writeToRenderBuffer(x, y, color, a * alpha); };
    drawing::drawSquare(p.x, p.y, width, writeFunc, false, mParams->mLineWidth);
}

void PathVisualizer::draw(scene_rdl2::fb_util::RenderBuffer* renderBuffer, const Scene* scene)
{
    /// ---- Set up the camera frustum -------------------------------------
    if (!setUpFrustum(*scene->getCamera())) {
        std::cout << "Camera must have a valid frustum\n";
        return;
    }

    /// ---- Filter Nodes based on given debug parameters ----
    std::vector<int> nodeIndices;
    filter(nodeIndices);

    if (nodeIndices.size() == 0) {
        return; 
    }

    /// ---- Create a function that will write to the render buffer --------
    auto writeToRenderBuffer = [&] (int px, int py, scene_rdl2::math::Color& color, float a) {
        if (!isInBounds(px, py)) { return; }
        a = scene_rdl2::math::clamp(a, 0.f, 1.f);

        auto& renderColor = renderBuffer->getPixel(px, py);
        renderColor.x = a * color.r + (1.f - a) * renderColor.x;
        renderColor.y = a * color.g + (1.f - a) * renderColor.y;
        renderColor.z = a * color.b + (1.f - a) * renderColor.z;
        renderColor.w = 1;
    };

    resetCameraIsectIndex();

    /// ---- For each node, draw a line on the render buffer ---------------
    for (int nodeIndex : nodeIndices) {
        drawLine(writeToRenderBuffer, nodeIndex, scene);
    }

    drawPixelFocus(writeToRenderBuffer, scene->getCamera());
}

/// --------------------------------------------- Utilities ------------------------------------------------------------

scene_rdl2::math::Vec3f PathVisualizer::findSceneIsect(const mcrt_common::Ray& ray, const Scene& scene) const
{
    mcrt_common::Ray testRay(ray.getOrigin(), ray.getDirection());
    scene.getEmbreeAccelerator()->intersect(testRay); 
    MNRY_ASSERT(testRay.geomID != RTC_INVALID_GEOMETRY_ID);

    // Compute world-space point
    scene_rdl2::math::Vec3f isectPt = testRay.getOrigin() + testRay.tfar * testRay.getDirection();
    return transformPoint(scene.getRender2World(), isectPt);
}

scene_rdl2::math::Vec2f 
PathVisualizer::transformPointWorld2Screen(const scene_rdl2::math::Vec3f& p, const pbr::Camera* cam) const
{
    scene_rdl2::math::Vec4d camP = transformPoint(cam->getWorld2Camera(), p);
    scene_rdl2::math::Vec3f result = mFrustum->projectToViewport(scene_rdl2::math::Vec3f(camP.x, camP.y, camP.z));
    return scene_rdl2::math::Vec2f(result.x, result.y);
}

/// -------- Getters ---------

inline bool PathVisualizer::matchesFlag(int nodeIndex, const Flags& flag) const 
{
    return static_cast<uint8_t>(getNode(nodeIndex).mFlags) & static_cast<uint8_t>(flag);
}

inline bool PathVisualizer::matchesFlags(int nodeIndex) const
{
    bool matches = true;
    if (matchesFlag(nodeIndex, Flags::SPECULAR)) {
        matches = matches && mParams->mSpecularRaysOn;
    } else if (matchesFlag(nodeIndex, Flags::DIFFUSE)) {
        matches = matches && mParams->mDiffuseRaysOn;
    } else if (matchesFlag(nodeIndex, Flags::LIGHT_SAMPLE)) {
        matches = matches && mParams->mLightSamplesOn && mParams->mOcclusionRaysOn;
    } else {
        if (!isCameraRay(nodeIndex)) {
            matches = matches && mParams->mBsdfSamplesOn && mParams->mOcclusionRaysOn;
        }
    }
    return matches;
}

inline int PathVisualizer::getRayDepth(int nodeIndex) const
{
    return getNode(nodeIndex).mDepth;
}

inline bool PathVisualizer::isCameraRay(int nodeIndex) const
{
    return getRayDepth(nodeIndex) == 0;
}

inline scene_rdl2::math::Vec3f PathVisualizer::getRayOrigin(int nodeIndex) const
{
    int rayOriginIndex = getNode(nodeIndex).mRayOriginIndex;

    MNRY_ASSERT(rayOriginIndex >= 0 && rayOriginIndex < mVertexBuffer.size());
    return getVert(rayOriginIndex);
}

inline scene_rdl2::math::Vec3f PathVisualizer::getRayEndpoint(int nodeIndex) const
{
    int rayEndpointIndex = getNode(nodeIndex).mRayEndpointIndex;

    MNRY_ASSERT(rayEndpointIndex >= 0 && rayEndpointIndex < mVertexBuffer.size());
    return getVert(rayEndpointIndex);
}

inline scene_rdl2::math::Vec3f PathVisualizer::getRayIsect(int nodeIndex) const
{
    int rayIsectIndex = getNode(nodeIndex).mRayIsectIndex;

    if (rayIsectIndex == -1) { return scene_rdl2::math::Vec3f(0.f); }

    MNRY_ASSERT(rayIsectIndex >= 0 && rayIsectIndex < mVertexBuffer.size());
    return getVert(rayIsectIndex);
}

inline scene_rdl2::math::Color PathVisualizer::getRayColor(int nodeIndex) const
{
    // if nodeIndex == -1, this is the occluded part of the ray
    if (nodeIndex == -1) {
        return scene_rdl2::math::Color(0, 0, 0);
    }
    if (isCameraRay(nodeIndex) && !matchesFlag(nodeIndex, Flags::LIGHT_SAMPLE)) {
        return mParams->mCameraRayColor;
    }
    if (matchesFlag(nodeIndex, Flags::DIFFUSE)) {
        return mParams->mDiffuseRayColor;
    } else if (matchesFlag(nodeIndex, Flags::SPECULAR)) {
        return mParams->mSpecularRayColor;
    } else if (matchesFlag(nodeIndex, Flags::LIGHT_SAMPLE)) {
        return mParams->mLightSampleColor;
    } else {
        return mParams->mBsdfSampleColor;
    }

    return scene_rdl2::math::Color(1, 1, 1);
}

/// -------- Setters --------

inline void PathVisualizer::setFlag(Flags& flags, const Flags& flag) const

{
    flags = static_cast<Flags>(static_cast<uint8_t>(flags) | static_cast<uint8_t>(flag));
}

inline void PathVisualizer::setFlags(Flags& flags, bool isDiffuse, bool isSpecular, 
                                     bool isLightSample) const
{
    flags = Flags::NONE;
    if (isDiffuse) { 
        setFlag(flags, Flags::DIFFUSE); 
    } 
    else if (isSpecular) { 
        setFlag(flags, Flags::SPECULAR); 
    }
    else if (isLightSample) {
        setFlag(flags, Flags::LIGHT_SAMPLE);
    }
    else { 
        setFlag(flags, Flags::BSDF_SAMPLE);
    }
}

inline int PathVisualizer::addVertex(const scene_rdl2::math::Vec3f& v)
{
    std::scoped_lock<std::mutex> lock(mVertexBufferLock);
    // Check the most recent vertex added. If it's the same, don't add again
    if (mVertexBuffer.size() == 0 || !isEqual(v, mVertexBuffer.back())) {
        // If the search fails, add the vertex
        mVertexBuffer.push_back(v);
    }
    // Return the index of the vert position
    return mVertexBuffer.size() - 1;
}

inline void PathVisualizer::addNode(int pixelID, int originIndex, int endpointIndex, int isectIndex, 
                                    int depth, int sp, Flags& flags)
{
    std::scoped_lock<std::mutex> lock(mNodesLock);
    MNRY_ASSERT(originIndex >= 0 && originIndex < mVertexBuffer.size());
    MNRY_ASSERT(endpointIndex >= 0 && endpointIndex < mVertexBuffer.size());
    MNRY_ASSERT(isectIndex == -1 || (isectIndex >= 0 && isectIndex < mVertexBuffer.size()));

    mNodes.emplace_back(originIndex, endpointIndex, isectIndex, depth, flags);

    // Add node to pixel data structure for easier finding later
    mPixelBuffer.addNode(pixelID, sp, mNodes.size() - 1);
}

/// -------- Statistics helpers --------

int PathVisualizer::getMemoryFootprint() const
{
    int nodesSize = sizeof(mNodes) + sizeof(Node) * mNodes.size();
    int vertsSize = sizeof(mVertexBuffer) + sizeof(scene_rdl2::math::Vec3f) * mVertexBuffer.size();
    int pixelsSize = mPixelBuffer.getSize();

    int totalSize = nodesSize                           + /* mNodes */
                    vertsSize                           + /* mVertexBuffer */
                    pixelsSize                          + /* mPixelBuffer */
                    sizeof(PathVisualizerParams*)       + /* mParams */
                    sizeof(bool)                        + /* mOn */
                    sizeof(int)                         + /* mCameraIsectIndex */
                    sizeof(mcrt_common::Frustum)        + /* mFrustum */
                    sizeof(float)                       + /* mMaxRayLength */
                    sizeof(std::mutex) * 3              + /* mLock, mNodesLock, mVertexBufferLock */
                    sizeof(moonray::util::AverageDouble)+ /* mInRenderingTime */
                    sizeof(moonray::util::AverageDouble)  /* mPostRenderingTime */
                    ;

    return totalSize;
}

void PathVisualizer::printTimeStats() const
{
    double inRenderTime = mInRenderingTime.getSum() / mcrt_common::getNumTBBThreads();
    double postRenderTime = mPostRenderingTime.getSum() / mcrt_common::getNumTBBThreads();
    std::cout << "In Rendering Time / # threads: " << inRenderTime << " ms\n";
    std::cout << "Post-Rendering Time / # threads: " << postRenderTime << " ms\n";
    std::cout << "Combined Time: " << (inRenderTime + postRenderTime) << " ms\n";
}

void PathVisualizer::printNodes(std::vector<int>& filteredList)
{
    std::cout << "-------- Printing out nodes ---------\n";
    /// TODO: change this pointer list to an index list
    for (int nodeIndex : filteredList) {
        const Node& node = getNode(nodeIndex);
        
        std::cout << "{ depth: " << node.mDepth;
        std::cout << ", rayOrigin: " << getRayOrigin(nodeIndex);
        std::cout << ", rayEndpoint: " << getRayEndpoint(nodeIndex);
        std::cout << "}\n";
    }
    std::cout << "\n";
}

void PathVisualizer::printNodes(int maxEntries = -1) const
{
    std::cout << "-------- Printing out nodes ---------\n";
    for (int i = 0; i < mNodes.size(); ++i) {
        const Node& node = mNodes[i];

        if (maxEntries != -1 && i >= maxEntries) { return; }

        std::cout << "{ depth: " << node.mDepth;
        std::cout << ", rayOrigin: " << getRayOrigin(i);
        std::cout << ", rayEndpoint: " << getRayEndpoint(i);
        std::cout << "}\n";
    }
    std::cout << "\n";
}

} // end namespace rndr
} // end namespace moonray