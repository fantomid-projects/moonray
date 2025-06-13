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
    int mMinPixelX  = 0;
    int mMinPixelY  = 0;
    int mMaxPixelX  = -1;
    int mMaxPixelY  = -1;
    int mMaxDepth   = 1;
    bool mOcclusionRaysOn   = true;
    bool mSpecularRaysOn    = true;
    bool mDiffuseRaysOn     = true;
    bool mBsdfSamplesOn     = true;
    bool mLightSamplesOn    = true;
    scene_rdl2::math::Color mBsdfSampleColor    = scene_rdl2::math::Color(1, 0.4, 0);
    scene_rdl2::math::Color mLightSampleColor   = scene_rdl2::math::Color(1, 1, 0);
    scene_rdl2::math::Color mCameraRayColor     = scene_rdl2::math::Color(0, 0, 1);
    scene_rdl2::math::Color mDiffuseRayColor    = scene_rdl2::math::Color(1, 0, 1);
    scene_rdl2::math::Color mSpecularRayColor   = scene_rdl2::math::Color(0, 1, 1);
    float mLineWidth = 2;
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

    // Stores node info in a buffer of "pixels". The data structure looks like this:
    // [ pixel1: [subpixel1: [node1, node2,...], subpixel2: [node1, node2,...]], pixel2: ...]
    // The nodes are all stored as indices to the member variable mNodes.
    struct PixelBuffer {
        std::vector<Pixel> mData;
        mutable std::mutex mLock;
        int mWidth = -1;
        int mHeight = -1;

        PixelBuffer() : mWidth(-1), mHeight(-1), mData() {}

        PixelBuffer(int w, int h, int pixelSamples) 
            : mWidth(w), mHeight(h), mData()
        {
            mData.resize(w * h);

            // For each pixel, there should be "pixelSamples"
            // number of subpixel arrays allocated
            for (Pixel& pixel : mData) {
                pixel.resize(pixelSamples);
            }
        }

        void validate(int pixelID, int sp, int nodeIndex) const
        {
            MNRY_ASSERT(pixelID >= 0 && pixelID < mData.size());
            MNRY_ASSERT(sp >= 0 && sp < mData[pixelID].size());
            MNRY_ASSERT(nodeIndex >= 0);
        }

        void addNode(int pixelID, int sp, int nodeIndex)
        {
            std::scoped_lock<std::mutex> lock(mLock);
            validate(pixelID, sp, nodeIndex);

            SubpixelPath& spPath = mData[pixelID][sp];
            spPath.push_back(nodeIndex);
        }

        /// Gets the expanded pixel ID
        int getPixelID(int px, int py) const
        {
            return py * mWidth + px; 
        }

        const Pixel& getPixelAt(int px, int py) const
        {
            std::scoped_lock<std::mutex> lock(mLock);
            int pixelID = getPixelID(px, py);
            MNRY_ASSERT(pixelID >= 0 && pixelID < mData.size());
            return mData[pixelID];
        }

        int getSize() const
        {
            int pixelsSize = sizeof(mData);
            int widthAndHeight = sizeof(int) * 2;

            for (const Pixel& pixel : mData) {
                pixelsSize += sizeof(pixel);
                for (const SubpixelPath& path : pixel) {
                    pixelsSize += sizeof(path) + sizeof(int) * path.size();
                }
            }
            return pixelsSize + widthAndHeight + sizeof(std::mutex);
        }
    };

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

        // Copy constructor
        Node(const Node& other)
            : mRayOriginIndex(other.mRayOriginIndex),
              mRayEndpointIndex(other.mRayEndpointIndex),
              mRayIsectIndex(other.mRayIsectIndex),
              mDepth(other.mDepth),
              mFlags(other.mFlags)
        {}

        // Move constructor
        Node(Node&& other)
            : mRayOriginIndex(other.mRayOriginIndex),
              mRayEndpointIndex(other.mRayEndpointIndex),
              mRayIsectIndex(other.mRayIsectIndex),
              mDepth(other.mDepth),
              mFlags(other.mFlags)
        {
            other.mRayOriginIndex = 0;
            other.mRayEndpointIndex = 0;
            other.mRayIsectIndex = 0;
            other.mDepth = 0;
            other.mFlags = Flags::NONE;
        }

        Node() = default;
        ~Node() = default;
    };

