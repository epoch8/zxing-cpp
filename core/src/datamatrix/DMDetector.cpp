/*
* Copyright 2016 Nu-book Inc.
* Copyright 2016 ZXing authors
* Copyright 2017 Axel Waggershauser
*/
// SPDX-License-Identifier: Apache-2.0



#include "DMDetector.h"

#include "DMDecoder.h"
#include "DecoderResult.h"
#include "GridSampler.h"

#include "BitMatrix.h"
#include "BitMatrixCursor.h"
#include "ByteMatrix.h"
#include "DetectorResult.h"
#include "GridSampler.h"
#include "LogMatrix.h"
#include "Point.h"
#include "RegressionLine.h"
#include "ResultPoint.h"
#include "Scope.h"
#include "WhiteRectDetector.h"
#include "ReadBarcode.h"

#include <string>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <map>
#include <utility>
#include <vector>
#include <iostream>

#undef min
#undef max

#ifndef PRINT_DEBUG
#define printf(...){}
#define printv(...){}
#else
#define printv(fmt, vec) \
for (auto v : vec) \
	printf(fmt, v); \
printf("\n");
#endif

namespace ZXing::DataMatrix {

/**
* The following code is the 'old' code by Sean Owen based on the Java upstream project.
* It looks for a white rectangle, then cuts the corners until it hits a black pixel, which
* results in 4 corner points. Then it determines the dimension by counting transitions
* between the upper and right corners and samples the grid.
* This code has several limitations compared to the new code below but has one advantage:
* it works on high resolution scans with noisy/rippled black/white-edges and potentially
* on partly occluded locator patterns (the surrounding border of modules/pixels). It is
* therefore kept as a fall-back.
*/

/**
* Simply encapsulates two points and a number of transitions between them.
*/
    struct ResultPointsAndTransitions
    {
        const ResultPoint* from;
        const ResultPoint* to;
        int transitions;
    };




/**
* Counts the number of black/white transitions between two points, using something like Bresenham's algorithm.
*/
    static ResultPointsAndTransitions TransitionsBetween(const BitMatrix& image, const ResultPoint& from,
                                                         const ResultPoint& to)
    {
        // See QR Code Detector, sizeOfBlackWhiteBlackRun()
        int fromX = static_cast<int>(from.x());
        int fromY = static_cast<int>(from.y());
        int toX = static_cast<int>(to.x());
        int toY = static_cast<int>(to.y());
        bool steep = std::abs(toY - fromY) > std::abs(toX - fromX);
        if (steep) {
            std::swap(fromX, fromY);
            std::swap(toX, toY);
        }

        int dx = std::abs(toX - fromX);
        int dy = std::abs(toY - fromY);
        int error = -dx / 2;
        int ystep = fromY < toY ? 1 : -1;
        int xstep = fromX < toX ? 1 : -1;
        int transitions = 0;
        bool inBlack = image.get(steep ? fromY : fromX, steep ? fromX : fromY);
        for (int x = fromX, y = fromY; x != toX; x += xstep) {
            bool isBlack = image.get(steep ? y : x, steep ? x : y);
            if (isBlack != inBlack) {
                transitions++;
                inBlack = isBlack;
            }
            error += dy;
            if (error > 0) {
                if (y == toY) {
                    break;
                }
                y += ystep;
                error -= dx;
            }
        }
        return ResultPointsAndTransitions{ &from, &to, transitions };
    }

    static bool IsValidPoint(const ResultPoint& p, int imgWidth, int imgHeight)
    {
        return p.x() >= 0 && p.x() < imgWidth && p.y() > 0 && p.y() < imgHeight;
    }

    template <typename T>
    static float RoundToNearestF(T x)
    {
        return static_cast<float>(std::round(x));
    }

/**
* Calculates the position of the white top right module using the output of the rectangle detector
* for a rectangular matrix
*/
    static bool CorrectTopRightRectangular(const BitMatrix& image, const ResultPoint& bottomLeft,
                                           const ResultPoint& bottomRight, const ResultPoint& topLeft,
                                           const ResultPoint& topRight, int dimensionTop, int dimensionRight,
                                           ResultPoint& result)
    {
        float corr = RoundToNearestF(distance(bottomLeft, bottomRight)) / static_cast<float>(dimensionTop);
        float norm = RoundToNearestF(distance(topLeft, topRight));
        float cos = (topRight.x() - topLeft.x()) / norm;
        float sin = (topRight.y() - topLeft.y()) / norm;

        ResultPoint c1(topRight.x() + corr*cos, topRight.y() + corr*sin);

        corr = RoundToNearestF(distance(bottomLeft, topLeft)) / (float)dimensionRight;
        norm = RoundToNearestF(distance(bottomRight, topRight));
        cos = (topRight.x() - bottomRight.x()) / norm;
        sin = (topRight.y() - bottomRight.y()) / norm;

        ResultPoint c2(topRight.x() + corr*cos, topRight.y() + corr*sin);

        if (!IsValidPoint(c1, image.width(), image.height())) {
            if (IsValidPoint(c2, image.width(), image.height())) {
                result = c2;
                return true;
            }
            return false;
        }
        if (!IsValidPoint(c2, image.width(), image.height())) {
            result = c1;
            return true;
        }

        int l1 = std::abs(dimensionTop - TransitionsBetween(image, topLeft, c1).transitions) +
                 std::abs(dimensionRight - TransitionsBetween(image, bottomRight, c1).transitions);
        int l2 = std::abs(dimensionTop - TransitionsBetween(image, topLeft, c2).transitions) +
                 std::abs(dimensionRight - TransitionsBetween(image, bottomRight, c2).transitions);

        result = l1 <= l2 ? c1 : c2;
        return true;
    }

/**
* Calculates the position of the white top right module using the output of the rectangle detector
* for a square matrix
*/
    static ResultPoint CorrectTopRight(const BitMatrix& image, const ResultPoint& bottomLeft, const ResultPoint& bottomRight,
                                       const ResultPoint& topLeft, const ResultPoint& topRight, int dimension)
    {
        float corr = RoundToNearestF(distance(bottomLeft, bottomRight)) / (float)dimension;
        float norm = RoundToNearestF(distance(topLeft, topRight));
        float cos = (topRight.x() - topLeft.x()) / norm;
        float sin = (topRight.y() - topLeft.y()) / norm;

        ResultPoint c1(topRight.x() + corr * cos, topRight.y() + corr * sin);

        corr = RoundToNearestF(distance(bottomLeft, topLeft)) / (float)dimension;
        norm = RoundToNearestF(distance(bottomRight, topRight));
        cos = (topRight.x() - bottomRight.x()) / norm;
        sin = (topRight.y() - bottomRight.y()) / norm;

        ResultPoint c2(topRight.x() + corr * cos, topRight.y() + corr * sin);

        if (!IsValidPoint(c1, image.width(), image.height())) {
            if (!IsValidPoint(c2, image.width(), image.height()))
                return topRight;
            return c2;
        }
        if (!IsValidPoint(c2, image.width(), image.height()))
            return c1;

        int l1 = std::abs(TransitionsBetween(image, topLeft, c1).transitions -
                          TransitionsBetween(image, bottomRight, c1).transitions);
        int l2 = std::abs(TransitionsBetween(image, topLeft, c2).transitions -
                          TransitionsBetween(image, bottomRight, c2).transitions);
        return l1 <= l2 ? c1 : c2;
    }

    static DetectorResult SampleGrid(const BitMatrix& image, const ResultPoint& topLeft, const ResultPoint& bottomLeft,
                                     const ResultPoint& bottomRight, const ResultPoint& topRight, int width, int height)
    {
        return SampleGrid(image, width, height,
                          {Rectangle(width, height, 0.5), {topLeft, topRight, bottomRight, bottomLeft}});
    }

