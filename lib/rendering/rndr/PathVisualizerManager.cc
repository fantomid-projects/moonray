// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "PathVisualizerManager.h"

#include <scene_rdl2/scene/rdl2/rdl2.h>
#include <scene_rdl2/common/math/Color.h>
#include <moonray/rendering/pbr/core/PathVisualizer.h>
#include <moonray/rendering/pbr/core/Scene.h>
#include <moonray/rendering/rt/EmbreeAccelerator.h>
#include <moonray/rendering/rndr/RenderContext.h>

namespace moonray {
namespace rndr {

PathVisualizerManager::PathVisualizerManager(RenderContext* renderContext) 
    : mPathVisualizer(), mScene(nullptr), mRenderContext(renderContext)
{
    mParams = std::make_unique<pbr::PathVisualizerParams>();
    mPathVisualizer = std::make_unique<pbr::PathVisualizer>();
}

PathVisualizerManager::~PathVisualizerManager() {}

void PathVisualizerManager::initialize(const scene_rdl2::rdl2::SceneVariables& vars, pbr::Scene* scene)
{
    // Only create the path visualizer once
    /// TODO: is there a place we can call initialize that will only be called once? Outside of startFrame?
    if (!isOff()) { return; }

    float sceneSize = scene_rdl2::math::length(scene->getEmbreeAccelerator()->getBounds().size());

    mPathVisualizer->initialize(vars.getRezedWidth(), vars.getRezedHeight(), mParams.get(), sceneSize);
    
    mScene = scene;
    mScene->setPathVisualizer(mPathVisualizer.get());
}

/// ----------------------------------------------------------------------

void PathVisualizerManager::startSimulation()
{
    // if visualizer hasn't been initialized yet,
    // we can't run the simulation
    if (isOff()) { return; }

    mPathVisualizer->setState(pbr::State::RECORD);
    if (mRenderContext->isFrameRendering()) {
        mPathVisualizer->setNeedsRenderRefresh(true);
    }
    mRenderContext->runSimulation();
}

void PathVisualizerManager::stopSimulation()
{
    MNRY_ASSERT(isInRecordState());
    mPathVisualizer->setState(pbr::State::STOP_RECORD);
}

void PathVisualizerManager::requestDraw()
{
    MNRY_ASSERT(isInStopRecordState());
    mPathVisualizer->setState(pbr::State::REQUEST_DRAW);
}

void PathVisualizerManager::startDraw()
{
    MNRY_ASSERT(isDrawRequested());
    mPathVisualizer->setState(pbr::State::DRAW);
}

void PathVisualizerManager::draw(scene_rdl2::fb_util::RenderBuffer* renderBuffer)
{
    MNRY_ASSERT(isInDrawState());
    mPathVisualizer->draw(renderBuffer, mScene);
}

void PathVisualizerManager::setNeedsRenderRefresh(bool refresh)
{
    mPathVisualizer->setNeedsRenderRefresh(refresh);
}

void PathVisualizerManager::reset()
{
    MNRY_ASSERT(!isOff());
    mPathVisualizer->reset();
}

/// ----------------------------- Getters ------------------------------------

bool PathVisualizerManager::isOff() const
{
    return mPathVisualizer->getState() == pbr::State::OFF;
}

bool PathVisualizerManager::isInReadyState() const
{
    return mPathVisualizer->getState() == pbr::State::READY;
}

bool PathVisualizerManager::isInRecordState() const
{
    return mPathVisualizer->getState() == pbr::State::RECORD;
}

bool PathVisualizerManager::isInStopRecordState() const
{
    return mPathVisualizer->getState() == pbr::State::STOP_RECORD;
}

bool PathVisualizerManager::isDrawRequested() const
{
    return mPathVisualizer->getState() == pbr::State::REQUEST_DRAW;
}

bool PathVisualizerManager::isInDrawState() const
{
    return mPathVisualizer->getState() == pbr::State::DRAW;
}

bool PathVisualizerManager::getNeedsRenderRefresh() const
{
    return mPathVisualizer->getNeedsRenderRefresh();
}

scene_rdl2::math::Vec2i PathVisualizerManager::getPixel() const
{
    return scene_rdl2::math::Vec2i(mParams->mPixelX, mParams->mPixelY);
}

/// ------------------------- UI setters --------------------------------- //

void PathVisualizerManager::setPixelX(int px) { mParams->mPixelX = px; }
void PathVisualizerManager::setPixelY(int py) { mParams->mPixelY = py; }
void PathVisualizerManager::setMaxDepth(int depth) { mParams->mMaxDepth = depth; }

void PathVisualizerManager::fillPixelSamples(unsigned int& samples) const
{
    if (!mParams->mUseSceneSamples) {
        samples = mParams->mPixelSamples;
    }
}

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

void PathVisualizerManager::setUseSceneSamples(int useSceneSamples) { mParams->mUseSceneSamples = useSceneSamples; }
void PathVisualizerManager::setPixelSamples(int samples) { mParams->mPixelSamples = samples * samples; }
void PathVisualizerManager::setLightSamples(int samples) { mParams->mLightSamples = samples * samples; }
void PathVisualizerManager::setBsdfSamples(int samples)  { mParams->mBsdfSamples = samples * samples; }

} // end namespace rndr
} // end namespace moonray