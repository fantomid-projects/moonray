// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <moonray/rendering/pbr/core/RayState.h>

#include <scene_rdl2/common/fb_util/FbTypes.h>

namespace moonray {

namespace mcrt_common { struct Frustum; }

namespace pbr {

class Camera;

// All of the user parameters used to filter the nodes
struct PathVisualizerParams {
    int mPixelX         = 0;            // x-coord for user-chosen pixel
    int mPixelY         = 0;            // y-coord for user-chosen pixel
    int mMaxDepth       = 1;            // max number of bounces
    int mPixelSamples   = 4;            // # of pixel samples
    int mLightSamples   = 1;            // # of light samples
    int mBsdfSamples    = 1;            // # of bsdf samples
    bool mUseSceneSamples   = true;     // whether to use the sampling settings from the rdla or user-specified settings
    bool mOcclusionRaysOn   = true;     // whether to display occlusion rays (bsdf + light)
    bool mSpecularRaysOn    = true;     // whether to display specular rays
    bool mDiffuseRaysOn     = true;     // whether to display diffuse rays
    bool mBsdfSamplesOn     = true;     // whether to display occlusion rays sampled from the bsdf
    bool mLightSamplesOn    = true;     // whether to display occlusion rays sampled from the light
    scene_rdl2::math::Color mCameraRayColor     = scene_rdl2::math::Color(0, 0, 1);     // color of the camera rays
    scene_rdl2::math::Color mSpecularRayColor   = scene_rdl2::math::Color(0, 1, 1);     // color of the specular rays
    scene_rdl2::math::Color mDiffuseRayColor    = scene_rdl2::math::Color(1, 0, 1);     // color of the diffuse rays
    scene_rdl2::math::Color mBsdfSampleColor    = scene_rdl2::math::Color(1, 0.4, 0);   // color of the bsdf rays
    scene_rdl2::math::Color mLightSampleColor   = scene_rdl2::math::Color(1, 1, 0);     // color of the light rays
    float mLineWidth = 2;               // width of the lines drawn
};

/// Current state
enum class State : uint8_t {
    OFF                     = 0,            // does not yet contain any data, hasn't been initialized
    READY                   = 1 << 0,       // has been initialized
    RECORD                  = 1 << 1,       // is currently recording data (rendering in debug mode)
    STOP_RECORD             = 1 << 2,       // done recording data -- needs to stop rendering
    REQUEST_DRAW            = 1 << 3,       // we need to force-draw the visualization, even if rendering has ended
    DRAW                    = 1 << 4,       // visualization is ready to draw 
};
    
/// The PathVisualizer class manages the gathering and drawing of ray information 
/// on top of the render buffer. During the rendering process, if the visualizer has been turned on, it gathers all
/// of the ray information (see recordRay()), and stores it in a Node object.
/// The PathVisualizer manages a vector of these nodes, mNodes, which it can then
/// filter according to user specifications. The resulting sublist of nodes is then
/// used during the drawing stage (see PathVisualizer::draw()), where we
/// take the resulting nodes, and use Wu's line drawing algorithm to draw a line
/// for each (see drawLine()). During subsequent camera updates, the node information is not
/// updated, unless specified, to allow the user to see the ray visualization in multiple dimensions.

class PathVisualizer {

#define SubpixelPath std::vector<int>
#define Pixel std::vector<SubpixelPath>

    /// Type flags for a Node
    enum class Flags : uint8_t {
        NONE         = 0,
        DIFFUSE      = 1 << 0,
        SPECULAR     = 1 << 1,
        BSDF_SAMPLE  = 1 << 2,
        LIGHT_SAMPLE = 1 << 3
    };

    // A Node stores all the information about a ray needed for visualization
    struct Node {
        int mRayOriginIndex;                            // index of ray origin point in mVertices (world space)
        int mRayEndpointIndex;                          // index of ray endpoint in mVertices (world space)
        int mRayIsectIndex;                             // index of where the ray intersects (-1 if not occlusion ray)
        int8_t mDepth;                                  // ray depth
        Flags mFlags;                                   // node type flags