    static DetectorResult SampleGridWarped(const BitMatrix& image, const ResultPoint& topLeft, const ResultPoint& bottomLeft,
                                           const ResultPoint& bottomRight, const ResultPoint& topRight, int width, int height, const Warp& warp)
    {
        return SampleGridWarped(image, width, height, warp,
                                {Rectangle(width, height, 0.5), {topLeft, topRight, bottomRight, bottomLeft}});
    }

/**
* Returns the z component of the cross product between vectors BC and BA.
*/
    static float CrossProductZ(const ResultPoint& a, const ResultPoint& b, const ResultPoint& c)
    {
        return (c.x() - b.x())*(a.y() - b.y()) - (c.y() - b.y())*(a.x() - b.x());
    }

/**
* Orders an array of three ResultPoints in an order [A,B,C] such that AB is less than AC
* and BC is less than AC, and the angle between BC and BA is less than 180 degrees.
*/
    static void OrderByBestPatterns(const ResultPoint*& p0, const ResultPoint*& p1, const ResultPoint*& p2)
    {
        // Find distances between pattern centers
        auto zeroOneDistance = distance(*p0, *p1);
        auto oneTwoDistance = distance(*p1, *p2);
        auto zeroTwoDistance = distance(*p0, *p2);

        const ResultPoint* pointA;
        const ResultPoint* pointB;
        const ResultPoint* pointC;
        // Assume one closest to other two is B; A and C will just be guesses at first
        if (oneTwoDistance >= zeroOneDistance && oneTwoDistance >= zeroTwoDistance) {
            pointB = p0;
            pointA = p1;
            pointC = p2;
        }
        else if (zeroTwoDistance >= oneTwoDistance && zeroTwoDistance >= zeroOneDistance) {
            pointB = p1;
            pointA = p0;
            pointC = p2;
        }
        else {
            pointB = p2;
            pointA = p0;
            pointC = p1;
        }

        // Use cross product to figure out whether A and C are correct or flipped.
        // This asks whether BC x BA has a positive z component, which is the arrangement
        // we want for A, B, C. If it's negative, then we've got it flipped around and
        // should swap A and C.
        if (CrossProductZ(*pointA, *pointB, *pointC) < 0.0f) {
            std::swap(pointA, pointC);
        }

        p0 = pointA;
        p1 = pointB;
        p2 = pointC;
    }

    static DetectorResult DetectOld(const BitMatrix& image)
    {
        ResultPoint pointA, pointB, pointC, pointD;
        if (!DetectWhiteRect(image, pointA, pointB, pointC, pointD))
            return {};

        // Point A and D are across the diagonal from one another,
        // as are B and C. Figure out which are the solid black lines
        // by counting transitions
        std::array transitions = {
                TransitionsBetween(image, pointA, pointB),
                TransitionsBetween(image, pointA, pointC),
                TransitionsBetween(image, pointB, pointD),
                TransitionsBetween(image, pointC, pointD),
        };
        std::sort(transitions.begin(), transitions.end(),
                  [](const auto& a, const auto& b) { return a.transitions < b.transitions; });

        // Sort by number of transitions. First two will be the two solid sides; last two
        // will be the two alternating black/white sides
        const auto& lSideOne = transitions[0];
        const auto& lSideTwo = transitions[1];

        // We accept at most 4 transisions inside the L pattern (i.e. 2 corruptions) to reduce false positive FormatErrors
        if (lSideTwo.transitions > 8)
            return {};

        // Figure out which point is their intersection by tallying up the number of times we see the
        // endpoints in the four endpoints. One will show up twice.
        std::map<const ResultPoint*, int> pointCount;
        pointCount[lSideOne.from] += 1;
        pointCount[lSideOne.to] += 1;
        pointCount[lSideTwo.from] += 1;
        pointCount[lSideTwo.to] += 1;

        const ResultPoint* bottomRight = nullptr;
        const ResultPoint* bottomLeft = nullptr;
        const ResultPoint* topLeft = nullptr;
        for (const auto& [point, count] : pointCount) {
            if (count == 2) {
                bottomLeft = point; // this is definitely the bottom left, then -- end of two L sides
            }
            else {
                // Otherwise it's either top left or bottom right -- just assign the two arbitrarily now
                if (bottomRight == nullptr) {
                    bottomRight = point;
                }
                else {
                    topLeft = point;
                }
            }
        }

        if (bottomRight == nullptr || bottomLeft == nullptr || topLeft == nullptr)
            return {};

        // Bottom left is correct but top left and bottom right might be switched
        // Use the dot product trick to sort them out
        OrderByBestPatterns(bottomRight, bottomLeft, topLeft);

        // Which point didn't we find in relation to the "L" sides? that's the top right corner
        const ResultPoint* topRight;
        if (pointCount.find(&pointA) == pointCount.end()) {
            topRight = &pointA;
        }
        else if (pointCount.find(&pointB) == pointCount.end()) {
            topRight = &pointB;
        }
        else if (pointCount.find(&pointC) == pointCount.end()) {
            topRight = &pointC;
        }
        else {
            topRight = &pointD;
        }

        // Next determine the dimension by tracing along the top or right side and counting black/white
        // transitions. Since we start inside a black module, we should see a number of transitions
        // equal to 1 less than the code dimension. Well, actually 2 less, because we are going to
        // end on a black module:

        // The top right point is actually the corner of a module, which is one of the two black modules
        // adjacent to the white module at the top right. Tracing to that corner from either the top left
        // or bottom right should work here.

        int dimensionTop = TransitionsBetween(image, *topLeft, *topRight).transitions;
        int dimensionRight = TransitionsBetween(image, *bottomRight, *topRight).transitions;


        if ((dimensionTop & 0x01) == 1) {
            // it can't be odd, so, round... up?
            dimensionTop++;
        }
        dimensionTop += 2;

        if ((dimensionRight & 0x01) == 1) {
            // it can't be odd, so, round... up?
            dimensionRight++;
        }
        dimensionRight += 2;

        if (dimensionTop < 10 || dimensionTop > 144 || dimensionRight < 8 || dimensionRight > 144 )
            return {};

        ResultPoint correctedTopRight;

        // Rectangular symbols are 6x16, 6x28, 10x24, 10x32, 14x32, or 14x44. If one dimension is more
        // than twice the other, it's certainly rectangular, but to cut a bit more slack we accept it as
        // rectangular if the bigger side is at least 7/4 times the other:
        if (4 * dimensionTop >= 7 * dimensionRight || 4 * dimensionRight >= 7 * dimensionTop) {
            // The matrix is rectangular

            if (!CorrectTopRightRectangular(image, *bottomLeft, *bottomRight, *topLeft, *topRight, dimensionTop,
                                            dimensionRight, correctedTopRight)) {
                correctedTopRight = *topRight;
            }

            dimensionTop = TransitionsBetween(image, *topLeft, correctedTopRight).transitions;
            dimensionRight = TransitionsBetween(image, *bottomRight, correctedTopRight).transitions;

            if ((dimensionTop & 0x01) == 1) {
                // it can't be odd, so, round... up?
                dimensionTop++;
            }

            if ((dimensionRight & 0x01) == 1) {
                // it can't be odd, so, round... up?
                dimensionRight++;
            }
        }
        else {
            // The matrix is square

            int dimension = std::min(dimensionRight, dimensionTop);
            // correct top right point to match the white module
            correctedTopRight = CorrectTopRight(image, *bottomLeft, *bottomRight, *topLeft, *topRight, dimension);

            // Redetermine the dimension using the corrected top right point
            int dimensionCorrected = std::max(TransitionsBetween(image, *topLeft, correctedTopRight).transitions,
                                              TransitionsBetween(image, *bottomRight, correctedTopRight).transitions);
            dimensionCorrected++;
            if ((dimensionCorrected & 0x01) == 1) {
                dimensionCorrected++;
            }

            dimensionTop = dimensionRight = dimensionCorrected;
        }


        return SampleGrid(image, *topLeft, *bottomLeft, *bottomRight, correctedTopRight, dimensionTop, dimensionRight);
    }

