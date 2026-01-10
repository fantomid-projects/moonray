// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "PathVisualizerManager.h"

#include <scene_rdl2/common/grid_util/PathVisSimGlobalInfo.h>
#include <scene_rdl2/common/math/Color.h>
#include <scene_rdl2/scene/rdl2/rdl2.h>
#include <moonray/rendering/pbr/core/PathVisualizer.h>
#include <moonray/rendering/pbr/core/Scene.h>
#include <moonray/rendering/rt/EmbreeAccelerator.h>
#include <moonray/rendering/rndr/RenderContext.h>

namespace moonray {
namespace rndr {

PathVisualizerManager::PathVisualizerManager(RenderContext* renderContext) 
    : mPathVisualizer(), 
      mScene(nullptr), 
      mRenderContext(renderContext), 
      mOn(false),
      mInitialCameraXform(), 
      mCachedCameraXform(), 
      mCameraXformWasCached(false)
{
    mParams = std::make_unique<pbr::PathVisualizerParams>();
    mPathVisualizer = std::make_unique<pbr::PathVisualizer>();

    parserConfigure();
}

PathVisualizerManager::~PathVisualizerManager() {}

void PathVisualizerManager::initialize(const scene_rdl2::rdl2::SceneVariables& vars, pbr::Scene* scene)
{
    // Only create the path visualizer once
    if (!isInNoneState()) { return; }

    const float sceneSize = scene_rdl2::math::length(scene->getEmbreeAccelerator()->getBounds().size());

    const unsigned width = vars.getRezedWidth();
    const unsigned height = vars.getRezedHeight();
    pbr::PathVisualizerParams* params = mParams.get();
    params->setPixelRange(0, 0, width - 1, height - 1);
    mPathVisualizer->initialize(width, height, params, sceneSize);
    
    mScene = scene;
    mScene->setPathVisualizer(mPathVisualizer.get());
}

/// ----------------------------------------------------------------------

void PathVisualizerManager::startSimulation()
{
    // PathVisualizer is in NONE state if it hasn't been initialized,
    /// and OFF if it has been turned off by the user
    if (isInNoneState() || !isOn()) { return; }

    mSimulationRecTime.start();

    mPathVisualizer->setState(pbr::State::START_RECORD);
    mPathVisualizer->reset();
}

void PathVisualizerManager::setRecordState()
{
    MNRY_ASSERT(isInStartRecordState());
    mPathVisualizer->setState(pbr::State::RECORD);    
}

void PathVisualizerManager::stopSimulation()
{
    MNRY_ASSERT(isInRecordState());
    mPathVisualizer->setState(pbr::State::STOP_RECORD);

    mSimulationTime = mSimulationRecTime.end();
}

void PathVisualizerManager::requestDraw()
{
    MNRY_ASSERT(isInStopRecordState());
    mPathVisualizer->setState(pbr::State::REQUEST_DRAW);
}

void PathVisualizerManager::generateLines()
{
    if (!isOn()) { return; }

    scene_rdl2::rec_time::RecTime timer;
    timer.start();
    mPathVisualizer->setState(pbr::State::GENERATE_LINES);
    mPathVisualizer->generateLines(mScene);
    mPathVisualizer->setState(pbr::State::DRAW);
    mGenerateLinesTime = timer.end();
}

void PathVisualizerManager::draw(scene_rdl2::fb_util::RenderBuffer* renderBuffer)
{
    if (!isOn()) { return; }

    MNRY_ASSERT(isInDrawState());
    mPathVisualizer->draw(renderBuffer, mScene);
}

void PathVisualizerManager::printStats() const
{
    if (getTotalLines() == 0) { return; }

    const size_t lightSampleRayCount = mPathVisualizer->getLightSampleRayCount();
    const size_t bsdfSampleRayCount = mPathVisualizer->getBsdfSampleRayCount();
    const size_t diffuseRayCount = mPathVisualizer->getDiffuseRayCount();
    const size_t specularRayCount = mPathVisualizer->getSpecularRayCount();
    const size_t occlRayCount = lightSampleRayCount + bsdfSampleRayCount;
    const size_t total = occlRayCount + diffuseRayCount + specularRayCount;

    std::cout << "\n\n=====================================\n";
    std::cout <<   "===     Path Visualizer Stats     ===\n";
    std::cout <<   "=====================================\n";
    std::cout << "Total # rays: " << total << std::endl;
    std::cout << "Total occlusion rays: " << occlRayCount << std::endl;
    std::cout << "Total light sample rays: " << lightSampleRayCount << std::endl;
    std::cout << "Total bsdf sample rays: " << bsdfSampleRayCount << std::endl;
    std::cout << "Total diffuse rays: " << diffuseRayCount << std::endl;
    std::cout << "Total specular rays: " << specularRayCount << std::endl;
    std::cout << "\n";
    std::cout << "Simulation time (s): " << mSimulationTime << std::endl;
    std::cout << "Generate lines time (s): " << mGenerateLinesTime << std::endl;
    std::cout << "Avg time per line (ms): " << (mGenerateLinesTime / getTotalLines() * 1000) << std::endl;
    std::cout << "=====================================\n\n";
}

void
PathVisualizerManager::crawlAllLines(const CrawlLineFunc& func)
{
    if (!mPathVisualizer) return;
    mPathVisualizer->crawlAllLines(func);
}

size_t
PathVisualizerManager::getTotalLines() const
{
    return mPathVisualizer->getTotalLines();
}

// static function
scene_rdl2::grid_util::VectorPacketLineStatus::RayType
PathVisualizerManager::flagsToRayType(const uint8_t& flags)
{
    return pbr::PathVisualizer::flagsToRayType(flags);
}

scene_rdl2::math::Color
PathVisualizerManager::getColorByFlags(const uint8_t& flags) const
{
    return mPathVisualizer->getColorByFlags(flags);
}

void PathVisualizerManager::setNeedsRenderRefresh(const bool refresh)
{
    mPathVisualizer->setNeedsRenderRefresh(refresh);
}

void PathVisualizerManager::reset()
{
    MNRY_ASSERT(!isInNoneState());
    mPathVisualizer->reset();
}

void PathVisualizerManager::fillPixelSamples(int& samples) const
{
    if (!mParams->mUseSceneSamples) {
        samples = mParams->mPixelSamples;
    }
}

void PathVisualizerManager::fillLightSamples(int& samples) const
{
    if (!mParams->mUseSceneSamples) {
        samples = mParams->mLightSamples;
    }
}

void PathVisualizerManager::fillBsdfSamples(int& samples) const
{
    if (!mParams->mUseSceneSamples) {
        samples = mParams->mBsdfSamples;
    }
}

void PathVisualizerManager::fillMaxDepth(int& samples) const
{
    samples = mParams->mMaxDepth;
}

void PathVisualizerManager::turnOn()
{
    mOn = true;

    mPathVisualizer->setOn(mOn);
    mParams->setOn(mOn);

    startSimulation();
}
void PathVisualizerManager::turnOff()
{
    mOn = false;

    mPathVisualizer->setOn(mOn);
    mParams->setOn(mOn);

    //
    // This is for the vecPacket communication, more precise and sync.
    // Especially for the timing of PathVisualizer turning off to on,
    // there is some garbage data is still left before generating updated lines.
    // vecPacket might pick garbage data and send it to the downstream.
    // We must clean up old lines when the path visualizer is off.
    //
    mPathVisualizer->resetLines();
}

void PathVisualizerManager::setInitialCameraXform(const scene_rdl2::math::Mat4d& xform)
{
    mInitialCameraXform = xform;
}

void PathVisualizerManager::setCachedCameraXform(const scene_rdl2::math::Mat4d& xform)
{
    mCachedCameraXform = xform;
    mCameraXformWasCached = true;
}

void PathVisualizerManager::setCameraXformWasCached(const bool cached)
{
    mCameraXformWasCached = cached;
}

/// ----------------------------- Getters ------------------------------------

bool PathVisualizerManager::isOn() const
{
    return mOn;
}

bool PathVisualizerManager::isInNoneState() const
{
    return mPathVisualizer->getState() == pbr::State::NONE;
}

bool PathVisualizerManager::isInReadyState() const
{
    return mPathVisualizer->getState() == pbr::State::READY;
}

bool PathVisualizerManager::isInStartRecordState() const
{
    return mPathVisualizer->getState() == pbr::State::START_RECORD;
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

bool PathVisualizerManager::isProcessing() const
{
    return isInStartRecordState() || isInRecordState() || isInStopRecordState();
}

scene_rdl2::math::Vec2i PathVisualizerManager::getPixel() const
{
    return scene_rdl2::math::Vec2i(mParams->mPixelX, mParams->mPixelY);
}

scene_rdl2::math::Mat4d 
PathVisualizerManager::getInitialCameraXform() const
{
    return mInitialCameraXform;
}

scene_rdl2::math::Mat4d 
PathVisualizerManager::getCachedCameraXform() const
{
    return mCachedCameraXform;
}

bool 
PathVisualizerManager::getCameraXformWasCached() const
{
    return mCameraXformWasCached;
}

/// ------------------------- UI getters --------------------------------- //

uint32_t PathVisualizerManager::getPixelX() const { return mParams->mPixelX; }
uint32_t PathVisualizerManager::getPixelY() const { return mParams->mPixelY; }
uint32_t PathVisualizerManager::getMaxDepth() const { return mParams->mMaxDepth; }

bool PathVisualizerManager::showRay(const uint8_t& flag) const
{
    switch (static_cast<pbr::PathVisualizer::Flags>(flag)) {
        case pbr::PathVisualizer::Flags::CAMERA:        return true;
        case pbr::PathVisualizer::Flags::SPECULAR:      return getShowSpecularRays();
        case pbr::PathVisualizer::Flags::DIFFUSE:       return getShowDiffuseRays();
        case pbr::PathVisualizer::Flags::BSDF_SAMPLE:   return getShowBsdfSamples();
        case pbr::PathVisualizer::Flags::LIGHT_SAMPLE:  return getShowLightSamples();
        case pbr::PathVisualizer::Flags::INACTIVE:      return getShowLightSamples();
        default:                                        return false;
    }
}

bool PathVisualizerManager::getShowSpecularRays() const { return mParams->mSpecularRaysOn; }
bool PathVisualizerManager::getShowDiffuseRays() const { return mParams->mDiffuseRaysOn; }
bool PathVisualizerManager::getShowBsdfSamples() const { return mParams->mBsdfSamplesOn; }
bool PathVisualizerManager::getShowLightSamples() const { return mParams->mLightSamplesOn; }

const scene_rdl2::math::Color& PathVisualizerManager::getCameraRayColor() const { return mParams->mCameraRayColor; }
const scene_rdl2::math::Color& PathVisualizerManager::getSpecularRayColor() const { return mParams->mSpecularRayColor; }
const scene_rdl2::math::Color& PathVisualizerManager::getDiffuseRayColor() const { return mParams->mDiffuseRayColor; }
const scene_rdl2::math::Color& PathVisualizerManager::getBsdfSampleColor() const { return mParams->mBsdfSampleColor; }
const scene_rdl2::math::Color& PathVisualizerManager::getLightSampleColor() const { return mParams->mLightSampleColor; }

float PathVisualizerManager::getLineWidth() const { return mParams->mLineWidth; }

bool PathVisualizerManager::getUseSceneSamples() const { return mParams->mUseSceneSamples; }
uint32_t PathVisualizerManager::getPixelSamples() const { return std::sqrt(mParams->mPixelSamples); }
uint32_t PathVisualizerManager::getLightSamples() const { return std::sqrt(mParams->mLightSamples); }
uint32_t PathVisualizerManager::getBsdfSamples() const  { return std::sqrt(mParams->mBsdfSamples); }

/// ------------------------- UI setters --------------------------------- //

void PathVisualizerManager::setPixelX(uint32_t px, bool update)
{
    mParams->mPixelX = px;
    if (update) { startSimulation(); }
}
void PathVisualizerManager::setPixelY(uint32_t py, bool update)
{
    mParams->mPixelY = py;
    if (update) { startSimulation(); }
}
void PathVisualizerManager::setPixel(uint32_t px, uint32_t py, bool update)
{
    mParams->mPixelX = px;
    mParams->mPixelY = py;
    if (update) { startSimulation(); }
}
void PathVisualizerManager::setMaxDepth(int depth, bool update)
{
    mParams->mMaxDepth = depth;
    if (update) { startSimulation(); }
}

void PathVisualizerManager::setShowSpecularRays(bool flag) { mParams->mSpecularRaysOn = flag; }
void PathVisualizerManager::setShowDiffuseRays(bool flag) { mParams->mDiffuseRaysOn = flag; }
void PathVisualizerManager::setShowBsdfSamples(bool flag) { mParams->mBsdfSamplesOn = flag; }
void PathVisualizerManager::setShowLightSamples(bool flag) { mParams->mLightSamplesOn = flag; }

void PathVisualizerManager::setCameraRayColor(scene_rdl2::math::Color color) { mParams->mCameraRayColor = color; }
void PathVisualizerManager::setSpecularRayColor(scene_rdl2::math::Color color) { mParams->mSpecularRayColor = color; }
void PathVisualizerManager::setDiffuseRayColor(scene_rdl2::math::Color color) { mParams->mDiffuseRayColor = color; }
void PathVisualizerManager::setBsdfSampleColor(scene_rdl2::math::Color color) { mParams->mBsdfSampleColor = color; }
void PathVisualizerManager::setLightSampleColor(scene_rdl2::math::Color color) { mParams->mLightSampleColor = color; }

void PathVisualizerManager::setLineWidth(const float value) { mParams->mLineWidth = value; }

void PathVisualizerManager::setUseSceneSamples(bool useSceneSamples, bool update)
{ 
    mParams->mUseSceneSamples = useSceneSamples; 
    if (update) { startSimulation(); }
}
void PathVisualizerManager::setPixelSamples(uint32_t samples, bool update)
{ 
    mParams->mPixelSamples = samples * samples; 
    if (update) { startSimulation(); }
}
void PathVisualizerManager::setLightSamples(uint32_t samples, bool update)
{ 
    mParams->mLightSamples = samples * samples; 
    if (update) { startSimulation(); }
}
void PathVisualizerManager::setBsdfSamples(uint32_t samples, bool update)
{ 
    mParams->mBsdfSamples = samples * samples; 
    if (update) { startSimulation(); }
}

//------------------------------------------------------------------------------------------

void
PathVisualizerManager::setupSimGlobalInfo(scene_rdl2::grid_util::PathVisSimGlobalInfo& globalInfo) const
{
    globalInfo.setPathVisActive(isOn());
    if (!isOn()) return;
    
    globalInfo.setSamples(mParams->mPixelX, mParams->mPixelY, mParams->mMaxDepth,
                          mParams->mPixelSamples, mParams->mLightSamples, mParams->mBsdfSamples);
    globalInfo.setRayTypeSelection(mParams->mUseSceneSamples,
                                   mParams->mOcclusionRaysOn,
                                   mParams->mSpecularRaysOn,
                                   mParams->mDiffuseRaysOn,
                                   mParams->mBsdfSamplesOn,
                                   mParams->mLightSamplesOn);
    globalInfo.setColor(mParams->mCameraRayColor,
                        mParams->mSpecularRayColor,
                        mParams->mDiffuseRayColor,
                        mParams->mBsdfSampleColor,
                        mParams->mLightSampleColor);
    globalInfo.setLineWidth(mParams->mLineWidth);
}

bool
PathVisualizerManager::getCamPos(scene_rdl2::math::Vec3f& camPos) const
{
    return mPathVisualizer->getCamPos(camPos);
}

std::vector<scene_rdl2::math::Vec3f>
PathVisualizerManager::getCamRayIsectSfPos() const
{
    return mPathVisualizer->getCamRayIsectSfPos();
}

size_t
PathVisualizerManager::serializeNodeDataAll(std::string& buff) const
//
// Return non-zero data size even if node total and vtx total both are zero,
// because the data size is encoded.
//
{
    return mPathVisualizer->serializeNodeDataAll(buff);
}

//------------------------------------------------------------------------------------------

void
PathVisualizerManager::parserConfigure()
{
    mParser.description("PathVisualizerManager command");

    mParser.opt("pathVis", "...command...", "pathVisualizer command",
                [&](Arg& arg) { return mPathVisualizer->getParser().main(arg.childArg()); });
    mParser.opt("param", "...command...", "parameters",
                [&](Arg& arg) { return mParams->getParser().main(arg.childArg()); });
    mParser.opt("showInitCamXform", "", "show initialCameraXform",
                [&](Arg& arg) { return arg.msg(showInitialCameraXform() + '\n'); });
}

std::string
PathVisualizerManager::showInitialCameraXform() const
{
    auto showMtx = [](const scene_rdl2::rdl2::Mat4d& mtx) {
        auto showF = [](const float f) {
            std::ostringstream ostr;
            ostr << std::setw(10) << std::fixed << std::setprecision(5) << f;
            return ostr.str();
        };
        std::ostringstream ostr;
        ostr << showF(mtx.vx.x) << ", " << showF(mtx.vx.y) << ", " << showF(mtx.vx.z) << ", " << showF(mtx.vx.w) << '\n'
             << showF(mtx.vy.x) << ", " << showF(mtx.vy.y) << ", " << showF(mtx.vy.z) << ", " << showF(mtx.vy.w) << '\n'
             << showF(mtx.vz.x) << ", " << showF(mtx.vz.y) << ", " << showF(mtx.vz.z) << ", " << showF(mtx.vz.w) << '\n'
             << showF(mtx.vw.x) << ", " << showF(mtx.vw.y) << ", " << showF(mtx.vw.z) << ", " << showF(mtx.vw.w);
        return ostr.str();
    };

    std::ostringstream ostr;
    ostr << "mInitialCameraXform {\n"
         << scene_rdl2::str_util::addIndent(showMtx(mInitialCameraXform)) + '\n'
         << "}";
    return ostr.str();
}

} // end namespace rndr
} // end namespace moonray
