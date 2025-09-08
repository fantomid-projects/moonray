// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace moonray {
namespace drawing {

using scene_rdl2::math::Vec2f;
using scene_rdl2::math::min;
using scene_rdl2::math::max;
using scene_rdl2::math::dot;
using scene_rdl2::math::length;

void drawLineWu(const int inX0, const int inY0, 
                const int inX1, const int inY1, 
                const unsigned int lineWidth, 
                const std::function<void(int, int, float)>& writeToBuffer)
{
    const int dx = abs(inX1 - inX0);             // change in x
    const int dy = abs(inY1 - inY0);             // change in y

    if (dx == 0 && dy == 0) { return; }
    
    int sx = inX0 < inX1 ? 1 : -1;         // step in the x-direction
    int sy = inY0 < inY1 ? 1 : -1;         // step in the y-direction

    bool steep = false;                          // is the slope > 1 ?
    int x0 = inX0;                               // starting point x
    int x1 = inX1;                               // ending point x
    int y0 = inY0;                               // starting point y
    int y1 = inY1;                               // ending point y
    
    // Certain values need to be doubles, since using a float leads to precision
    // issues, which causes the line to sometimes stop one pixel short
    double slope;
    
    // We always want to be drawing a line with
    // a gradual slope, so if it's a steep line, swap the x and y
    // coordinates so we can draw a line with slope < 1
    if (dy > dx) {
        steep = true;
        slope = (double) dx / dy;
        std::swap(x0, y0);
        std::swap(x1, y1);
        std::swap(sx, sy);
    } else {
        slope = (double) dy / dx; 
    }

    // Create a function to write to the buffer
    auto write = [&] (int x, int y, float a) {
        // if slope is steep, must swap x and y back before writing
        if (steep) { 
            std::swap(x, y); 
        }
        writeToBuffer(x, y, a);
    };
    
    // The integer part of half the line width
    const unsigned int halfWidthInt = lineWidth == 1 ? 0 : lineWidth * 0.5;

    double yIntersect = y0;
    // This is a gradually increasing line, so
    // we always increase in x, and conditionally increase in y
    for (int x = x0; x != x1+sx; x += sx) {

        // find the integer part of yIntersect
        // which is the new y coordinate
        const int yIntersectInt = (int) yIntersect;

        // find the fractional part of yIntersect
        // which represents how far off the line we are,
        // and helps us calculate the alpha value
        const double yIntersectFract = yIntersect - yIntersectInt;

        // Create width by extending on either side in the y-direction
        const int minY = yIntersectInt - halfWidthInt;
        const int maxY = yIntersectInt + (lineWidth - halfWidthInt);
        for (int y = minY; y <= maxY; ++y) {
            if (y == minY) {
                // lowest, anti-aliased pixel
                write(x, y, 1.f - yIntersectFract);
            } else if (y == maxY) {
                // highest, anti-aliased pixel
                write(x, y, yIntersectFract);
            } else {
                // any middle pixels don't factor in AA
                write(x, y, 1.f);
            }
        }   
        yIntersect += slope * sy;
    }
}

void drawSquare(const int px, const int py, const unsigned int width, 
                const std::function<void(int, int, float)>& writeToBuffer,
                const bool filled = true, const unsigned int lineWidth = 1)
{
    const unsigned int halfWidth = width / 2;
    const int minX = px - halfWidth;
    const int maxX = px + halfWidth;
    const int minY = py - halfWidth;
    const int maxY = py + halfWidth;

    if (filled) {
        for (int row = minY; row < maxY; ++row) {
            drawLineWu(minX, row, maxX, row, 1, writeToBuffer);
        }
    } else {
        drawLineWu(minX, minY, maxX, minY, lineWidth, writeToBuffer);
        drawLineWu(maxX, minY, maxX, maxY, lineWidth, writeToBuffer);
        drawLineWu(maxX, maxY, minX, maxY, lineWidth, writeToBuffer);
        drawLineWu(minX, maxY, minX, minY, lineWidth, writeToBuffer);
    }
}

void drawCircle(const int px, const int py, const int r, 
                const std::function<void(int, int, float)>& writeToBuffer)
{
    /// TODO: We should explore optimizations, especially for large circles,
    /// such as Midpoint circle algorithm (Bresenham's circle algorithm)
    /// https://en.wikipedia.org/wiki/Midpoint_circle_algorithm
    /// We could also look into restricting the active region using distance^2 
    /// compared to (r + 1)^2, then calculate the alpha if it's 
    /// inside a radius of r+1. We could also leverage the symmetry of the circle
    /// to reduce the number of pixels we need to calculate.

    // For each pixel in the square bound by the radius,
    // calculate the circle's sdf and use it to determine the transparency
    for (int y = -r+1; y <= r-1; ++y) {
        for (int x = -r+1; x <= r-1; ++x) {
            const float sdf = scene_rdl2::math::sqrt(x*x + (float) y*y) - (r-1);
            const float a = sdf <= 0.f ? 1.f : max(1.f - sdf, 0.f);

            if (a > 0.f) {
                writeToBuffer(px + x, py + y, a);
            }
        }
    }
}

} // end namespace drawing
} // end namespace moonray