        Node(int rayOriginIndex, int rayEndpointIndex, int rayIsectIndex, int rayDepth, Flags flags)
                : mRayOriginIndex(rayOriginIndex), 
                  mRayEndpointIndex(rayEndpointIndex), 
                  mRayIsectIndex(rayIsectIndex),
                  mDepth(rayDepth), 
                  mFlags(flags) {}

        Node() = default;
        ~Node() = default;
    };

public:
    PathVisualizer();
    ~PathVisualizer();

    /// Initializes the visualizer
    void initialize(int width, int height, const PathVisualizerParams* params, float sceneSize);

    /// Calls recordRay() to record an occlusion ray
    void recordOcclusionRay(const mcrt_common::Ray& ray, const Scene& scene, int pixel, int spIndex,
                            bool lightSampleFlag, bool occlusionFlag);

    /// Calls recordRay() to record a regular ray
    void recordRegularRay(const mcrt_common::Ray& ray, const Scene& scene, int pixel, int spIndex, int lobeType);

    /// Draws the path visualization with the given user parameters
    void draw(scene_rdl2::fb_util::RenderBuffer* renderBuffer, const Scene* scene);

    /// Clears all ray data
    void reset();

    /// Returns the amount of memory used, in bytes
    int getMemoryFootprint() const;

    /// Gets/sets the current state of the visualizer
    const State& getState() const { return mState; }
    void setState(State state);

    /// Gets/sets whether we need to restart rendering
    /// once we are done gathering data
    bool getNeedsRenderRefresh() const { return mNeedRenderRefresh; }
    void setNeedsRenderRefresh(bool refresh) 
    {
        std::lock_guard<std::mutex> lock(mWriteLock);
        mNeedRenderRefresh = refresh; 
    }

    // Sets the given 'samples' input to the appropriate number
    // based on the users selections
    void setLightSamples(int& samples) const;
    void setBsdfSamples(int& samples) const;

    /// Print how long everything took
    void printTimeStats() const;

    // Print ALL of the Nodes, up to maxEntries
    // If maxEntries == -1, print all nodes
    void printNodes(int maxEntries) const;


private:
    /// Sets up the viewing frustum, mFrustum, for the given camera
    bool setUpFrustum(const Camera& cam);    

    /// Creates a new Node to store all of the given ray information
    void recordRay(const mcrt_common::Ray& ray, const Scene& scene, int pixel, int spIndex,
                   int lobeType, bool lightSampleFlag, bool occlusionFlag);

    /// Given some ray data, check whether that ray matches the user parameters
    bool matchesParams(int pixel, int lobeType, bool lightSampleFlag, int depth) const;

    /// Check if the current pixel is occluded by scene geometry
    bool pixelIsOccluded(int x, int y, const scene_rdl2::math::Vec2f& p1, const pbr::Scene* scene, 
                         float totalDistance, float invDepth1, float invDepthDiff) const;
    
    /// Find the ray origin, endpoint, and intersection, if it's an occlusion ray. Then,
    /// clip those points using the viewing frustum. Returns the number of elements 
    /// in outPoints (can be 2-3)
    uint8_t clipPoints(int nodeIndex, scene_rdl2::math::Vec3f* outPoints) const;

    /// Given a node and a function to write to a pixel in the render buffer, use 
    /// Wu's line drawing algorithm to draw a line representing the ray to the buffer.
    void drawLine(const std::function<void(int, int, scene_rdl2::math::Color&, float)>& writeToRenderBuffer, 
                  int nodeIndex, const pbr::Scene* scene) const;

    /// Draws a square around the chosen pixel
    void drawPixelFocus(const std::function<void(int, int, scene_rdl2::math::Color&, float)>& writeToRenderBuffer,
                        const pbr::Camera* cam) const;

    /// ---------- Utilities -----------------------------------------------------------------
    