    static DetectorResult DetectOldWithOffsets(const BitMatrix& image, DecoderResult& outDecoderResult)
    {
        ResultPoint pointA, pointB, pointC, pointD;
        if (!DetectWhiteRect(image, pointA, pointB, pointC, pointD))
            return {};

        // Point A and D are across the diagonal from one another,
        // as are B and C. Figure out which are the solid black lines
        // by counting transitions
        std::array transitions = {
                TransitionsBetween(image, pointA, pointB),
                TransitionsBetween(image, pointA, pointC),
                TransitionsBetween(image, pointB, pointD),
                TransitionsBetween(image, pointC, pointD),
        };
        std::sort(transitions.begin(), transitions.end(),
                  [](const auto& a, const auto& b) { return a.transitions < b.transitions; });

        // Sort by number of transitions. First two will be the two solid sides; last two
        // will be the two alternating black/white sides
        const auto& lSideOne = transitions[0];
        const auto& lSideTwo = transitions[1];

        // We accept at most 4 transisions inside the L pattern (i.e. 2 corruptions) to reduce false positive FormatErrors
        if (lSideTwo.transitions > 8)
            return {};

        // Figure out which point is their intersection by tallying up the number of times we see the
        // endpoints in the four endpoints. One will show up twice.
        std::map<const ResultPoint*, int> pointCount;
        pointCount[lSideOne.from] += 1;
        pointCount[lSideOne.to] += 1;
        pointCount[lSideTwo.from] += 1;
        pointCount[lSideTwo.to] += 1;

        const ResultPoint* bottomRight = nullptr;
        const ResultPoint* bottomLeft = nullptr;
        const ResultPoint* topLeft = nullptr;
        for (const auto& [point, count] : pointCount) {
            if (count == 2) {
                bottomLeft = point; // this is definitely the bottom left, then -- end of two L sides
            }
            else {
                // Otherwise it's either top left or bottom right -- just assign the two arbitrarily now
                if (bottomRight == nullptr) {
                    bottomRight = point;
                }
                else {
                    topLeft = point;
                }
            }
        }

        if (bottomRight == nullptr || bottomLeft == nullptr || topLeft == nullptr)
            return {};

        // Bottom left is correct but top left and bottom right might be switched
        // Use the dot product trick to sort them out
        OrderByBestPatterns(bottomRight, bottomLeft, topLeft);

        // Which point didn't we find in relation to the "L" sides? that's the top right corner
        const ResultPoint* topRight;
        if (pointCount.find(&pointA) == pointCount.end()) {
            topRight = &pointA;
        }
        else if (pointCount.find(&pointB) == pointCount.end()) {
            topRight = &pointB;
        }
        else if (pointCount.find(&pointC) == pointCount.end()) {
            topRight = &pointC;
        }
        else {
            topRight = &pointD;
        }

        // Next determine the dimension by tracing along the top or right side and counting black/white
        // transitions. Since we start inside a black module, we should see a number of transitions
        // equal to 1 less than the code dimension. Well, actually 2 less, because we are going to
        // end on a black module:

        // The top right point is actually the corner of a module, which is one of the two black modules
        // adjacent to the white module at the top right. Tracing to that corner from either the top left
        // or bottom right should work here.

        int dimensionTop = TransitionsBetween(image, *topLeft, *topRight).transitions;
        int dimensionRight = TransitionsBetween(image, *bottomRight, *topRight).transitions;


        if ((dimensionTop & 0x01) == 1) {
            // it can't be odd, so, round... up?
            dimensionTop++;
        }
        dimensionTop += 2;

        if ((dimensionRight & 0x01) == 1) {
            // it can't be odd, so, round... up?
            dimensionRight++;
        }
        dimensionRight += 2;

        if (dimensionTop < 10 || dimensionTop > 144 || dimensionRight < 8 || dimensionRight > 144 )
            return {};




        ResultPoint correctedTopRight;
        {
            // The matrix is square

            int dimension = std::max(dimensionRight, dimensionTop);

            DetectorResult res;
            float dimInv = 1.0 / float(dimension);
            PointF DirTopLR = dimInv * (*topRight - *topLeft);
            PointF DirBottomLR = dimInv * (*bottomRight - *bottomLeft);

            PointF DirRightBT = dimInv * (*topRight - *bottomRight);
            PointF DirLeftBT = dimInv * (*topLeft - *bottomLeft);

            dimensionTop = dimensionRight = dimension;

            res = SampleGrid(image, *topLeft, *bottomLeft, *bottomRight + DirBottomLR, *topRight + DirTopLR, dimensionTop, dimensionRight);
            if(outDecoderResult = Decode(res.bits()); outDecoderResult.isValid()) return res;

            res = SampleGrid(image, *topLeft + DirLeftBT, *bottomLeft, *bottomRight, *topRight + DirRightBT, dimensionTop, dimensionRight);
            if(outDecoderResult = Decode(res.bits()); outDecoderResult.isValid()) return res;


            correctedTopRight = CorrectTopRight(image, *bottomLeft, *bottomRight, *topLeft, *topRight, dimension);

            DirTopLR = dimInv * (correctedTopRight - *topLeft);
            DirRightBT = dimInv * (correctedTopRight - *bottomRight);

            res = SampleGrid(image, *topLeft - DirTopLR, *bottomLeft - DirBottomLR, *bottomRight, correctedTopRight, dimensionTop, dimensionRight);
            if(outDecoderResult = Decode(res.bits()); outDecoderResult.isValid()) return res;

            res = SampleGrid(image, *topLeft, *bottomLeft - DirLeftBT, *bottomRight - DirRightBT, correctedTopRight, dimensionTop, dimensionRight);
            if(outDecoderResult = Decode(res.bits()); outDecoderResult.isValid()) return res;

            res = SampleGrid(image, *topLeft, *bottomLeft, *bottomRight, correctedTopRight, dimensionTop, dimensionRight);
            outDecoderResult = Decode(res.bits());
            return res;
        }

        return {};
    }












/**
* The following code is the 'new' one implemented by Axel Waggershauser and is working completely different.
* It is performing something like a (back) trace search along edges through the bit matrix, first looking for
* the 'L'-pattern, then tracing the black/white borders at the top/right. Advantages over the old code are:
*  * works with lower resolution scans (around 2 pixel per module), due to sub-pixel precision grid placement
*  * works with real-world codes that have just one module wide quiet-zone (which is perfectly in spec)
*/

    class DMRegressionLine : public RegressionLine
    {
        template <typename Container, typename Filter>
        static double average(const Container& c, Filter f)
        {
            double sum = 0;
            int num = 0;
            for (const auto& v : c)
                if (f(v)) {
                    sum += v;
                    ++num;
                }
            return sum / num;
        }

    public:
        void reverse() { std::reverse(_points.begin(), _points.end()); }

        double modules(PointF beg, PointF end)
        {
            assert(_points.size() > 3);

            // re-evaluate and filter out all points too far away. required for the gapSizes calculation.
            evaluate(1.2, true);

            std::vector<double> gapSizes, modSizes;
            gapSizes.reserve(_points.size());

            // calculate the distance between the points projected onto the regression line
            for (size_t i = 1; i < _points.size(); ++i)
                gapSizes.push_back(distance(project(_points[i]), project(_points[i - 1])));

            // calculate the (expected average) distance of two adjacent pixels
            auto unitPixelDist = ZXing::length(bresenhamDirection(_points.back() - _points.front()));

            // calculate the width of 2 modules (first black pixel to first black pixel)
            double sumFront = distance(beg, project(_points.front())) - unitPixelDist;
            double sumBack = 0; // (last black pixel to last black pixel)
            for (auto dist : gapSizes) {
                if (dist > 1.9 * unitPixelDist)
                    modSizes.push_back(std::exchange(sumBack, 0.0));
                sumFront += dist;
                sumBack += dist;
                if (dist > 1.9 * unitPixelDist)
                    modSizes.push_back(std::exchange(sumFront, 0.0));
            }
            if (modSizes.empty())
                return 0;
            modSizes.push_back(sumFront + distance(end, project(_points.back())));
            modSizes.front() = 0; // the first element is an invalid sumBack value, would be pop_front() if vector supported this
            auto lineLength = distance(beg, end) - unitPixelDist;
            auto [iMin, iMax] = std::minmax_element(modSizes.begin() + 1, modSizes.end());
            auto meanModSize = average(modSizes, [](double dist){ return dist > 0; });

            printf("unit pixel dist: %.1f\n", unitPixelDist);
            printf("lineLength: %.1f, meanModSize: %.1f (min: %.1f, max: %.1f), gaps: %lu\n", lineLength, meanModSize, *iMin, *iMax,
                   modSizes.size());
            printv("%.1f ", modSizes);

            if (*iMax > 2 * *iMin) {
                for (int i = 1; i < Size(modSizes) - 2; ++i) {
                    if (modSizes[i] > 0 && modSizes[i] + modSizes[i + 2] < meanModSize * 1.4)
                        modSizes[i] += std::exchange(modSizes[i + 2], 0);
                    else if (modSizes[i] > meanModSize * 1.6)
                        modSizes[i] = 0;
                }
                printv("%.1f ", modSizes);

                meanModSize = average(modSizes, [](double dist) { return dist > 0; });
            }
            printf("post filter meanModSize: %.1f\n", meanModSize);

            return lineLength / meanModSize;
        }
    };

