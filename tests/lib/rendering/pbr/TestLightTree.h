// Copyright 2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <moonray/rendering/pbr/light/LightTreeUtil.h>

#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/TestFixture.h>

namespace moonray {
namespace pbr {

//----------------------------------------------------------------------------

class TestLightTree : public CppUnit::TestFixture
{
    CPPUNIT_TEST_SUITE(TestLightTree);
#if 1
    CPPUNIT_TEST(testCone);
    CPPUNIT_TEST(testTwoLights);
    CPPUNIT_TEST(testSceneReproduction1);
    CPPUNIT_TEST(testSceneReproduction2);
    CPPUNIT_TEST(testSceneReproduction3);
    CPPUNIT_TEST(testSceneReproduction4);
    CPPUNIT_TEST(testSceneReproduction5);
    CPPUNIT_TEST(testSceneReproduction6);
#endif
    CPPUNIT_TEST_SUITE_END();

public:
    void setUp();
    void tearDown();
    
    void testCone();
    void testTwoLights();
    void testSceneReproduction1();
    void testSceneReproduction2();
    void testSceneReproduction3();
    void testSceneReproduction4();
    void testSceneReproduction5();
    void testSceneReproduction6();

private:
    scene_rdl2::rdl2::SceneContext* mContext;
};

//----------------------------------------------------------------------------

} // namespace pbr
} // namespace moonray

