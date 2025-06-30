// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <memory>

#include <scene_rdl2/common/fb_util/FbTypes.h>

#pragma once

namespace scene_rdl2 { 
    namespace rdl2 { class SceneVariables; }
    namespace math { class Color; }
}

namespace moonray {

namespace pbr { class Scene; class PathVisualizer; struct PathVisualizerParams; }

namespace rndr {

class RenderContext;

/// Manages the PathVisualizer object. The PathVisualizer doesn't exist (is nullptr) until the user
/// specifies that they want to start recording ray data. Until that time, the PathVisualizerManager
/// simply handles the parameters set by the user. The mTriggered member variable tells us whether the 
/// user specified we should start recording data for the PathVisualizer. During RenderContext::startFrame(), 
/// if mTriggered is true, we initialize the PathVisualizer object with the given parameters.
class PathVisualizerManager {

public:
    PathVisualizerManager(RenderContext* renderContext);
    ~PathVisualizerManager();

    /// Checks if mTrigger is true -- if so, create the PathVisualizer object
    void initialize(const scene_rdl2::rdl2::SceneVariables& vars, pbr::Scene* scene);

    // ----------------------------------------------------------------------------

    // Starts the recording process
    void startSimulation();

    // Sets the state to STOP_RECORD
    void stopSimulation();

    // Requests that the gui draw the visualization
    // This forces a draw, whereas the DRAW state
    // indicates that we CAN draw
    void requestDraw();

    // Sets the state to DRAW
    void startDraw();

    /// Draws the visualization
    void draw(scene_rdl2::fb_util::RenderBuffer* renderBuffer);

    // Indicates whether we need to restart rendering after recording ray data
    void setNeedsRenderRefresh(bool refresh);

    // Clear all PathVisualizer data
    void reset();

    // ----------------------------------------------------------------------------

    // Whether the PathVisualizer is off (not initialized)
    bool isOff() const;

    // Whether PathVisualizer is initialized
    bool isInReadyState() const;

    // Whether the PathVisualizer is recording
    bool isInRecordState() const;

    // Whether the PathVisualizer needs to stop recording
    bool isInStopRecordState() const;

    // Whether the gui needs to forcibly draw the visualization
    bool isDrawRequested() const;

    // Whether the PathVisualizer is ready for drawing
    bool isInDrawState() const;

    // Whether we need to restart rendering after recording is done
    bool getNeedsRenderRefresh() const;

    /// Check if the PathVisualizer has been created
    bool getPathVisualizerExists() const;

    scene_rdl2::math::Vec2i getPixel() const;

    /// ------------------------- UI setters --------------------------------- //

    void setPixelX(int px);
    void setPixelY(int py);
    void setMaxDepth(int depth);

    void fillPixelSamples(unsigned int& samples) const;

    void setOcclusionRaysFlag(bool flag);
    void setSpecularRaysFlag(bool flag);
    void setDiffuseRaysFlag(bool flag);
    void setBsdfSamplesFlag(bool flag);
    void setLightSamplesFlag(bool flag);

    void setCameraRayColor(scene_rdl2::math::Color color);
    void setSpecularRayColor(scene_rdl2::math::Color color);
    void setDiffuseRayColor(scene_rdl2::math::Color color);
    void setBsdfSampleColor(scene_rdl2::math::Color color);
    void setLightSampleColor(scene_rdl2::math::Color color);

    void setLineWidth(int value);

    void setUseSceneSamples(int useSceneSamples);
    void setPixelSamples(int samples);
    void setLightSamples(int samples);
    void setBsdfSamples(int samples);

private:
    // pointer to the PathVisualizer object
    std::unique_ptr<pbr::PathVisualizer> mPathVisualizer;

    RenderContext* mRenderContext;

    // Collection of user parameters
    std::unique_ptr<pbr::PathVisualizerParams> mParams;

    // Pointer to the Scene -- nullptr until we initialize the PathVisualizer
    pbr::Scene* mScene;
};

} // end namespace rndr
} // end namespace moonray