    class EdgeTracer : public BitMatrixCursorF
    {
        enum class StepResult { FOUND, OPEN_END, CLOSED_END };

        // force this function inline to allow the compiler optimize for the maxStepSize==1 case in traceLine()
        // this can result in a 10% speedup of the falsepositive use case when build with c++20
#if defined(__clang__) || defined(__GNUC__)
        inline __attribute__((always_inline))
#elif defined(_MSC_VER)
        __forceinline
#endif
        StepResult traceStep(PointF dEdge, int maxStepSize, bool goodDirection)
        {
            dEdge = mainDirection(dEdge);
            for (int breadth = 1; breadth <= (maxStepSize == 1 ? 2 : (goodDirection ? 1 : 3)); ++breadth)
                for (int step = 1; step <= maxStepSize; ++step)
                    for (int i = 0; i <= 2*(step/4+1) * breadth; ++i) {
                        auto pEdge = p + step * d + (i&1 ? (i+1)/2 : -i/2) * dEdge;
                        log(pEdge);

                        if (!blackAt(pEdge + dEdge))
                            continue;

                        // found black pixel -> go 'outward' until we hit the b/w border
                        for (int j = 0; j < std::max(maxStepSize, 3) && isIn(pEdge); ++j) {
                            if (whiteAt(pEdge)) {
                                // if we are not making any progress, we still have another endless loop bug
                                assert(p != centered(pEdge));
                                p = centered(pEdge);

                                if (history && maxStepSize == 1) {
                                    if (history->get(PointI(p)) == state)
                                        return StepResult::CLOSED_END;
                                    history->set(PointI(p), state);
                                }

                                return StepResult::FOUND;
                            }
                            pEdge = pEdge - dEdge;
                            if (blackAt(pEdge - d))
                                pEdge = pEdge - d;
                            log(pEdge);
                        }
                        // no valid b/w border found within reasonable range
                        return StepResult::CLOSED_END;
                    }
            return StepResult::OPEN_END;
        }

    public:
        ByteMatrix* history = nullptr;
        int state = 0;

        using BitMatrixCursorF::BitMatrixCursor;

        bool updateDirectionFromOrigin(PointF origin)
        {
            auto old_d = d;
            setDirection(p - origin);
            // if the new direction is pointing "backward", i.e. angle(new, old) > 90 deg -> break
            if (dot(d, old_d) < 0)
                return false;
            // make sure d stays in the same quadrant to prevent an infinite loop
            if (std::abs(d.x) == std::abs(d.y))
                d = mainDirection(old_d) + 0.99f * (d - mainDirection(old_d));
            else if (mainDirection(d) != mainDirection(old_d))
                d = mainDirection(old_d) + 0.99f * mainDirection(d);
            return true;
        }

	bool updateDirectionFromLine(RegressionLine& line)
	{
		return line.evaluate(1.5) && updateDirectionFromOrigin(p - line.project(p) + line.points().front());
	}

	bool updateDirectionFromLineCentroid(RegressionLine& line)
	{
		// Basically a faster, less accurate version of the above without the line evaluation
		return updateDirectionFromOrigin(line.centroid());
	}

	bool traceLine(PointF dEdge, RegressionLine& line)
	{
		line.setDirectionInward(dEdge);
		do {
			log(p);
			line.add(p);
			if (line.points().size() % 50 == 10) {
				if (!line.evaluate())
					return false;
				if (!updateDirectionFromOrigin(p - line.project(p) + line.points().front()))
					return false;
			}
			auto stepResult = traceStep(dEdge, 1, line.isValid());
			if (stepResult != StepResult::FOUND)
				return stepResult == StepResult::OPEN_END && line.points().size() > 1;
		} while (true);
	}

    bool traceGaps(PointF dEdge, RegressionLine& line, int maxStepSize, const RegressionLine& finishLine = {}, double minDist = 0)
    {
        line.setDirectionInward(dEdge);
        int gaps = 0, steps = 0, maxStepsPerGap = maxStepSize;
        PointF lastP;
        do {
            // detect an endless loop (lack of progress). if encountered, please report.
            // this fixes a deadlock in falsepositives-1/#570.png and the regression in #574
            if (p == std::exchange(lastP, p) || steps++ > (gaps == 0 ? 2 : gaps + 1) * maxStepsPerGap)
                return false;
            log(p);

                // if we drifted too far outside of the code, break
                if (line.isValid() && line.signedDistance(p) < -5 && (!line.evaluate() || line.signedDistance(p) < -5))
                    return false;

                // if we are drifting towards the inside of the code, pull the current position back out onto the line
                if (line.isValid() && line.signedDistance(p) > 3) {
                    // The current direction d and the line we are tracing are supposed to be roughly parallel.
                    // In case the 'go outward' step in traceStep lead us astray, we might end up with a line
                    // that is almost perpendicular to d. Then the back-projection below can result in an
                    // endless loop. Break if the angle between d and line is greater than 45 deg.
                    if (std::abs(dot(normalized(d), line.normal())) > 0.7) // thresh is approx. sin(45 deg)
                        return false;

                    // re-evaluate line with all the points up to here before projecting
                    if (!line.evaluate(1.5))
                        return false;

                auto np = line.project(p);
                // make sure we are making progress even when back-projecting:
                // consider a 90deg corner, rotated 45deg. we step away perpendicular from the line and get
                // back projected where we left off the line.
                // The 'while' instead of 'if' was introduced to fix the issue with #245. It turns out that
                // np can actually be behind the projection of the last line point and we need 2 steps in d
                // to prevent a dead lock. see #245.png
                while (distance(np, line.project(line.points().back())) < 1)
                    np = np + d;
                p = centered(np);
            }
            else {
                auto curStep = line.points().empty() ? PointF() : p - line.points().back();
                auto stepLengthInMainDir = line.points().empty() ? 0.0 : dot(mainDirection(d), curStep);
                line.add(p);

                if (stepLengthInMainDir > 1 || maxAbsComponent(curStep) >= 2) {
                    ++gaps;
                    if (gaps >= 2 || line.points().size() > 5) {
                        if (!updateDirectionFromLine(line))
                            return false;
                        // check if the first half of the top-line trace is complete.
                        // the minimum code size is 10x10 -> every code has at least 4 gaps
                        if (minDist && gaps >= 4 && distance(p, line.points().front()) > minDist) {
                            // undo the last insert, it will be inserted again after the restart
                            line.pop_back();
                            --gaps;
                            return true;
                        }
                    }
                } else if (gaps == 0 && Size(line.points()) >= 2 * maxStepSize) {
                    return false; // no point in following a line that has no gaps
                }
            }

                if (finishLine.isValid())
                    UpdateMin(maxStepSize, static_cast<int>(finishLine.signedDistance(p)));

                auto stepResult = traceStep(dEdge, maxStepSize, line.isValid());

                if (stepResult != StepResult::FOUND)
                    // we are successful iff we found an open end across a valid finishLine
                    return stepResult == StepResult::OPEN_END && finishLine.isValid() &&
                           static_cast<int>(finishLine.signedDistance(p)) <= maxStepSize + 1;
            } while (true);
        }

        bool traceCorner(PointF dir, PointF& corner)
        {
            step();
            log(p);
            corner = p;
            std::swap(d, dir);
            traceStep(-1 * dir, 2, false);
            printf("turn: %.0f x %.0f -> %.2f, %.2f\n", p.x, p.y, d.x, d.y);

            return isIn(corner) && isIn(p);
        }

