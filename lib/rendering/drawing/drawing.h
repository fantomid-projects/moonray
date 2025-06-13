// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace moonray {
namespace drawing {

void drawLineWu(int inX0, int inY0, int inX1, int inY1, int lineWidth, 
                const std::function<void(int, int, float)>& writeToBuffer)
{
    int dx = abs(inX1 - inX0);          // change in x
    int dy = abs(inY1 - inY0);          // change in y

    if (dx == 0 && dy == 0) { return; }
    
    int sx = inX0 < inX1 ? 1 : -1;      // step in the x-direction
    int sy = inY0 < inY1 ? 1 : -1;      // step in the y-direction

    bool steep = false;                 // is the slope > 1 ?
    int x0 = inX0;                      // starting point x
    int x1 = inX1;                      // ending point x
    int y0 = inY0;                      // starting point y
    int y1 = inY1;                      // ending point y
    float slope;                        // slope/gradient of line  

    // We always want to be drawing a line with
    // a gradual slope, so if it's a steep line, swap the x and y
    // coordinates so we can draw a line with slope < 1
    if (dy > dx) {
        steep = true;
        slope = (float) dx / dy;
        std::swap(x0, y0);
        std::swap(x1, y1);
        std::swap(sx, sy);
    } else {
        slope = (float) dy / dx; 
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
    int halfWidthInt = lineWidth * 0.5;

    float yIntersect = y0;
    // This is a gradually increasing line, so
    // we always increase in x, and conditionally increase in y
    for (int x = x0; x != x1; x += sx) {

        // find the integer part of yIntersect
        // which is the new y coordinate
        int yIntersectInt = (int) yIntersect;

        // find the fractional part of yIntersect
        // which represents how far off the line we are,
        // and helps us calculate the alpha value
        float yIntersectFract = yIntersect - yIntersectInt;

        // Create width by extending on either side in the y-direction
        int minY = yIntersectInt - halfWidthInt;
        int maxY = yIntersectInt + (lineWidth - halfWidthInt);
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

void drawSquare(int px, int py, int width, const std::function<void(int, int, float)>& writeToBuffer,
                bool filled = true, int lineWidth = 1)
{
    int halfWidth = width / 2;
    int minX = px - halfWidth;
    int maxX = px + halfWidth;
    int minY = py - halfWidth;
    int maxY = py + halfWidth;
    
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

} // end namespace drawing
} // end namespace moonray