public:
    PathVisualizer(int width, int height, int pixelSamplesSqrt, const PathVisualizerParams* params, float sceneSize);
    ~PathVisualizer();

    /// Initializes the path visualizer, resetting the member variables and reserving memory capacity
    void initialize(int width, int height, int pixelSamplesSqrt, int lightSamplesSqrt,
                    int bsdfSamplesSqrt, int maxDepth);

    /// Calls recordRay() to record an occlusion ray
    void recordOcclusionRay(const mcrt_common::Ray& ray, const Scene& scene, int pixel, int spIndex,
                            bool lightSampleFlag, bool occlusionFlag);

    /// Calls recordRay() to record a regular ray
    void recordRegularRay(const mcrt_common::Ray& ray, const Scene& scene, int pixel, int spIndex, int lobeType);

    /// Draws the path visualization with the given user parameters
    void draw(scene_rdl2::fb_util::RenderBuffer* renderBuffer, const Scene* scene);

    // Turns off the path visualizer
    // Prevents any more ray data from being recorded
    void turnOff() 
    { 
        if (mOn) {
            mOn = false;
            std::cout << "------------------ Path Visualizer Stats ----------------------\n";
            std::cout << "Memory footprint (in MB): " << (getMemoryFootprint() / 1000000.f) << std::endl;
            printTimeStats();
            std::cout << "---------------------------------------------------------------\n";
        }
    }

    /// Turns on the path visualizer
    /// Ensures we will record rays during rendering
    void turnOn()  
    {
        std::scoped_lock<std::mutex> lock(mLock);
        mOn = true; 
    }

    /// Is the path visualizer on?
    bool isOn() const { return mOn; }

    // Print ALL of the Nodes, up to maxEntries
    // If maxEntries == -1, print all nodes
    void printNodes(int maxEntries) const;

    /// Returns the amount of memory used, in bytes
    int getMemoryFootprint() const;

    /// Print how long everything took
    void printTimeStats() const;

private:
    
    /// Creates a new Node to store all of the given ray information
    void recordRay(const mcrt_common::Ray& ray, const Scene& scene, int pixel, int spIndex,
                   int lobeType, bool lightSampleFlag, bool occlusionFlag);
    
    /// Sets up the viewing frustum, mFrustum, for the given camera
    bool setUpFrustum(const Camera& cam);    

    // Given some params to filter by, return the nodes that match that criteria
    void filter(std::vector<int>& filteredNodes) const;

    /// Find the ray origin, endpoint, and intersection, if it's an occlusion ray. Then,
    /// clip those points using the viewing frustum. Returns the number of elements 
    /// in outPoints (can be 2-3)
    uint8_t clipPoints(int nodeIndex, scene_rdl2::math::Vec3f* outPoints) const;

    /// Check if the current pixel is occluded by scene geometry
    bool pixelIsOccluded(int x, int y, const scene_rdl2::math::Vec2f& p1, const pbr::Scene* scene, 
                         float totalDistance, float invDepth1, float invDepthDiff) const;

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

    /// Get node
    inline const Node& getNode(int nodeIndex) const
    {
        std::scoped_lock<std::mutex> lock(mNodesLock);
        return mNodes[nodeIndex];
    }

    /// Get vertex
    inline const scene_rdl2::math::Vec3f& getVert(int vertIndex) const
    {
        std::scoped_lock<std::mutex> lock(mVertexBufferLock);
        return mVertexBuffer[vertIndex];
    }

    /// Tells us whether the node at nodeIndex matches the given flag
    inline bool matchesFlag(int nodeIndex, const Flags& flag) const;

    /// Tells us whether the node at nodeIndex matches the current user-specified flags in mParams
    inline bool matchesFlags(int nodeIndex) const;

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
        return x >= 0 && x < mPixelBuffer.mWidth && y >= 0 && y < mPixelBuffer.mHeight; 
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

    /// A pixel array, where a vector of subpixels lives at
    /// each pixel index
    /// [ pixel1: [subpixel1: [node1, node2, etc], subpixel2: [node1, node2, etc]], pixel2: ...]
    PixelBuffer mPixelBuffer;

    /// A read-only pointer to the user parameters, set in the PathVisualizerManager
    const PathVisualizerParams* mParams;

    /// Is the PathVisualizer on?
    bool mOn = false;

    /// The index in mVertices of a camera ray's intersection with the scene.
    /// Used to draw the pixel focus
    mutable int mCameraIsectIndex = -1;

    /// The camera's viewing frustum
    std::unique_ptr<mcrt_common::Frustum> mFrustum;

    /// Maximum possible ray length
    float mMaxRayLength;

    /// Mutex to avoid simultaneous writes by different threads
    mutable std::mutex mLock;
    mutable std::mutex mNodesLock;
    mutable std::mutex mVertexBufferLock;

    /// Timing statistics
    mutable moonray::util::AverageDouble mInRenderingTime;
    mutable moonray::util::AverageDouble mPostRenderingTime;
};

} // end namespace rndr
} // end namespace moonray