        bool moveToNextWhiteAfterBlack()
        {
            assert(std::abs(d.x + d.y) == 1);

            FastEdgeToEdgeCounter e2e(BitMatrixCursorI(*img, PointI(p), PointI(d)));
            int steps = e2e.stepToNextEdge(INT_MAX);
            if (!steps)
                return false;
            step(steps);
            if(isWhite())
                return true;

            steps = e2e.stepToNextEdge(INT_MAX);
            if (!steps)
                return false;
            return step(steps);
        }
    };


    static DetectorResult Scan(EdgeTracer& startTracer, std::array<DMRegressionLine, 4>& lines, Warp* warp = nullptr, bool tryToTraceWarp = false, bool correctCorners = false)
    {
        while (startTracer.moveToNextWhiteAfterBlack()) {
            log(startTracer.p);

            PointF tl, bl, br, tr;
            auto& [lineL, lineB, lineR, lineT] = lines;

            for (auto& l : lines)
                l.reset();

#ifdef PRINT_DEBUG
            SCOPE_EXIT([&] {
			for (auto& l : lines)
				log(l.points());
		});
# define CHECK(A) if (!(A)) { printf("broke at %d\n", __LINE__); continue; }
#else
# define CHECK(A) if(!(A)) continue
#endif

            auto t = startTracer;

            // follow left leg upwards
            t.turnRight();
            t.state = 1;
            CHECK(t.traceLine(t.right(), lineL));
            CHECK(t.traceCorner(t.right(), tl));
            lineL.reverse();
            auto tlTracer = t;

            // follow left leg downwards
            t = startTracer;
            t.state = 1;
            t.setDirection(tlTracer.right());
            CHECK(t.traceLine(t.left(), lineL));
            if (!lineL.isValid())
                t.updateDirectionFromOrigin(tl);
            auto up = t.back();
            CHECK(t.traceCorner(t.left(), bl));

            // follow bottom leg right
            t.state = 2;
            CHECK(t.traceLine(t.left(), lineB));
            if (!lineB.isValid())
                t.updateDirectionFromOrigin(bl);
            auto right = t.front();
            CHECK(t.traceCorner(t.left(), br));

            auto lenL = distance(tl, bl) - 1;
            auto lenB = distance(bl, br) - 1;
            CHECK(lenL >= 8 && lenB >= 10 && lenB >= lenL / 4 && lenB <= lenL * 18);

            auto maxStepSize = static_cast<int>(lenB / 5 + 1); // datamatrix bottom dim is at least 10

            // at this point we found a plausible L-shape and are now looking for the b/w pattern at the top and right:
            // follow top row right 'half way' (4 gaps), see traceGaps break condition with 'invalid' line
            tlTracer.setDirection(right);
            CHECK(tlTracer.traceGaps(tlTracer.right(), lineT, maxStepSize, {}, lenB / 2));

            maxStepSize = std::min(lineT.length() / 3, static_cast<int>(lenL / 5)) * 2;

            // follow up until we reach the top line
            t.setDirection(up);
            t.state = 3;
            CHECK(t.traceGaps(t.left(), lineR, maxStepSize, lineT));
            CHECK(t.traceCorner(t.left(), tr));

            auto lenT = distance(tl, tr) - 1;
            auto lenR = distance(tr, br) - 1;

            CHECK(std::abs(lenT - lenB) / lenB < 0.5 && std::abs(lenR - lenL) / lenL < 0.5 &&
                  lineT.points().size() >= 5 && lineR.points().size() >= 5);

            // continue top row right until we cross the right line
            CHECK(tlTracer.traceGaps(tlTracer.right(), lineT, maxStepSize, lineR));

            printf("L: %.1f, %.1f ^ %.1f, %.1f > %.1f, %.1f (%d : %d : %d : %d)\n", bl.x, bl.y,
                   tl.x - bl.x, tl.y - bl.y, br.x - bl.x, br.y - bl.y, (int)lenL, (int)lenB, (int)lenT, (int)lenR);

            for (auto* l : {&lineL, &lineB, &lineT, &lineR})
                l->evaluate(1.0);

            // find the bounding box corners of the code with sub-pixel precision by intersecting the 4 border lines
            bl = intersect(lineB, lineL);
            tl = intersect(lineT, lineL);
            tr = intersect(lineT, lineR);
            br = intersect(lineB, lineR);

            int dimT, dimR;
            double fracT, fracR;
            auto splitDouble = [](double d, int* i, double* f) {
                *i = std::isnormal(d) ? static_cast<int>(d + 0.5) : 0;
                *f = std::isnormal(d) ? std::abs(d - *i) : INFINITY;
            };
            splitDouble(lineT.modules(tl, tr), &dimT, &fracT);
            splitDouble(lineR.modules(br, tr), &dimR, &fracR);

            // the dimension is 2x the number of black/white transitions
            dimT *= 2;
            dimR *= 2;

            printf("L: %.1f, %.1f ^ %.1f, %.1f > %.1f, %.1f ^> %.1f, %.1f\n", bl.x, bl.y,
                   tl.x - bl.x, tl.y - bl.y, br.x - bl.x, br.y - bl.y, tr.x, tr.y);
            printf("dim: %d x %d\n", dimT, dimR);

            // if we have an almost square (invalid rectangular) data matrix dimension, we try to parse it by assuming a
            // square. we use the dimension that is closer to an integral value. all valid rectangular symbols differ in
            // their dimension by at least 10. Note: this is currently not required for the black-box tests to complete.
            if (std::abs(dimT - dimR) < 10)
                dimT = dimR = fracR < fracT ? dimR : dimT;

            CHECK(dimT >= 10 && dimT <= 144 && dimR >= 8 && dimR <= 144);

            auto movedTowardsBy = [](PointF a, PointF b1, PointF b2, auto d) {
                return a + d * normalized(normalized(b1 - a) + normalized(b2 - a));
            };

            // shrink shape by half a pixel to go from center of white pixel outside of code to the edge between white and black
            QuadrilateralF sourcePoints = {
                    movedTowardsBy(tl, tr, bl, 0.5f),
                    // move the tr point a little less because the jagged top and right line tend to be statistically slightly
                    // inclined toward the center anyway.
                    movedTowardsBy(tr, br, tl, 0.3f),
                    movedTowardsBy(br, bl, tr, 0.5f),
                    movedTowardsBy(bl, tl, br, 0.5f),
            };

            DetectorResult res;
			if(tryToTraceWarp && warp) {
				if(!warp->isValid() || warp->xOffsets.size() > dimT || warp->yOffsets.size() > dimR) {
					*warp = ComputeWarp(*startTracer.img, tl, bl, br, tr, 5, 5, dimT);
				}
				// warp->Resample(dimT, dimR);
			}
        	if(warp) {
				if(warp->xOffsets.size() != dimT || warp->yOffsets.size() != dimR) {
					warp->Resample(dimT, dimR);
				}
                if(correctCorners) {
                    auto TL = tl, BL = bl, BR = br, TR = tr;
                    CorrectCorners(*startTracer.img, TL, BL, BR, TR, dimT);
                    res = SampleGridWarped(*startTracer.img, TL, BL, BR, TR, dimT, dimR, *warp );
                } else {
					res = SampleGridWarped(*startTracer.img, dimT, dimR, *warp, PerspectiveTransform(Rectangle(dimT, dimR, 0), sourcePoints));
                }
            } else {
                res = SampleGrid(*startTracer.img, dimT, dimR, PerspectiveTransform(Rectangle(dimT, dimR, 0), sourcePoints));
            }

            CHECK(res.isValid());

            return res;
        }

        return {};
    }


