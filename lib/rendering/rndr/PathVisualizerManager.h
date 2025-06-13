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

/// Manages the PathVisualizer object. The PathVisualizer doesn't exist (is nullptr) until the user
/// specifies that they want to start recording ray data. Until that time, the PathVisualizerManager
/// simply handles the parameters set by the user. The mTriggered member variable tells us whether the 
/// user specified we should start recording data for the PathVisualizer. During RenderContext::startFrame(), 
/// if mTriggered is true, we initialize the PathVisualizer object with the given parameters.
class PathVisualizerManager {

public:
    PathVisualizerManager();
    ~PathVisualizerManager();

    /// Checks if mTrigger is true -- if so, create the PathVisualizer object
    void initialize(const scene_rdl2::rdl2::SceneVariables& vars, const pbr::Scene* scene);

    /// If the PathVisualizer exists, draws the visualization
    void draw(scene_rdl2::fb_util::RenderBuffer* renderBuffer);

    /// Turns off the recording functionality for the PathVisualizer
    void turnOff();

    /// Indicate that the user wants to build the PathVisualizer
    void trigger() { mTriggered = true; }

    /// Check if the PathVisualizer has been created
    bool pathVisualizerExists() const;

    /// Check if the PathVisualizer is on (i.e. if it is recording data)
    bool isPathVisualizerOn() const;

    /// ------------------------- UI setters --------------------------------- //

    void setPixelX(int px);
    void setPixelY(int py);
    void setMaxDepth(int depth);

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

private:
    // pointer to the PathVisualizer object -- nullptr if not created yet
    pbr::PathVisualizer* mPathVisualizer;

    // Has the user specified we should create a PathVisualizer object?
    bool mTriggered;

    // Collection of user parameters
    std::unique_ptr<pbr::PathVisualizerParams> mParams;

    // Pointer to the Scene -- nullptr until we initialize the PathVisualizer
    pbr::Scene* mScene;
};

} // end namespace rndr
} // end namespace moonray