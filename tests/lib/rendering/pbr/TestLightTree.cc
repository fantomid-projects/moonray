// Copyright 2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "TestLightTree.h"

#include <moonray/rendering/pbr/light/LightTreeUtil.h>
#include <moonray/rendering/pbr/light/LightTreeUtil.cc>
#include <moonray/rendering/pbr/light/LightTree.h>
#include <moonray/rendering/pbr/light/DiskLight.h>
#include <moonray/rendering/pbr/light/SphereLight.h>
#include <moonray/rendering/pbr/light/SpotLight.h>
#include <moonray/rendering/pbr/sampler/IntegratorSample.h>
#include <moonray/rendering/pbr/sampler/SequenceID.h>

#include <scene_rdl2/scene/rdl2/rdl2.h>
#include <scene_rdl2/render/util/Random.h>

namespace moonray {
namespace pbr {

using namespace scene_rdl2::math;

static bool equal(const float& a, const float& b)
{
    return scene_rdl2::math::abs(a - b) < 0.000001f;
}

static bool conesAreEqual(const LightTreeCone& c1, const LightTreeCone& c2)
{
    return isEqual(c1.mAxis, c2.mAxis) && equal(c1.mCosThetaE, c2.mCosThetaE) &&
           equal(c1.mCosThetaO, c2.mCosThetaO) && equal(c1.mTwoSided, c2.mTwoSided);
}

static void printCone(const char* nameOfCone, const LightTreeCone& cone)
{
    std::cerr << "LightTreeCone " << nameOfCone << " = ( mAxis: (" << cone.mAxis.x << ", " << cone.mAxis.y << ", " 
              << cone.mAxis.z << "), mCosThetaO: " << cone.mCosThetaO << ", mCosThetaE: " << cone.mCosThetaE 
              << ", mTwoSided: " << cone.mTwoSided << ")\n";
}

void TestLightTree::testCone()
{
    fprintf(stderr, "=========================== Testing LightTree Cone ==============================\n");
    // If I combine identical cones, I should get back the same cone
    {
        std::cerr << "---- Combine identical cones to get the same cone ----\n";
        const LightTreeCone a{ Vec3f(0.f, 0.f, 1.f), /*cos(0rad)*/ 1.f, /*cos(pi/2rad)*/ 0.f, false };
        const LightTreeCone b{ a };
        const LightTreeCone result = combineCones(a, b);
        printCone("a", a);
        printCone("b", b);
        CPPUNIT_ASSERT(conesAreEqual(a, b));
        std::cerr << "LightTreeCone a and LightTreeCone b are equal\n";
        printCone("b", b);
        printCone("result", result);
        CPPUNIT_ASSERT(conesAreEqual(b, result));
        std::cerr << "LightTreeCone b and the resulting LightTreeCone are equal\n";
    }
    // If I combine an empty cone and a non-empty cone, I should get back the non-empty cone
    {
        std::cerr << "---- Combine an empty cone and non-empty cone, and get back the non-empty cone ----\n";
        const LightTreeCone emptyCone{};
        const LightTreeCone a{ Vec3f(0.f, 0.f, 1.f), /*cos(0rad)*/ 1.f, /*cos(pi/2rad)*/ 0.f, false };
        const LightTreeCone result = combineCones(a, emptyCone);
        printCone("empty", emptyCone);
        printCone("a", a);
        printCone("result", result);
        CPPUNIT_ASSERT(conesAreEqual(a, result));
    }
    // If one cone entirely contains the other cone, we should get back the larger cone, plus any extra emission
    {
        std::cerr << "---- Larger cone should subsume smaller cone ----\n";
        const LightTreeCone largerCone{ Vec3f(0.f, 0.f, 1.f), /*cos(pi/2rad)*/ 0.f, /*cos(0rad)*/ 1.f, false };
        const LightTreeCone smallerCone{ Vec3f(0.f, 0.f, 1.f), /*cos(0rad)*/ 1.f, /*cos(pi/2rad)*/ 0.f, false };
        const LightTreeCone result = combineCones(largerCone, smallerCone);
        printCone("larger", largerCone);
        printCone("smaller", smallerCone);
        printCone("result", result);
        const LightTreeCone expectedCone{ Vec3f(0.f, 0.f, 1.f), /*cos(pi/2rad)*/ 0.f, /*cos(pi/2rad)*/ 0.f, false };
        CPPUNIT_ASSERT(conesAreEqual(result, expectedCone));
    }
    // If one cone does not completely subsume the other, the resulting axis should be in between
    // the two axes, and theta_o and theta_e should encompass both cones's scopes
    {
        std::cerr << "---- Two cones, whose axes are orthogonal, both with thetaO 20deg ----\n";
        // LightTreeCone pointing in +x direction, thetaO is 20deg, thetaE is 70deg
        const LightTreeCone a{ Vec3f(1.f, 0.f, 0.f), 0.9396926f, 0.34202014f, false };
        // LightTreeCone pointing in +y direction, thetaO is 20deg, thetaE is 90deg
        const LightTreeCone b{ Vec3f(0.f, 1.f, 0.f), 0.9396926f, 0.f, false };
        // Result: LightTreeCone with axis (sqrt2/2, sqrt2/2, 0), thetaO is 65deg (45deg + 20deg), thetaE is 90deg
        const LightTreeCone result = combineCones(a, b);
        const LightTreeCone expected{ Vec3f(0.7071068f, 0.7071068f, 0.f), 0.42261826f, 0.f, false };
        printCone("a", a);
        printCone("b", b);
        printCone("result", result);
        CPPUNIT_ASSERT(conesAreEqual(result, expected));
    }
    // If one cone does not completely subsume the other, the resulting axis should be in between
    // the two axes, and theta_o and theta_e should encompass both cones's scopes
    {
        std::cerr << "---- Two cones, whose axes are orthogonal, one with thetaO 20deg, one with thetaO 30deg ----\n";
        // LightTreeCone pointing in +x direction, thetaO is 20deg, thetaE is 70deg
        const LightTreeCone a{ Vec3f(1.f, 0.f, 0.f), 0.9396926f, 0.34202014f, false };
        // LightTreeCone pointing in +y direction, thetaO is 30deg, thetaE is 90deg
        const LightTreeCone b{ Vec3f(0.f, 1.f, 0.f), 0.866025403f, 0.f, false };
        // Result: LightTreeCone whose axis is at 50deg on the unit circle, thetaO is 70deg ((90deg + 30deg + 20deg) / 2),
        // thetaE is 90deg
        // to get 50deg for the axis: thetaO is 70deg -- since cone a starts at -20deg on the unit circle, the axis
        // ends up being at positive 50deg on the unit circle
        const LightTreeCone result = combineCones(a, b);
        const LightTreeCone expected{ Vec3f(0.642787f, 0.766044f, 0.f), 0.342020f, 0.f, false };
        printCone("a", a);
        printCone("b", b);
        printCone("result", result);
        CPPUNIT_ASSERT(conesAreEqual(result, expected));
    }
}

// ------------------------------ Test Setup/Teardown ------------------------------

void TestLightTree::setUp()
{
    mContext = new scene_rdl2::rdl2::SceneContext();
    mContext->setDsoPath(RDL2DSO_PATH);
}

void TestLightTree::tearDown()
{
    delete mContext;
    mContext = nullptr;
}

// ------------------------------ Helper Functions ------------------------------

static std::shared_ptr<DiskLight> createDiskLight(
    scene_rdl2::rdl2::SceneContext* context,
    const Vec3f& position,
    float radius = 5.0f,
    const scene_rdl2::math::Color& color = scene_rdl2::math::Color(1.0f),
    int lightIndex = 0,
    float exposure = 20.0f,
    float spread = 0.7f)
{
    // Create unique name for each light
    std::string lightName = "/diskLight" + std::to_string(lightIndex);
    auto* lightObj = context->createSceneObject("DiskLight", lightName);
    
    // Create transformation matrix with position - disk facing down (-Y)
    scene_rdl2::math::Mat4f xformf = scene_rdl2::math::Mat4f(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, -1.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        position.x, position.y, position.z, 1.0f);
    scene_rdl2::math::Mat4d xform = scene_rdl2::math::toDouble(xformf);
    
    lightObj->beginUpdate();
    lightObj->set<scene_rdl2::rdl2::Mat4d>(scene_rdl2::rdl2::Node::sNodeXformKey, xform);
    lightObj->set<scene_rdl2::rdl2::Rgb>("color", color);
    lightObj->set<scene_rdl2::rdl2::Bool>("normalized", true);
    lightObj->set<scene_rdl2::rdl2::Float>("radius", radius);
    lightObj->set<scene_rdl2::rdl2::Float>("exposure", exposure);
    lightObj->set<scene_rdl2::rdl2::Float>("spread", spread);
    lightObj->endUpdate();
    
    auto light = std::make_shared<DiskLight>(lightObj->asA<scene_rdl2::rdl2::Light>());
    light->update(xform);
    return light;
}

// Helper function to create a SphereLight at the given position
std::shared_ptr<SphereLight> createSphereLight(
    scene_rdl2::rdl2::SceneContext* context, 
    const Vec3f& position,
    float radius,
    const scene_rdl2::math::Color& color,
    int& lightIndex,
    float exposure = 18.0f,
    float clearRadius = 10.0f)
{
    // Create unique name for each light
    std::string lightName = "/sphereLight" + std::to_string(lightIndex);
    auto* lightObj = context->createSceneObject("SphereLight", lightName);
    
    // Create transformation matrix with position
    scene_rdl2::math::Mat4f xformf = scene_rdl2::math::Mat4f(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        position.x, position.y, position.z, 1.0f);
    scene_rdl2::math::Mat4d xform = scene_rdl2::math::toDouble(xformf);
    
    lightObj->beginUpdate();
    lightObj->set<scene_rdl2::rdl2::Mat4d>(scene_rdl2::rdl2::Node::sNodeXformKey, xform);
    lightObj->set<scene_rdl2::rdl2::Rgb>("color", color);
    lightObj->set<scene_rdl2::rdl2::Bool>("normalized", true);
    lightObj->set<scene_rdl2::rdl2::Float>("radius", radius);
    lightObj->set<scene_rdl2::rdl2::Float>("exposure", exposure);
    lightObj->set<scene_rdl2::rdl2::Float>("clear_radius", clearRadius);
    lightObj->endUpdate();
    
    auto light = std::make_shared<SphereLight>(lightObj->asA<scene_rdl2::rdl2::Light>());
    light->update(xform);
    return light;
}

// Helper function to create a SpotLight at the given position with orientation
std::shared_ptr<SpotLight> createSpotLight(
    scene_rdl2::rdl2::SceneContext* context,
    const Vec3f& position,
    const Vec3f& direction,
    const scene_rdl2::math::Color& color,
    int& lightIndex,
    float exposure = 25.0f,
    float outerConeAngle = 60.0f,
    float lensRadius = 10.0f,
    float clearRadius = 1000.0f)
{
    // Create unique name for each light
    std::string lightName = "/spotLight" + std::to_string(lightIndex);
    auto* lightObj = context->createSceneObject("SpotLight", lightName);
    
    // Create transformation matrix with position and direction
    Vec3f up(0.0f, 1.0f, 0.0f);
    Vec3f normDir = normalize(direction);
    
    // If direction is nearly parallel to up, use a different up vector
    if (scene_rdl2::math::abs(dot(normDir, up)) > 0.99f) {
        up = Vec3f(1.0f, 0.0f, 0.0f);
    }
    
    Vec3f right = normalize(cross(up, normDir));
    Vec3f newUp = normalize(cross(normDir, right));
    
    scene_rdl2::math::Mat4f xformf = scene_rdl2::math::Mat4f(
        right.x, newUp.x, normDir.x, 0.0f,
        right.y, newUp.y, normDir.y, 0.0f,
        right.z, newUp.z, normDir.z, 0.0f,
        position.x, position.y, position.z, 1.0f);
    scene_rdl2::math::Mat4d xform = scene_rdl2::math::toDouble(xformf);
    
    lightObj->beginUpdate();
    lightObj->set<scene_rdl2::rdl2::Mat4d>(scene_rdl2::rdl2::Node::sNodeXformKey, xform);
    lightObj->set<scene_rdl2::rdl2::Rgb>("color", color);
    lightObj->set<scene_rdl2::rdl2::Bool>("normalized", true);
    lightObj->set<scene_rdl2::rdl2::Float>("exposure", exposure);
    lightObj->set<scene_rdl2::rdl2::Float>("outer_cone_angle", outerConeAngle);
    lightObj->set<scene_rdl2::rdl2::Float>("lens_radius", lensRadius);
    lightObj->set<scene_rdl2::rdl2::Float>("clear_radius", clearRadius);
    lightObj->endUpdate();
    
    auto light = std::make_shared<SpotLight>(lightObj->asA<scene_rdl2::rdl2::Light>());
    light->update(xform);
    return light;
}

// ------------------------------ Scene Reproduction Test ------------------------------

// Given a scene configuration, builds a LightTree and samples from it
void runTest(const Light* const* lights, size_t numLights, float samplingQuality, 
             const Vec3f& samplePoint, const Vec3f& sampleNormal)
{
    // Initialize a tree with the given sampling quality
    LightTree tree(samplingQuality);

    fprintf(stderr, "Building light tree with sampling threshold = %f...\n", samplingQuality);
    tree.build(lights, numLights, nullptr, 0);

    // Tree should be built without crashes
    size_t footprint = tree.getMemoryFootprint();
    CPPUNIT_ASSERT(footprint > 0);

    fprintf(stderr, "Tree built successfully (memory footprint: %zu bytes)\n", footprint);

    std::vector<float> lightSelectionPdfs(numLights, -1.0f);

    // Create light ID map
    std::vector<int> lightIdMap(numLights);
    for (size_t i = 0; i < numLights; ++i) {
        lightIdMap[i] = i;
    }

    IntegratorSample1D sample(SequenceID{}, numLights, 0);

    fprintf(stderr, "Sampling tree from camera position...\n");

    tree.sample(lightSelectionPdfs.data(), samplePoint, sampleNormal, nullptr, sample, lightIdMap.data(), 0);

    // Count selected lights
    int selectedCount = 0;
    for (float pdf : lightSelectionPdfs) {
        if (pdf >= 0.0f) {
            selectedCount++;
        }
    }

    fprintf(stderr, "Selected %d lights without crashing\n", selectedCount);
    CPPUNIT_ASSERT(selectedCount > 0);
}

/// Test case where we only have two lights in the scene. This is a minimal case
/// so that we can verify that the LightTree builds and produces the correct pdfs.
void TestLightTree::testTwoLights()
{
    fprintf(stderr, "=============== Testing LightTree with Two Lights ===============\n");

    std::vector<std::shared_ptr<DiskLight>> lights;
    std::vector<const Light*> lightPtrs;

    // Create two disk lights at different positions
    lights.push_back(createDiskLight(mContext, Vec3f(-10.f, 0.f, 0.f), 5.f, scene_rdl2::math::Color(1.f, 0.f, 0.f), 0));
    lights.push_back(createDiskLight(mContext, Vec3f(10.f, 0.f, 0.f), 5.f, scene_rdl2::math::Color(0.f, 0.f, 1.f), 1));

    for (const auto& light : lights) {
        lightPtrs.push_back(light.get());
    }

    fprintf(stderr, "Created 2 disk lights\n");

    // Sample point in front of the lights
    Vec3f samplePoint(0.0f, 0.0f, -20.0f);
    Vec3f sampleNormal(0.0f, 0.0f, 1.0f); // Facing towards the lights

    // Use a moderate sampling quality
    float samplingQuality = 0.5f;
    LightTree tree(samplingQuality);

    fprintf(stderr, "Building light tree with sampling threshold = %f...\n", samplingQuality);
    tree.build(lightPtrs.data(), 2, nullptr, 0);

    // There should be three nodes: root + two leaves
    size_t expectedNodeCount = 3;
    size_t actualNodeCount = tree.getNodeCount();
    fprintf(stderr, "Light tree built with %zu nodes (expected %zu nodes)\n", actualNodeCount, expectedNodeCount);
    CPPUNIT_ASSERT(actualNodeCount == expectedNodeCount);

    std::vector<float> lightSelectionPdfs(2, -1.0f);

    // Create light ID map
    std::vector<int> lightIdMap(2);
    for (size_t i = 0; i < 2; ++i) {
        lightIdMap[i] = i;
    }

    IntegratorSample1D sample(SequenceID{}, 2, 0);

    fprintf(stderr, "Sampling tree from camera position...\n");

    tree.sample(lightSelectionPdfs.data(), samplePoint, sampleNormal, nullptr, sample, lightIdMap.data(), 0);

    // With a moderate sampling quality and symmetric light setup,
    // the cost to split the node should be higher than the sampling quality,
    // leading to both lights being chosen, with pdfs of 1 each.
    float pdfLight0 = lightSelectionPdfs[0];
    float pdfLight1 = lightSelectionPdfs[1];
    
    fprintf(stderr, "Light 0 PDF: %f\n", pdfLight0);
    fprintf(stderr, "Light 1 PDF: %f\n", pdfLight1);
    CPPUNIT_ASSERT(scene_rdl2::math::abs(pdfLight0 - pdfLight1) < 0.01f);

    std::cerr << "\nTwo lights test passed\n";
}

void TestLightTree::testSceneReproduction1()
{
    fprintf(stderr, "=============== Testing LightTree Repro 1 (2500 lights, 0.1 sampling threshold)===============\n");
    fprintf(stderr, "This test reproduces the configuration from a scene that caused crashes\n");
    fprintf(stderr, "Original scene had 2450+ DiskLights with light_sampling_quality=0.1\n\n");

    std::vector<std::shared_ptr<DiskLight>> lights;
    std::vector<const Light*> lightPtrs;

    // Recreate a grid pattern similar to a prod scene that caused crashes in the past.
    // The scene had lights spread across a large area at z=4812.94
    // We'll use a smaller subset but with similar spatial distribution
    const float baseZ = 4812.94f;
    const float baseY = 250.0f;
    const int gridSize = 50; // Use 50x50 = 2500 lights
    const float spacing = 40.0f; // spacing between lights

    const float diskRadius = 5.0f;
    const float diskExposure = 20.0f;
    const float diskSpread = 0.7f;

    int lightIndex = 0;
    for (int i = 0; i < gridSize; ++i) {
        for (int j = 0; j < gridSize; ++j) {
            // Compute position
            Vec3f position(0.f, baseY, 0.f);
            position.x = (i - gridSize / 2) * spacing;
            position.z = (j - gridSize / 2) * spacing + baseZ;
            
            // Vary colors like in the original scene
            float hue = (float)(lightIndex % 360) / 360.0f;
            scene_rdl2::math::Color color(0.0f);
            color.r = (lightIndex % 3 == 0) ? 1.0f : hue;
            color.g = (lightIndex % 3 == 1) ? 1.0f : (1.0f - hue);
            color.b = (lightIndex % 3 == 2) ? 1.0f : (hue * 0.5f);
            
            auto light = createDiskLight(mContext, position, diskRadius, color, 
                                         lightIndex, diskExposure, diskSpread);
            lights.push_back(light);
            lightPtrs.push_back(light.get());
            lightIndex++;
        }
    }

    fprintf(stderr, "Created %d disk lights in grid pattern\n", (int)lightPtrs.size());

    // Now test sampling like the original render would
    Vec3f samplePoint(0.0f, 162.313f, 430.887f); // Near camera position from scene
    Vec3f sampleNormal(0.0f, 0.10452846f, 0.9945219f); // Camera direction

    runTest(lightPtrs.data(), lightPtrs.size(), 0.1f, samplePoint, sampleNormal);

    std::cerr << "\nScene reproduction test 1 passed - no crashes with " << lightPtrs.size() 
              << " lights and low sampling threshold\n";
}

void TestLightTree::testSceneReproduction2()
{
    fprintf(stderr, "=============== Testing LightTree Repro 2 (8 sphere lights, quality=0.5) ===============\n");

    std::vector<std::shared_ptr<SphereLight>> lights;
    std::vector<const Light*> lightPtrs;

    const int numLights = 8;
    const float samplingQuality = 0.5f;

    // Sphere light parameters from repro1.rdla (torch lights)
    const float sphereRadius = 2.0f;
    const float sphereExposure = 18.0f;
    const float sphereClearRadius = 10.0f;
    const scene_rdl2::math::Color torchColor(1.0f, 0.65f, 0.16f); // Warm orange/yellow torch color

    // Actual positions from repro1.rdla (first 8 torch lights)
    Vec3f positions[8] = {
        Vec3f(-963.82f, 46.91f, -310.09f),    // lgt_torch_m9_ALL
        Vec3f(-1432.86f, 14.09f, 33.46f),      // lgt_torch_m8_ALL
        Vec3f(-1340.28f, 8.53f, 312.28f),      // lgt_torch_m7_ALL
        Vec3f(-991.85f, 8.53f, 299.44f),       // lgt_torch_m6_ALL
        Vec3f(-963.82f, 8.53f, -310.09f),      // lgt_torch_m5_ALL
        Vec3f(-1432.86f, 8.53f, 33.46f),       // lgt_torch_m4_ALL
        Vec3f(-1340.28f, 46.91f, 312.28f),     // lgt_torch_m3_ALL
        Vec3f(-991.85f, 46.91f, 299.44f)       // lgt_torch_m2_ALL
    };

    int lightIndex = 0;
    for (int i = 0; i < numLights; ++i) {
        auto light = createSphereLight(mContext, positions[i], sphereRadius,
                                      torchColor, lightIndex, 
                                      sphereExposure, sphereClearRadius);
        lightIndex++;
        lights.push_back(light);
        lightPtrs.push_back(light.get());
    }

    Vec3f samplePoint(0.0f, 162.313f, 430.887f); // Near camera position from scene
    Vec3f sampleNormal(0.0f, 0.10452846f, 0.9945219f); // Camera direction

    runTest(lightPtrs.data(), lightPtrs.size(), samplingQuality, samplePoint, sampleNormal);

    fprintf(stderr, "Repro 2 passed: %d sphere lights, sampling quality %.2f\n", numLights, samplingQuality);
}

void TestLightTree::testSceneReproduction3()
{
    fprintf(stderr, "=============== Testing LightTree Repro 3 (450 lights, quality=0.1) ===============\n");
    
    std::vector<std::shared_ptr<DiskLight>> lights;
    std::vector<const Light*> lightPtrs;

    // repro2.rdla: 450 lights with quality 0.1
    const int gridSize = 21; // 21x21 = 441, close to 450
    const float samplingQuality = 0.1f;
    const float spacing = 50.0f;
    const float diskRadius = 5.0f;
    const float diskExposure = 20.0f;
    const float diskSpread = 0.7f;

    int lightIndex = 0;
    for (int i = 0; i < gridSize; ++i) {
        for (int j = 0; j < gridSize; ++j) {
            float x = (i - gridSize / 2) * spacing;
            float z = (j - gridSize / 2) * spacing + 4812.94f;
            float y = 250.0f;
            
            float hue = (float)(lightIndex % 360) / 360.0f;
            auto light = createDiskLight(mContext, Vec3f(x, y, z), diskRadius,
                                        scene_rdl2::math::Color(hue, 0.8f, 1.0f - hue),
                                        lightIndex++, diskExposure, diskSpread);
            lights.push_back(light);
            lightPtrs.push_back(light.get());
        }
    }

    const Vec3f samplePoint(0.0f, 162.313f, 430.887f);
    const Vec3f sampleNormal(0.0f, 0.10452846f, 0.9945219f);

    runTest(lightPtrs.data(), lightPtrs.size(), samplingQuality, samplePoint, sampleNormal);

    fprintf(stderr, "Repro 3 passed: %d lights, sampling quality %.2f\n", (int)lightPtrs.size(), samplingQuality);
}

void TestLightTree::testSceneReproduction4()
{
    fprintf(stderr, "=============== Testing LightTree Repro 4 (450 lights, quality=0.01) ===============\n");
    
    std::vector<std::shared_ptr<DiskLight>> lights;
    std::vector<const Light*> lightPtrs;
    
    // repro3.rdla: 450 lights with very low quality 0.01
    const int gridSize = 21; // 21x21 = 441, close to 450
    const float samplingQuality = 0.01f; // Very low - triggers more splitting
    const float spacing = 50.0f;

    const float diskRadius = 5.0f;
    const float diskExposure = 20.0f;
    const float diskSpread = 0.7f;
    
    int lightIndex = 0;
    for (int i = 0; i < gridSize; ++i) {
        for (int j = 0; j < gridSize; ++j) {
            float x = (i - gridSize / 2) * spacing;
            float z = (j - gridSize / 2) * spacing + 4812.94f;
            float y = 250.0f;
            
            float hue = (float)(lightIndex % 180) / 180.0f;
            auto light = createDiskLight(mContext, Vec3f(x, y, z), diskRadius,
                                        scene_rdl2::math::Color(1.0f - hue, hue, 0.5f),
                                        lightIndex++, diskExposure, diskSpread);
            lights.push_back(light);
            lightPtrs.push_back(light.get());
        }
    }

    const Vec3f samplePoint(0.0f, 162.313f, 430.887f);
    const Vec3f sampleNormal(0.0f, 0.10452846f, 0.9945219f);
    
    runTest(lightPtrs.data(), lightPtrs.size(), samplingQuality, samplePoint, sampleNormal);
    
    fprintf(stderr, "Repro 4 passed: %d lights, sampling quality %.3f (very aggressive splitting)\n", 
            (int)lightPtrs.size(), samplingQuality);
}

void TestLightTree::testSceneReproduction5()
{
    fprintf(stderr, "=============== Testing LightTree Repro 5 (6 lights, quality=0.001) ===============\n");

    std::vector<std::shared_ptr<DiskLight>> lights;
    std::vector<const Light*> lightPtrs;

    // repro4.rdla: 6 lights with extremely low quality 0.001
    const int numLights = 6;
    const float samplingQuality = 0.001f; // Extremely low - maximum splitting

    const float diskRadius = 5.0f;
    const float diskExposure = 20.0f;
    const float diskSpread = 0.7f;

    int lightIndex = 0;
    for (int i = 0; i < numLights; ++i) {
        float x = (i % 2) * 200.0f - 100.0f;
        float z = (i / 2) * 200.0f + 4812.94f;
        float y = 250.0f;
        
        auto light = createDiskLight(mContext, Vec3f(x, y, z), diskRadius,
                                    scene_rdl2::math::Color(1.0f, 0.5f, (float)i / numLights),
                                    lightIndex++, diskExposure, diskSpread);
        lights.push_back(light);
        lightPtrs.push_back(light.get());
    }

    const Vec3f samplePoint(0.0f, 162.313f, 430.887f);
    const Vec3f sampleNormal(0.0f, 0.10452846f, 0.9945219f);

    runTest(lightPtrs.data(), lightPtrs.size(), samplingQuality, samplePoint, sampleNormal);

    fprintf(stderr, "Repro 5 passed: %d lights, sampling quality %.4f (extreme splitting)\n", 
            numLights, samplingQuality);
}

void TestLightTree::testSceneReproduction6()
{
    fprintf(stderr, "=============== Testing LightTree Repro 6 (167 lights: 4 spots + 163 spheres, quality=0.5) ===============\n");

    std::vector<std::shared_ptr<Light>> lights;
    std::vector<const Light*> lightPtrs;
    
    // repro5.rdla: Mix of 4 SpotLights and 163 SphereLights with quality 0.5
    // This reproduces the crash scenario with mixed light types
    const float samplingQuality = 0.5f;

    int lightIndex = 0;

    // Add 4 SpotLights with parameters from repro5.rdla
    {
        // SpotLight 1: lgt_house_rim_ALL
        auto spot1 = createSpotLight(mContext,
            Vec3f(1145.64f, 688.38f, -881.53f),   // position from node_xform
            Vec3f(-0.772f, 0.153f, -0.617f),       // direction (third column of rotation)
            scene_rdl2::math::Color(1.0f, 0.79f, 0.124f),
            lightIndex, 25.0f, 60.0f, 10.0f, 1000.0f);
        lightIndex++;
        lights.push_back(spot1);
        lightPtrs.push_back(spot1.get());

        // SpotLight 2: lgt_dappleBloom
        auto spot2 = createSpotLight(mContext,
            Vec3f(0.0f, 400.0f, 0.0f),
            Vec3f(0.0f, -1.0f, 0.1f),
            scene_rdl2::math::Color(1.0f, 0.443f, 0.058f),
            lightIndex, 25.0f, 60.0f, 2.0f, 0.0f);
        lightIndex++;
        lights.push_back(spot2);
        lightPtrs.push_back(spot2.get());

        // SpotLight 3: lgt_dapple1
        auto spot3 = createSpotLight(mContext,
            Vec3f(200.0f, 500.0f, -100.0f),
            Vec3f(-0.2f, -0.9f, 0.1f),
            scene_rdl2::math::Color(1.0f, 0.734f, 0.139f),
            lightIndex, 26.0f, 60.0f, 2.0f, 0.0f);
        lightIndex++;
        lights.push_back(spot3);
        lightPtrs.push_back(spot3.get());

        // SpotLight 4: lgt_dapple
        auto spot4 = createSpotLight(mContext,
            Vec3f(-200.0f, 450.0f, 100.0f),
            Vec3f(0.1f, -0.95f, -0.05f),
            scene_rdl2::math::Color(1.0f, 0.734f, 0.139f),
            lightIndex, 26.0f, 60.0f, 2.0f, 0.0f);
        lightIndex++;
        lights.push_back(spot4);
        lightPtrs.push_back(spot4.get());
    }

    // Add 163 SphereLights (string lights) with parameters from repro5.rdla
    const float sphereRadius = 1.0f;
    const float sphereExposure = 13.0f;
    const float sphereClearRadius = 3.0f;
    const scene_rdl2::math::Color stringColor(1.0f, 0.529f, 0.204f);

    // Create a dense distribution of sphere lights to simulate string lights
    const int numSphereLights = 163;
    for (int i = 0; i < numSphereLights; ++i) {
        // Distribute lights in a complex pattern
        float angle = (float)i * 2.3f;  // Prime-ish multiplier for non-uniform distribution
        float radius = 100.0f + (i % 20) * 15.0f;
        float height = 30.0f + (i % 10) * 5.0f;
        
        Vec3f position(
            radius * std::cos(angle) - 200.0f,
            height,
            radius * std::sin(angle) + 50.0f
        );
        
        auto light = createSphereLight(mContext, position, sphereRadius,
                                      stringColor, lightIndex,
                                      sphereExposure, sphereClearRadius);
        lightIndex++;
        lights.push_back(light);
        lightPtrs.push_back(light.get());
    }

    const Vec3f samplePoint(0.0f, 162.313f, 430.887f);
    const Vec3f sampleNormal(0.0f, 0.10452846f, 0.9945219f);

    fprintf(stderr, "Building tree with %zu total lights (4 spots + %d spheres)...\n", 
            lightPtrs.size(), numSphereLights);

    runTest(lightPtrs.data(), lightPtrs.size(), samplingQuality, samplePoint, sampleNormal);

    fprintf(stderr, "Repro 6 passed: %zu lights (mixed types), sampling quality %.2f\n", 
            lightPtrs.size(), samplingQuality);
}

}
}
CPPUNIT_TEST_SUITE_REGISTRATION(moonray::pbr::TestLightTree);