    static DetectorResults DetectNew(const BitMatrix& image, bool tryHarder, bool tryRotate, Warp* warp = nullptr, bool tryToTraceWarp = false, bool correctCorners = false)
    {
#ifdef PRINT_DEBUG
        LogMatrixWriter lmw(log, image, 1, "dm-log.pnm");
//	tryRotate = tryHarder = false;
#endif

        // disable expensive multi-line scan to detect off-center symbols for now
#ifndef __cpp_impl_coroutine
        tryHarder = false;
#endif

        //ResultPoint p1;
        //createBitmapFromBitMatrix(image,p1,p1,p1,p1);

        // a history log to remember where the tracing already passed by to prevent a later trace from doing the same work twice
        ByteMatrix history;
        if (tryHarder)
            history = ByteMatrix(image.width(), image.height());

        // instantiate RegressionLine objects outside of Scan function to prevent repetitive std::vector allocations
        std::array<DMRegressionLine, 4> lines;

        constexpr int minSymbolSize = 8 * 2; // minimum realistic size in pixel: 8 modules x 2 pixels per module

        for (auto dir : {PointF{-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
            auto center = PointI(image.width() / 2, image.height() / 2);
            auto startPos = centered(center - center * dir + minSymbolSize / 2 * dir);

            history.clear();

            for (int i = 1;; ++i) {
                EdgeTracer tracer(image, startPos, dir);
                tracer.p += i / 2 * minSymbolSize * (i & 1 ? -1 : 1) * tracer.right();
                if (tryHarder)
                    tracer.history = &history;

                if (!tracer.isIn())
                    break;

#ifdef __cpp_impl_coroutine
                DetectorResult res;
			while (res = Scan(tracer, lines), res.isValid())
				co_yield std::move(res);
#else
                if (auto res = Scan(tracer, lines, warp, tryToTraceWarp, correctCorners); res.isValid()) {
                    return res;
                }
#endif

                if (!tryHarder)
                    break; // only test center lines
            }

            if (!tryRotate)
                break; // only test left direction
        }

#ifndef __cpp_impl_coroutine
        return {};
#endif
    }



    void correctBottle(const BitMatrix& img, BitMatrix& outImg, bool horizontal, bool inverse) {

        outImg = img.copy();

        int width, height;
        bool point;
        int newx, newy;
        int center,dy;
        float factor=15;

        width = img.width();
        height = img.height();


        if (horizontal) {

            center = (int)(height / 2.0);
            factor = (float)center / 7.6;


            for (int x = 0; x < width; x++) {
                for (int y = 0; y < height; y++) {
                    point = img.get(x, y);
                    dy = abs(y - center);
                    if (inverse) {
                        newx = (x - cos((float)dy / center) * factor) + (factor * 0.75f);
                    }
                    else {
                        newx = (x + cos((float)dy / center) * factor) - (factor * 0.75f);
                    }
                    if (newx >= width) newx = width - 1;
                    if (newx < 0) newx = 0;
                    newy = y;
                    outImg.set(newx, newy, point);
                }

            }
        }
        else {


            center = (int)(width / 2.0);
            factor = (float)center / 7.6;


            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    point = img.get(x, y);
                    dy = abs(x - center);
                    if (inverse) {
                        newy = (y - cos((float)dy / center) * factor) + (factor * 0.75f);
                    }
                    else {
                        newy = (y + cos((float)dy / center) * factor) - (factor * 0.75f);
                    }
                    if (newy >= height) newy = height - 1;
                    if (newy < 0) newy = 0;
                    newx = x;
                    outImg.set(newx, newy, point);
                }

            }


        }


        // img = img2.copy();

    }


    void rotate(const BitMatrix& img, BitMatrix& outImg, const PointF& sincos) {
        int  rows, cols,r,c,r1,c1,k,s;

        rows = img.width();
        cols = img.height();

        int centRow = rows / 2;
        int centCol = cols / 2;

        float maxRow = centRow + (rows - centRow) * sincos.x - (0 - centCol) * sincos.y;
        float maxCol = centCol + (cols - centCol) * sincos.x + (rows - centRow) * sincos.y;
        float minRow = centRow + (0 - centRow) * sincos.x - (cols - centCol) * sincos.y;
        float minCol = centCol + (0 - centCol) * sincos.x + (0 - centRow) * sincos.y;
        r = maxRow - minRow + 1;
        c = maxCol - minCol + 1;
        if (r < rows) r1 = rows; else r1 = r;
        if (c < cols) c1 = cols; else c1 = c;
        s = r1 / 40;
        if (s < 20) s = 20;

        outImg = BitMatrix(r1, c1);

        for (int i = 0; i < r1; i++)
        {
            for (int j = 0; j < c1; j++)
            {
                if ((i < rows) && (j < cols))
                {
                    float x = centRow + (i - centRow) * sincos.x - (j - centCol) * sincos.y;
                    float y = centCol + (j - centCol) * sincos.x + (i - centRow) * sincos.y;
                    int newI = x - minRow;
                    int newJ = y - minCol;

                    if ((newI >= 0) && (newI < r1) && (newJ >= 0) && (newJ < c1)) {
                        outImg.set(newI, newJ, img.get(i, j));

                        try {
                            if (newJ > 1 && outImg.get(newI, newJ - 1) == false)
                            {
                                if (i > 0 && j > 0) {
                                    k = 0;
                                    while ((outImg.get(newI, newJ - 1) == false) && (newJ > 1) && (k < s))
                                    {
                                        newJ--; k++;
                                        if (outImg.get(newI + 1, newJ + 1)) 	break;
                                    }
                                }

                                if ((newI >= 0) && (newI < r1) && (newJ >= 0) && (newJ < c1))
                                    outImg.set(newI, newJ, img.get(i, j));
                            }
                        }
                        catch (...) {}
                    }
                }

            }
        }
    }


    void rotate45(BitMatrix& img) {
        int  rows, cols,r,c,r1,c1,k,s;
        float rad = 0.785398; //45grad

        rows = img.width();
        cols = img.height();

        int centRow = rows / 2;
        int centCol = cols / 2;

        //��������� ��� angle �� 0 �� 90
        float maxRow = centRow + (rows - centRow) * cos(rad) - (0 - centCol) * sin(rad);
        float maxCol = centCol + (cols - centCol) * cos(rad) + (rows - centRow) * sin(rad);
        float minRow = centRow + (0 - centRow) * cos(rad) - (cols - centCol) * sin(rad);
        float minCol = centCol + (0 - centCol) * cos(rad) + (0 - centRow) * sin(rad);
        r = maxRow - minRow + 1;
        c = maxCol - minCol + 1;
        if (r < rows) r1 = rows; else r1 = r;
        if (c < cols) c1 = cols; else c1 = c;
        s = r1 / 40;
        if (s < 20) s = 20;//�����������, �� ����� ��������

        BitMatrix img2 = BitMatrix(r1, c1);

        for (int i = 0; i < r1; i++)
        {
            for (int j = 0; j < c1; j++)
            {
                if ((i < rows) && (j < cols))
                {
                    float x = centRow + (i - centRow) * cos(rad) - (j - centCol) * sin(rad);
                    float y = centCol + (j - centCol) * cos(rad) + (i - centRow) * sin(rad);
                    int newI = x - minRow;
                    int newJ = y - minCol;

                    if ((newI >= 0) && (newI < r1) && (newJ >= 0) && (newJ < c1)) {
                        img2.set(newI, newJ, img.get(i, j));


                        try {

                            if (newJ > 1 && img2.get(newI, newJ - 1) == false)
                            {
                                //���� ���������� �������� ������ �������(��� ���������), �� �������� ������� �������� � ���� ������ �������
                                if (i > 0 && j > 0) {   //��� �����, ����� ������� �� ���������� � ������
                                    k = 0;
                                    while ((img2.get(newI, newJ - 1) == false) && (newJ > 1) && (k < s))
                                    {
                                        newJ--; k++;
                                        if (img2.get(newI + 1, newJ + 1)) 	break;
                                    }
                                }

                                if ((newI >= 0) && (newI < r1) && (newJ >= 0) && (newJ < c1))
                                    img2.set(newI, newJ, img.get(i, j));

                            }

                        }
                        catch (...) {}



                    }


                }

            }
        }


        img = img2.copy();

    }