    /// Finds the world-space intersection of the given ray with the scene
    scene_rdl2::math::Vec3f findSceneIsect(const mcrt_common::Ray& ray, const Scene& scene) const;

    /// Transform given world-space point to screen space
    scene_rdl2::math::Vec2f transformPointWorld2Screen(const scene_rdl2::math::Vec3f& p, const pbr::Camera* cam) const;

    /// ---- Getters ----

    /// Tells us whether the node at nodeIndex matches the given flag
    inline bool matchesFlag(int nodeIndex, const Flags& flag) const;

    /// Tells us whether the given lobeType matches the given flag
    inline bool matchesFlag(int lobeType, int flag) const;

    /// Tells us whether the node at nodeIndex matches the current user-specified flags in mParams
    inline bool matchesFlags(int lobeType, int lightSampleFlag, int depth) const;

    /// Gets the depth of the ray
    inline int getRayDepth(int nodeIndex) const;

    /// Does ray depth == 0?
    inline bool isCameraRay(int nodeIndex) const;

    /// Gets the ray origin for the given node
    inline scene_rdl2::math::Vec3f getRayOrigin(int nodeIndex) const;

    /// Gets the ray endpoint for the given node
    inline scene_rdl2::math::Vec3f getRayEndpoint(int nodeIndex) const;

    /// Gets the ray isect for the given node
    /// Returns a zero vector if there is no isect for the node
    inline scene_rdl2::math::Vec3f getRayIsect(int nodeIndex) const;

    /// Given the node index, returns a color based on ray type
    inline scene_rdl2::math::Color getRayColor(int nodeIndex) const;

    /// Have we added any nodes to the visualizer yet?
    inline bool isEmpty() const { return mNodes.size() == 0; }

    /// Checks that the given pixel is in the image bounds
    inline bool isInBounds(int x, int y) const
    { 
        return x >= 0 && x < mWidth && y >= 0 && y < mHeight; 
    }

    /// ---- Setters ----

    /// Resets the camera ray intersection index
    inline void resetCameraIsectIndex() { mCameraIsectIndex = -1; }

    /// Given the writable "flags", set the provided flag
    inline void setFlag(Flags& flags, const Flags& flag) const;

    /// Given the writable "flags", set the appropriate flags based on the boolean vars
    inline void setFlags(Flags& flags, bool isDiffuse, bool isSpecular, bool isLightSample) const;

    /// Add the vertex to the vertex list and return its index
    inline int addVertex(const scene_rdl2::math::Vec3f& v);

    // Create a new node and add it to the nodes list
    inline void addNode(int pixelID, int originIndex, int endpointIndex, int isectIndex, 
                        int depth, int subpixel, Flags& flags);

    /// -----------------

    // Given a list of Node indices, print them
    void printNodes(std::vector<int>& filteredList);

    /// ---------------------- Member variables --------------------------------

    /// All of the Nodes for the given viewport
    std::vector<Node> mNodes;

    /// A list of path vertices, intended to reduce some duplicate vertices
    std::vector<scene_rdl2::math::Vec3f> mVertexBuffer;

    /// A read-only pointer to the user parameters, set in the PathVisualizerManager
    const PathVisualizerParams* mParams;

    /// What state is the visualizer in?
    State mState;

    /// Width/height of the render buffer
    int mWidth;
    int mHeight;

    /// Whether we need to restart rendering after gathering ray data
    bool mNeedRenderRefresh = false;

    /// The index in mVertices of a camera ray's intersection with the scene.
    /// Used to draw the pixel focus
    mutable int mCameraIsectIndex = -1;

    /// The camera's viewing frustum
    std::unique_ptr<mcrt_common::Frustum> mFrustum;

    /// Maximum possible ray length
    float mMaxRayLength;

    /// Mutex to avoid simultaneous writes by different threads
    mutable std::mutex mWriteLock;

    /// Timing statistics
    mutable moonray::util::AverageDouble mInRenderingTime;
    mutable moonray::util::AverageDouble mPostRenderingTime;
};

} // end namespace rndr
} // end namespace moonray