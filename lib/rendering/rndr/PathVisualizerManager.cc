// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "PathVisualizerManager.h"

#include <scene_rdl2/scene/rdl2/rdl2.h>
#include <scene_rdl2/common/math/Color.h>
#include <moonray/rendering/pbr/core/PathVisualizer.h>
#include <moonray/rendering/pbr/core/Scene.h>
#include <moonray/rendering/rt/EmbreeAccelerator.h>

namespace moonray {
namespace rndr {

PathVisualizerManager::PathVisualizerManager() 
    : mPathVisualizer(nullptr), mTriggered(false), mScene(nullptr) 
{
    mParams = std::make_unique<pbr::PathVisualizerParams>();
}

PathVisualizerManager::~PathVisualizerManager()
{
    delete mPathVisualizer;
}

void PathVisualizerManager::initialize(const scene_rdl2::rdl2::SceneVariables& vars, pbr::Scene* scene)
{
    // If not triggered, the user hasn't specified we need to create a new PathVisualizer
    if (!mTriggered) { return; }

    float sceneSize = scene_rdl2::math::length(scene->getEmbreeAccelerator()->getBounds().size());

    mPathVisualizer = new pbr::PathVisualizer(vars.getRezedWidth(), vars.getRezedHeight(), 
                                              vars.get(scene_rdl2::rdl2::SceneVariables::sPixelSamplesSqrt),
                                              mParams.get(), sceneSize);
    mTriggered = false; // we created the PathVisualizer, like the user asked
    mScene = scene;
    mScene->setPathVisualizer(mPathVisualizer);
}

void PathVisualizerManager::draw(scene_rdl2::fb_util::RenderBuffer* renderBuffer)
{
    if (!pathVisualizerExists() || !mScene) { return; }
    mPathVisualizer->draw(renderBuffer, mScene);
}

void PathVisualizerManager::turnOff()
{
    if (!pathVisualizerExists()) { return; }
    mPathVisualizer->turnOff();
}

bool PathVisualizerManager::isPathVisualizerOn() const { return pathVisualizerExists() && mPathVisualizer->isOn(); }

bool PathVisualizerManager::pathVisualizerExists() const { return mPathVisualizer != nullptr; }

/// ------------------------- UI setters --------------------------------- //

void PathVisualizerManager::setPixelX(int px) { mParams->mMinPixelX = px; }
void PathVisualizerManager::setPixelY(int py) { mParams->mMinPixelY = py; }
void PathVisualizerManager::setMaxDepth(int depth) { mParams->mMaxDepth = depth; }

void PathVisualizerManager::setOcclusionRaysFlag(bool flag) { mParams->mOcclusionRaysOn = flag; }
void PathVisualizerManager::setSpecularRaysFlag(bool flag) { mParams->mSpecularRaysOn = flag; }
void PathVisualizerManager::setDiffuseRaysFlag(bool flag) { mParams->mDiffuseRaysOn = flag; }
void PathVisualizerManager::setBsdfSamplesFlag(bool flag) { mParams->mBsdfSamplesOn = flag; }
void PathVisualizerManager::setLightSamplesFlag(bool flag) { mParams->mLightSamplesOn = flag; }

void PathVisualizerManager::setCameraRayColor(scene_rdl2::math::Color color) { mParams->mCameraRayColor = color; }
void PathVisualizerManager::setSpecularRayColor(scene_rdl2::math::Color color) {mParams->mSpecularRayColor = color; }
void PathVisualizerManager::setDiffuseRayColor(scene_rdl2::math::Color color) { mParams->mDiffuseRayColor = color; }
void PathVisualizerManager::setBsdfSampleColor(scene_rdl2::math::Color color) { mParams->mBsdfSampleColor = color; }
void PathVisualizerManager::setLightSampleColor(scene_rdl2::math::Color color) { mParams->mLightSampleColor = color; }

void PathVisualizerManager::setLineWidth(int value) { mParams->mLineWidth = value; }

} // end namespace rndr
} // end namespace moonray