    void line2(BitMatrix& img, int x1, int y1, int x2, int y2) {

        int deltaX = abs(x2 - x1);
        int deltaY = abs(y2 - y1);
        int signX = x1 < x2 ? 1 : -1;
        int signY = y1 < y2 ? 1 : -1;
        int error = deltaX - deltaY;

        if ((x1 < 0) || (y1 < 0) || (x2 < 0) || (y2 < 0) || (x1 >= img.width()) || (x2 >= img.width()) || (y1 >= img.height()) || (y2 >= img.height())) return;

        img.set(x2, y2, true);
        while (x1 != x2 || y1 != y2)
        {

            if (!((x1 < 0) || (y1 < 0) || ((x1+1) >= img.width()) || ((y1+1) >= img.height()))) {
                img.set(x1, y1, true);
                img.set(x1 + 1, y1 + 1, true);
            }

            int error2 = error * 2;

            if (error2 > -deltaY)
            {
                error -= deltaY;
                x1 += signX;
            }
            if (error2 < deltaX)
            {
                error += deltaX;
                y1 += signY;
            }
        }

    }



std::array rotateEasy = {
    PointF(cos(M_PI / 4), sin(M_PI / 4))
};

std::array rotateMediun = {
    PointF(cos(M_PI / 4), sin(M_PI / 4)),
    PointF(cos(M_PI / 6), sin(M_PI / 6)),
    PointF(cos(M_PI / 3), sin(M_PI / 3))
};

    static DetectorResult DetectCRPT(const BitMatrix& image, DecoderResult& outDecodeResult, Warp* warp = nullptr, bool needToTraceWarp = false, bool correctCorners = false)
    {

        /*ResultPoint p1(0, 0);
        ResultPoint p2(0, 0);
        ResultPoint p3(0, 0);
        ResultPoint p4(0, 0);
        createBitmapFromBitMatrix(image, p1, p2, p3, p4);*/

        BitMatrix newimage = image.copy();
        ResultPoint pointA, pointB, pointC, pointD;

        if(!DetectWhiteRect(newimage, pointA, pointB, pointC, pointD)) {
            for(const auto& rot : rotateEasy) {
                rotate(image, newimage, rot);
                if (DetectWhiteRect(newimage, pointA, pointB, pointC, pointD)) break;
            }
        }


        std::array transitions = {
                TransitionsBetween(newimage, pointA, pointB),
                TransitionsBetween(newimage, pointA, pointC),
                TransitionsBetween(newimage, pointB, pointD),
                TransitionsBetween(newimage, pointC, pointD),
        };

        std::sort(transitions.begin(), transitions.end(),
                  [](const auto& a, const auto& b) { return a.transitions < b.transitions; });


        DetectorResult res;
        int n1, n2;

        //���������� L �� ���� ��������� �� image � ������� DetectNew ��� ������� (��� � ������ L ���� ����������� � ���� �����)
        BitMatrix img2;
        for (int i = 0; i < 2; i++) {
            if (i == 0) { n1 = 0; n2 = 1; }
            if (i == 1) { n1 = 0; n2 = 2; }

            img2 = newimage.copy();
            line2(img2, transitions[n1].from->x(), transitions[n1].from->y(), transitions[n1].to->x(), transitions[n1].to->y());
            line2(img2, transitions[n2].from->x(), transitions[n2].from->y(), transitions[n2].to->x(), transitions[n2].to->y());

            res = DetectNew(img2, true, true, warp, needToTraceWarp, correctCorners);
            if (!res.isValid()) continue;
            if (outDecodeResult = Decode(res.bits()); outDecodeResult.isValid()) return res;

        } //i


        //������������ �������������� �������� �������, 4 ��������: �����������/���������+��������
        //� ���� ����� ������������ ���3, L1, ������� � �.�, �������� ����������� �� � ��������
        // BitMatrix img2;
        for (int i = 0; i < 4; i++) {
            n1 = 0; n2 = 1;

            correctBottle(newimage, img2, i & 0b10, i & 0b01);

            res = DetectNew(img2, true, true, warp, needToTraceWarp, correctCorners);
            if (!res.isValid()) continue;
            if (outDecodeResult = Decode(res.bits()); outDecodeResult.isValid()) return res;
        } //i
        return res;
    }
















/**
* This method detects a code in a "pure" image -- that is, pure monochrome image
* which contains only an unrotated, unskewed, image of a code, with some optional white border
* around it. This is a specialized method that works exceptionally fast in this special
* case.
*/
    static DetectorResult DetectPure(const BitMatrix& image)
    {
        return {};

        //createBitmapFromBitMatrix(image);

        //MessageBoxA(0, "Pure1", NULL, MB_OK | MB_ICONINFORMATION);

        int left, top, width, height;
        if (!image.findBoundingBox(left, top, width, height, 8))
            return {};



        //MessageBoxA(0, "Pure2", NULL, MB_OK | MB_ICONINFORMATION);


        BitMatrixCursorI cur(image, {left, top}, {0, 1});
        if (cur.countEdges(height - 1) != 0)
            return {};
        cur.turnLeft();
        if (cur.countEdges(width - 1) != 0)
            return {};
        cur.turnLeft();
        int dimR = cur.countEdges(height - 1) + 1;
        cur.turnLeft();
        int dimT = cur.countEdges(width - 1) + 1;

        auto modSizeX = float(width) / dimT;
        auto modSizeY = float(height) / dimR;
        auto modSize = (modSizeX + modSizeY) / 2;

        if (dimT % 2 != 0 || dimR % 2 != 0 || dimT < 10 || dimT > 144 || dimR < 8 || dimR > 144
            || std::abs(modSizeX - modSizeY) > 1
            || !image.isIn(PointF{left + modSizeX / 2 + (dimT - 1) * modSize, top + modSizeY / 2 + (dimR - 1) * modSize}))
            return {};

        int right  = left + width - 1;
        int bottom = top + height - 1;

        // Now just read off the bits (this is a crop + subsample)
        return {Deflate(image, dimT, dimR, top + modSizeX / 2, left + modSizeY / 2, modSize),
                {{left, top}, {right, top}, {right, bottom}, {left, bottom}}};
    }

