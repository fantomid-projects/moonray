// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <scene_rdl2/common/grid_util/Arg.h>
#include <scene_rdl2/common/grid_util/Parser.h>
#include <scene_rdl2/common/grid_util/VectorPacket.h>

#include <memory>
#include <string>

namespace moonray {

namespace rndr {
    class RenderContext;
}

namespace engine_tool {

using Vec2ui = scene_rdl2::math::Vec2<unsigned>; 
using Vec2f = scene_rdl2::math::Vec2f;
using Vec3uc = scene_rdl2::math::Vec3<unsigned char>;

class McrtVecSenderTestData
//
// VecSender's test data generation base class for testing
// All the test data generation classes would be better off inheriting this base class
// to share the underlying helper functions.
//
{
public:
    using DrawLineFunc = std::function<void(const Vec2f& s,
                                            const Vec2f& e,
                                            const float w,
                                            const Vec3uc& c,
                                            const unsigned char a)>;
    using DrawBoxFunc = std::function<void(const Vec2ui& min,
                                           const Vec2ui& max,
                                           const Vec3uc& c,
                                           const unsigned char a)>;

    virtual ~McrtVecSenderTestData() {};

protected:
    // Calculate the best window divide count for the multi-machine total count
    unsigned calcWinDivCount(const unsigned machinetotal) const;

    // Calculate the sub-windows position (min, max) based on the winDivCount and subWinId.
    void calcSubdivWindowPos(const unsigned width,
                             const unsigned height,
                             const unsigned winDivCount,
                             const unsigned subWinId,
                             const float activeRatioXMin, const float activeRatioXMax,
                             const float activeRatioYMin, const float activeRatioYMax,
                             Vec2ui& subWinMin,
                             Vec2ui& subWinMax) const;

    // Draw spinner test pattern based on the mDrawCounter value.
    // mDrawCounter is automatically incremented if this function is called.
    void genTestPatternA(const Vec2ui& winMin,
                         const Vec2ui& winMax,
                         const unsigned segmentTotal,
                         const float angleStart,
                         const float angleEnd,
                         const float lenScale,
                         const float widthMax,
                         const bool constLen,
                         const bool constWidth,
                         const DrawLineFunc& drawLineFunc);

    // Draw a fade-out outline box pattern based on the mDrawCounter.
    void genTestPatternB(const Vec2ui& winMin,
                         const Vec2ui& winMax,
                         const unsigned winDivCount,
                         const DrawBoxFunc& drawBoxFunc);

    //------------------------------

    unsigned mDrawCounter {0};
};

class McrtVecSenderTestData1 : public McrtVecSenderTestData
//
// Generate spinner test pattern
//
{
public:
    using Arg = scene_rdl2::grid_util::Arg;
    using Parser = scene_rdl2::grid_util::Parser;

    McrtVecSenderTestData1()
    {
        parserConfigure();
    }
    ~McrtVecSenderTestData1() {}

    void setMachineTotalAndId(const unsigned machineTotal, const unsigned machineId)
    {
        mMachineTotal = machineTotal;
        mMachineId = machineId;
    }

    void drawingLines(const unsigned width,
                      const unsigned height,
                      const DrawLineFunc& drawLineFunc);

    Parser& getParser() { return mParser; }

    std::string show() const;

private:
    void parserConfigure();

    //------------------------------

    unsigned mMachineTotal {1};
    unsigned mMachineId {0};

    float mActiveRatioXMin {0.05f};
    float mActiveRatioXMax {0.95f};
    float mActiveRatioYMin {0.05f};
    float mActiveRatioYMax {0.9f};

    unsigned mSegmentTotal {60};
    float mWidthMax {3.0f};

    Parser mParser;
};

class McrtVecSenderTestData2 : public McrtVecSenderTestData
//
// Generate fade-out outline box test pattern
//
{
public:
    using Arg = scene_rdl2::grid_util::Arg;
    using Parser = scene_rdl2::grid_util::Parser;
    using DrawBoxFunc = std::function<void(const Vec2ui& min,
                                           const Vec2ui& max,
                                           const Vec3uc& c,
                                           const unsigned char a)>;

    McrtVecSenderTestData2()
    {
        parserConfigure();
    }
    ~McrtVecSenderTestData2() {}

    void setMachineTotalAndId(const unsigned machineTotal, const unsigned machineId)
    {
        mMachineTotal = machineTotal;
        mMachineId = machineId;
    }

    void drawingBoxOutline(const unsigned width,
                           const unsigned height,
                           const DrawBoxFunc& drawBoxFunc);

    Parser& getParser() { return mParser; }

    std::string show() const;

private:
    void parserConfigure();

    //------------------------------
    
    unsigned mMachineTotal {1};
    unsigned mMachineId {0};

    unsigned mWinDivCount {16};

    float mActiveRatioXMin {0.0f};
    float mActiveRatioXMax {1.0f};
    float mActiveRatioYMin {0.0f};
    float mActiveRatioYMax {0.94f};

    Parser mParser;
};

class WinClip
//
// 2D line segment window clipping
//
{
public:
    static bool
    clipLine(Vec2f& a, Vec2f& b, const float w, const float h)
    // a: line start position
    // b: line end position
    // w: clipping window width
    // h: clipping window height
    {
        unsigned codeA = codeGen(a, w, h);
        unsigned codeB = codeGen(b, w, h);

        while (true) {
            if ((codeA | codeB) == 0) return true; // inside
            if ((codeA & codeB) != 0) return false; // outside

            int codeOut = codeA ? codeA : codeB;
            float x, y;
            if (codeOut & 0x8) { // top
                x = a[0] + (b[0] - a[0]) * (h - a[1]) / (b[1] - a[1]);
                y = h;
            } else if (codeOut & 0x4) { // bottom
                x = a[0] + (b[0] - a[0]) * (0.0f - a[1]) / (b[1] - a[1]);
                y = 0.0f;
            } else if (codeOut & 0x2) { // right
                x = w;
                y = a[1] + (b[1] - a[1]) * (w - a[0]) / (b[0] - a[0]);
            } else if (codeOut & 0x1) { // left
                x = 0.0f; 
                y = a[1] + (b[1] - a[1]) * (0.0f - a[0]) / (b[0] - a[0]);
            }

            if (codeOut == codeA) {
                a[0] = x;
                a[1] = y;
                codeA = codeGen(a, w, h);
            } else {
                b[0] = x;
                b[1] = y;
                codeB = codeGen(b, w, h);
            }
        }
    }

    static Vec2ui
    vec2fToVec2ui(const Vec2f& v, const float w, const float h)
    {
        float x = v[0];
        if (x < 0.0f) {
            x = 0.0f;
        } else if (x > (w - 1.0f)) {
            x = w - 1.0f; 
        }
        float y = v[1];
        if (y < 0.0f) {
            y = 0.0f;
        } else if (y > (h - 1.0f)) {
            y = h - 1.0f;
        }
        return Vec2ui(static_cast<unsigned>(x), static_cast<unsigned>(y));
    }

private:
    static unsigned codeGen(const Vec2f& p, const float w, const float h)
    {
        // The code represents a bit flag that stores whether a line intersects the left, right, bottom,
        // or top boundary.
        unsigned code = 0x0;
        if (p[0] < 0.0f) code |= 0x1; // left
        if (p[0] > w)    code |= 0x2; // right
        if (p[1] < 0.0f) code |= 0x4; // bottom
        if (p[1] > h)    code |= 0x8; // top
        return code;
    }
}; // WinClip

class McrtVecSender
//
// Snapshot PathVisualizer line drawing data, also internal parameters, and convert them to
// the vecPacket binary data for the progressiveFrame message.
//
{
public:
    using Arg = scene_rdl2::grid_util::Arg;
    using DataPtr = std::shared_ptr<uint8_t>;
    using MessageAddBuffFunc = std::function<void(const DataPtr& data, const size_t dataSize, const char* buffName)>;
    using MsgFunc = std::function<bool(const std::string& msg)>;
    using Parser = scene_rdl2::grid_util::Parser;
    using Vec2ui = scene_rdl2::grid_util::VectorPacketEnqueue::Vec2ui;
    using Vec4uc = scene_rdl2::grid_util::VectorPacketEnqueue::Vec4uc;

    McrtVecSender();

    void setMachineTotalAndId(const unsigned machineTotal, const unsigned machineId)
    {
        mMachineTotal = machineTotal;
        mMachineId = machineId;
        mTestData1.setMachineTotalAndId(mMachineTotal, mMachineId);
        mTestData2.setMachineTotalAndId(mMachineTotal, mMachineId);        
    }
 
    size_t snapshotDrawing(const unsigned width,
                           const unsigned height,
                           const unsigned renderCounter, // start from 1 and increment. never go back to 0
                           const unsigned snapshotId,
                           const rndr::RenderContext& renderContext,
                           const bool needToSendSimGlobalInfo);
    size_t snapshotPathVisualizerDisable();
    
    void addVecPacketToProgressiveFrame(const MessageAddBuffFunc& func) const;

    bool compareDictPathVisActive(const bool flag) const;

    Parser& getParser() { return mParser; }

    std::string show() const;

private:
    using Vec2uc = scene_rdl2::math::Vec2<unsigned char>;
    using Vec3uc = scene_rdl2::math::Vec3<unsigned char>;
    using Vec2f = scene_rdl2::math::Vec2f;
    using DrawLineFunc = McrtVecSenderTestData::DrawLineFunc;
    
    void setup();
    
    void enqCol(const Vec3uc& c, const unsigned char a)
    {
        const Vec4uc currC(c[0], c[1], c[2], a);
        if (mForcedOutputCol || currC != mCurrCol) {
            //
            // We only encode the color data if it is updated
            //
            mCurrCol = currC;
            mVecPacketEnqueue->enqRgba(mCurrCol);
            mForcedOutputCol = false;
        }
    }
    void enqWidth16(const float width)
    {
        const unsigned w16 = std::max(1U, static_cast<unsigned>(width * 16.0f));
        if (mForcedOutputWidth || mCurrWidth16 != w16) {
            //
            // We only encode the width data if it is updated
            //
            mCurrWidth16 = w16;
            mVecPacketEnqueue->enqWidth16(width);
            mForcedOutputWidth = false;
        }
    }

    size_t snapshotTestDrawing(const unsigned width, const unsigned height);
    void snapshotTestDrawingLinesSimple(const DrawLineFunc& drawLineFunc);
    size_t snapshotPathVisualizerEnable(const unsigned width,
                                        const unsigned height,
                                        const unsigned renderCounter,
                                        const rndr::RenderContext& renderContext,
                                        const bool needToSendSimGlobalInfo);
    void snapshotPathVisualizerSimGlobalInfo(const unsigned renderCounter,
                                             const rndr::RenderContext& renderContext);
    void snapshotPathVisualizerNodeData(const rndr::RenderContext& renderContext);

    uint8_t* duplicateVecPacket() const
    {
        uint8_t* const data = new uint8_t[mBuff.size()];
        std::memcpy(data, mBuff.data(), mBuff.size());
        return data;
    }
    DataPtr makeSharedPtr(uint8_t *data) const { return DataPtr(data, std::default_delete<uint8_t[]>()); }

    std::string showBuff() const;
    std::string showVecPacketHeader() const;
    std::string showVecPacketEnqueue() const;

    void parserConfigure();
    bool testRunSnapshot(const bool dumpResult, const MsgFunc& msgCallBack);

    //------------------------------

    unsigned mMachineTotal {1};
    unsigned mMachineId {0};

    bool mTestMode {false};
    unsigned mTestType {0};

    McrtVecSenderTestData1 mTestData1;
    McrtVecSenderTestData2 mTestData2;

    unsigned mRenderCounterLastSnapshot {0};

    std::string mBuff; // vector packet data
    std::unique_ptr<scene_rdl2::grid_util::VectorPacketHeader> mVecPacketHeader;
    std::unique_ptr<scene_rdl2::grid_util::VectorPacketEnqueue> mVecPacketEnqueue;

    //
    // When transferring a large number of line data entries, each line includes not only positional
    // information but also color and width data. However, writing out color and width for every
    // single line is not an efficient approach, because many lines often share the same color and
    // width values. Therefore, the basic strategy is to output the line color and width only when
    // they change, and have all subsequent lines share those values. This reduces redundancy and
    // significantly decreases the overall data packet size, since color and width are not repeatedly
    // written for each line.
    // The mForceOutput* flags control this behavior:
    // - When the flag is false, the logic described above is applied (output only when a change occurs).
	// - When the flag is true, color and width are written for every line, regardless of change.
    // These flags are initially set to true at the start of a new render frame, ensuring that the first
    // color and width values are always written unconditionally. Once a value has been output even once,
    // the flags are then set to false.
    //
    bool mForcedOutputCol {false};
    bool mForcedOutputWidth {false};

    Vec4uc mCurrCol {0, 0, 0, 0};
    unsigned mCurrWidth16 {0};

    Parser mParser;
}; // McrtVecSender

} // namespace engine_tool
} // namespace moonray