    DetectorResults Detect(const BitMatrix& image, bool tryHarder, bool tryRotate, bool isPure)
    {
#ifdef __cpp_impl_coroutine
        // First try the very fast DetectPure() path. Also because DetectNew() generally fails with pure module size 1 symbols
	// TODO: implement a tryRotate version of DetectPure, see #590.
	if (auto r = DetectPure(image); r.isValid())
		co_yield std::move(r);
	else if (!isPure) { // If r.isValid() then there is no point in looking for more (no-pure) symbols
		bool found = false;
		for (auto&& r : DetectNew(image, tryHarder, tryRotate)) {
			found = true;
			co_yield std::move(r);
		}
		if (!found && tryHarder) {
			if (auto r = DetectOld(image); r.isValid())
				co_yield std::move(r);
		}
	}
#else
        if (isPure)
            return DetectPure(image);

        auto result = DetectNew(image, tryHarder, tryRotate);
        DecoderResult outDecoderResult;
        //if (!result.isValid() && tryHarder)
        //	result = DetectPure(image);
        if (!result.isValid() && tryHarder)
            result = DetectCRPT(image, outDecoderResult);
        return result;

#endif
    }

//    const int CommonMatrixDimensions[] = { 20, 22, 24, 26, 32, 36, 40, 44 };
DetectorResults DetectSamplegridV1(const BitMatrix& image, bool tryHarder, bool tryRotate, bool isPure, DecoderResult& outDecoderResult)
{

	#ifdef __cpp_impl_coroutine
		DetectorResult detRes;
		//OLD DETECTORS
		detRes = DetectNew(image, tryHarder, tryRotate);
		if (!detRes.isValid())
			detRes = DetectCRPT(image.copy());

		if (detRes.isValid()) {
			outDecoderResult = Decode(detRes.bits());
			if(outDecoderResult.isValid()) {
				co_return detRes;
			}
		}
		//#OLD DETECTORS

		//OLD DETECTORS WITH MY SAMPLE GRID
		detRes = DetectNew(image, tryHarder, tryRotate, true, true);
		if (detRes.isValid()) {
			outDecoderResult = Decode(detRes.bits());
			if(outDecoderResult.isValid()) {
				co_return detRes;
			}
		}
		detRes = DetectCRPT(image.copy(), true, true);

		if (detRes.isValid()) {
			outDecoderResult = Decode(detRes.bits());
			if(outDecoderResult.isValid()) {
				co_return detRes;
			}
		}
		//OLD DETECTORS WITH MY SAMPLE GRID
		co_return {};
	#else
        QuadrilateralI resultCandidate;
		DetectorResult detRes;

        auto SetResultCandidate = [&](){
            if(detRes.isValid()) {
                resultCandidate = detRes.position();
            }
        };

		//OLD DETECTORS
        detRes = DetectOldWithOffsets(image, outDecoderResult);
        SetResultCandidate();
        if(outDecoderResult.isValid()) return detRes;

        detRes = DetectCRPT(image.copy(), outDecoderResult);
        SetResultCandidate();
        if(outDecoderResult.isValid()) return detRes;

        detRes = DetectNew(image, tryHarder, tryRotate);
        SetResultCandidate();
		if (detRes.isValid()) {
			outDecoderResult = Decode(detRes.bits());
			if(outDecoderResult.isValid()) {
				return detRes;
			}
		}
		//#OLD DETECTORS

		//OLD DETECTORS WITH MY SAMPLE GRID

		Warp warp;

		detRes = DetectNew(image, tryHarder, tryRotate, &warp, true);
        SetResultCandidate();
		if (detRes.isValid()) {
			outDecoderResult = Decode(detRes.bits());
			if(outDecoderResult.isValid()) {
				return detRes;
			}
		}

		detRes = DetectCRPT(image.copy(), outDecoderResult, &warp, true);
        SetResultCandidate();
        if(outDecoderResult.isValid()) return detRes;
		if (detRes.isValid()) {
			outDecoderResult = Decode(detRes.bits());
			if(outDecoderResult.isValid()) {
				return detRes;
			}
		}
		//#OLD DETECTORS WITH MY SAMPLE GRID
		return DetectorResults({}, std::move(resultCandidate));
	#endif
}

const int CommonMatrixDimensions[] = { 20, 22, 24, 26, 32, 36, 40, 44 };

DetectorResults DetectDefined(const BitMatrix& image, const PointF& P0, const PointF& P1, const PointF& P2, const PointF& P3, bool tryHarder, bool tryRotate, bool isPure, DecoderResult& outDecoderResult)
{
	DetectorResult detRes;

	//OLD DETECTORS
	detRes = DetectNew(image, tryHarder, tryRotate);
	if (!detRes.isValid())
		detRes = DetectCRPT(image.copy(), outDecoderResult);

	if (detRes.isValid()) {
		outDecoderResult = Decode(detRes.bits());
		if(outDecoderResult.isValid()) {
			return detRes;
		}
	}
	//#OLD DETECTORS

	//OLD DETECTORS WITH MY SAMPLE GRID

	Warp warp;

	detRes = DetectNew(image, tryHarder, tryRotate, &warp, true);
	if (detRes.isValid()) {
		outDecoderResult = Decode(detRes.bits());
		if(outDecoderResult.isValid()) {
			return detRes;
		}
	}
	detRes = DetectCRPT(image.copy(), outDecoderResult, &warp, true);

	if (detRes.isValid()) {
		outDecoderResult = Decode(detRes.bits());
		if(outDecoderResult.isValid()) {
			return detRes;
		}
	}
	//#OLD DETECTORS WITH MY SAMPLE GRID


        //MY DETECTOR
        // std::vector<double> cornersAsVector;
	//MY DETECTOR
	std::vector<double> cornersAsVector;

        // detRes = DetectNew(image, tryHarder, tryRotate, true, false);
        // outDecoderResult = Decode(detRes.bits());
        // if (outDecoderResult.isValid()) {
        // 	return detRes;
        // }
	// detRes = DetectNew(image, tryHarder, tryRotate, true, false);
	// outDecoderResult = Decode(detRes.bits());
	// if (outDecoderResult.isValid()) {
	// 	return detRes;
	// }

    //     // detRes = DetectNew(image, tryHarder, tryRotate, true, true);
    //     // outDecoderResult = Decode(detRes.bits());
    //     // if (outDecoderResult.isValid()) {
    //     // 	return detRes;
    //     // }
	// detRes = DetectNew(image, tryHarder, tryRotate, true, true);
	// outDecoderResult = Decode(detRes.bits());
	// if (outDecoderResult.isValid()) {
	// 	return detRes;
	// }

        // // for (int dim = 8; dim <= 44; dim+=2) {
        // for (int dim : CommonMatrixDimensions) {
	// for (int dim = 8; dim <= 44; dim+=2) {
	for (int dim : CommonMatrixDimensions) {

        // 	PointF P[] = {P0, P1, P2, P3};
        // 	CorrectCorners(image, P[0], P[1], P[2], P[3], dim);
        // 	int rotateSteps = FindRotation(image, P[0], P[1], P[2], P[3], dim);
        // 	if(rotateSteps > 0) {
        // 		PointF PP[4];
        // 		std::copy(P, &P[4], PP);
        // 		for(int i = 4; i--;) {
        // 			P[(i+rotateSteps) % 4] = PP[i];
        // 		}
        // 	}
        // 	//DEBUG DRAW
		PointF P[] = {P0, P1, P2, P3};
		CorrectCorners(image, P[0], P[1], P[2], P[3], dim);
		int rotateSteps = FindRotation(image, P[0], P[1], P[2], P[3], dim);
		if(rotateSteps > 0) {
			PointF PP[4];
			std::copy(P, &P[4], PP);
			for(int i = 4; i--;) {
				P[(i+rotateSteps) % 4] = PP[i];
			}
		}
		//DEBUG DRAW

        // 	auto postfix = std::to_string(dim);
		auto postfix = std::to_string(dim);

        // 	// cornersAsVector = {P[0].x, P[0].y, P[1].x, P[1].y, P[2].x, P[2].y, P[3].x, P[3].y};
        // 	// drawDebugImageWithLines(image, filename, cornersAsVector);
		// cornersAsVector = {P[0].x, P[0].y, P[1].x, P[1].y, P[2].x, P[2].y, P[3].x, P[3].y};
		// drawDebugImageWithLines(image, filename, cornersAsVector);

        // 	//END DEBUG DRAW
		//END DEBUG DRAW

        // 	auto&& [TL, BL, BR, TR] = P;
		auto&& [TL, BL, BR, TR] = P;

        // 	detRes = SampleGridWarped(image, TL, BL, BR, TR, dim, dim);
        // 	if (detRes.isValid()) {
        // 		outDecoderResult = Decode(detRes.bits());
        // 		if (outDecoderResult.isValid()) {
        // 			// cornersAsVector = {P[0].y, P[0].x, P[1].y, P[1].x, P[2].y, P[2].x, P[3].y, P[3].x};
        // 			// drawDebugImageWithLines(image, postfix, cornersAsVector);
        // 			// drawDebugImage(detRes.bits(), postfix);
        // 			return detRes;
        // 		}
        // 	}
        // }
        //#MY DETECTOR

		auto warp = ComputeWarp(image, TL, BL, BR, TR, dim, dim, dim);

		detRes = SampleGridWarped(image, TL, BL, BR, TR, dim, dim, warp);
		if (detRes.isValid()) {
			outDecoderResult = Decode(detRes.bits());
			if (outDecoderResult.isValid()) {
				// cornersAsVector = {P[0].y, P[0].x, P[1].y, P[1].x, P[2].y, P[2].x, P[3].y, P[3].x};
				// drawDebugImageWithLines(image, postfix, cornersAsVector);
				// drawDebugImage(detRes.bits(), postfix);
				return detRes;
			}
		}
	}
	//#MY DETECTOR

    return {};
}
} // namespace ZXing::DataMatrix

//DEBUG DRAW

// auto postfix = std::to_string(dim);
// auto filename = std::to_string(fileCntr);
// filename = std::string(4 - filename.length(), '0') + filename;
// filename += "_" + postfix + ".png";

// cornersAsVector = {TL.x, TL.y, BL.x, BL.y, BR.x, BR.y, TR.x, TR.y};
// drawDebugImageWithLines(image, filename, cornersAsVector);

//END DEBUG DRAW
