/*
* Copyright 2016 Nu-book Inc.
* Copyright 2016 ZXing authors
* Copyright 2017 Axel Waggershauser
*/
// SPDX-License-Identifier: Apache-2.0



#include "DMDetector.h"
#include "DMLadder.h"

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
#include "opencv2/opencv.hpp"
#include <opencv2/core/hal/intrin.hpp>
#include <memory>
// #include <chrono>


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

#define VEC_SIZE 8

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
	
	template <typename Func>
	void RasterizeTriangle(const PointF& v1, const PointF& v2, const PointF& v3, const BitMatrix& targetImage, Func ProcessPixel) {
		PointF vertices[3] = {v1, v2, v3};
		std::sort(vertices, vertices + 3, [](const PointF& a, const PointF& b) {
			return a.y < b.y;
		});

		PointF A = vertices[0];
		PointF B = vertices[1];
		PointF C = vertices[2];

		float dx1 = B.y != A.y ? (B.x - A.x) / (B.y - A.y) : 0.0;
		float dx2 = C.y != A.y ? (C.x - A.x) / (C.y - A.y) : 0.0;
		float dx3 = C.y != B.y ? (C.x - B.x) / (C.y - B.y) : 0.0;

		double mul = std::round(A.y) + 0.5 - A.y;
		float x1incr = dx1;
		float x2incr = dx2;
		if(dx1 >= dx2) {
			std::swap(x1incr, x2incr);
		}
		float x1 = A.x + x1incr * mul;
		float x2 = A.x + x2incr * mul;

		for (int y = std::round(A.y); y <= std::floor(B.y - 0.5); y++) {
			int x = x1 + 0.5;
			int endX = x2 + 0.5;
			for (; x < endX; x++) {
				ProcessPixel({x, y});
			}
			x1 += x1incr;
			x2 += x2incr;
		}

		mul = std::round(B.y) + 0.5 - B.y;
		if (dx1 > dx2) {
			x2 = B.x + mul * dx3;
			x1incr = dx2;
			x2incr = dx3;
		} else {
			x1 = B.x + mul * dx3;
			x1incr = dx3;
			x2incr = dx2;
		}
		if (x2 < x1) {
			std::swap(x1, x2);
			std::swap(x1incr, x2incr);
		}

		for (int y = std::round(B.y); y <= std::floor(C.y - 0.5); ++y) {
			int x = x1 + 0.5;
			int endX = x2 + 0.5;
			for (; x < endX; x++) {
				ProcessPixel({x, y});
			}
			x1 += x1incr;
			x2 += x2incr;
		}
	}

	int8_t testCenterLineOffset(const BitMatrix& img) {
		if(img.width() < 32 || img.width() > 52) return 0;
		uint8_t lineInfoCnt[4];
		for(int i = 4; i--;){
			lineInfoCnt[i] = 1.0f;
		}
		int startY = img.height() / 2 - 2;

		for(int yo = 0; yo < 4; yo++) {
			int y = startY + yo;
			uint8_t curState = img.get(0, y);
			for(int x = 1; x < img.width(); x++) {
				auto v = img.get(x, y);
				if(curState != v) {
					lineInfoCnt[yo]++;
					curState = v;
				}
			}
		}

		int indexSync = std::min_element(lineInfoCnt, lineInfoCnt + 4) - lineInfoCnt;
		int indexLine = std::max_element(lineInfoCnt, lineInfoCnt + 4) - lineInfoCnt;
		int minLine = std::min(indexSync, indexLine);
		return minLine - 1;
	}



	std::pair<int8_t, int8_t> testCenterLineBiOffset(const BitMatrix& img) {
		int8_t offsetY = testCenterLineOffset(img);
		auto imgRotated = img.copy();
		imgRotated.rotate90();
		int8_t offsetX = testCenterLineOffset(imgRotated);
		return {offsetX, offsetY};
	}

	struct DoubleLineFlags {
		bool top : 1;
		bool bottom : 1;
		bool left : 1;
		bool right : 1;
		
		bool any() {
			return top || bottom || left || right;
		};
	};

	DoubleLineFlags testDoubleLine(const BitMatrix& img) {
		
		auto testStartY = [](const BitMatrix& img, int startY) -> uint8_t {
			for(int yo = 0; yo < 2; yo++) {
				uint8_t lineInfoCnt = 0;
				int y = startY + yo;
				uint8_t curState = img.get(0, y);
				for(int x = 1; x < img.width(); x++) {
					auto v = img.get(x, y);
					if(curState != v) {
						lineInfoCnt++;
						curState = v;
					}
				}
				if(lineInfoCnt>4)
					return 0;
			}
			return 1;
		};

		DoubleLineFlags res;

		res.top = testStartY(img, 0);
		res.bottom = testStartY(img, img.height() - 2);
		auto imgRotated = img.copy();
		imgRotated.rotate90();
		res.left = testStartY(imgRotated, 0);
		res.right = testStartY(imgRotated, imgRotated.height() - 2);
		return res;
	}

	double FindMaxIslandArea(const BitMatrix& image, const PointF& p0, const PointF& p1, const PointF& p2, const PointF& p3) {
		BitMatrix targetImage(image.width(), image.height());
		int totalArea = 0;
		auto FillSame = [&targetImage, &image, &totalArea](const PointI& p){
			targetImage.set(p, !image.get(p));
			totalArea++;
		};
		RasterizeTriangle(p0, p1, p2, targetImage, FillSame);
		RasterizeTriangle(p2, p3, p0, targetImage, FillSame);

		int expandDist = 1;

		for(int y = image.height(); y--;){
			for(int x = 0; x < image.width() - expandDist; x++) {
				if(!targetImage.get(x + 1, y)) {
					targetImage.set(x, y, false);
				}
			}
			for(int x = image.width(); x-- > expandDist; ) {
				if(!targetImage.get(x - 1, y)) {
					targetImage.set(x, y, false);
				}
			}
		}
		for(int x = image.width(); x--;){
			for(int y = 0; y < image.height() - expandDist; y++) {
				if(!targetImage.get(x, y + 1)) {
					targetImage.set(x, y, false);
				}
			}
			for(int y = image.height(); y-- > expandDist; ) {
				if(!targetImage.get(x, y - 1)) {
					targetImage.set(x, y, false);
				}
			}
		}

		int maxArea = 0;
		std::vector<PointI> checkStack;

		// drawDebugImage(targetImage, "square");

		checkStack.reserve(image.width() * image.height() / 2);
		auto StartFill = [&targetImage, &checkStack, &maxArea](const PointI& p) {
			if(!targetImage.get(p)) return;
			int curArea = 0;
			checkStack.push_back({p.x, p.y});
			targetImage.set(p, false);
			while(checkStack.size() > 0) {
				auto curP = checkStack.back();
				checkStack.pop_back();
				curArea++;
				if(curP.x > 0 && targetImage.get(curP.x - 1, curP.y)){
					targetImage.set(curP.x - 1, curP.y, false);
					checkStack.push_back({curP.x - 1, curP.y});
				}
				if(curP.y > 0 && targetImage.get(curP.x, curP.y - 1)){
					targetImage.set(curP.x, curP.y - 1, false);
					checkStack.push_back({curP.x, curP.y - 1});
				}
				if(curP.x < targetImage.width() - 1 && targetImage.get(curP.x + 1, curP.y)){
					targetImage.set(curP.x + 1, curP.y, false);
					checkStack.push_back({curP.x + 1, curP.y});
				}
				if(curP.y < targetImage.height() - 1 && targetImage.get(curP.x, curP.y + 1)){
					targetImage.set(curP.x, curP.y + 1, false);
					checkStack.push_back({curP.x, curP.y + 1});
				}
			}
			// drawDebugImage(targetImage, "square");
			if(curArea > maxArea) {
				maxArea = curArea;
			}
		};
		for(int y = image.height(); y--;){
			for(int x = image.width(); x--;) {
				StartFill({x,y});
			}
		}
		// RasterizeTriangle(p0, p1, p2, targetImage, StartFill);
		// RasterizeTriangle(p2, p3, p0, targetImage, StartFill);
		return maxArea / (double)totalArea;
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

        ResultPoint c1(topRight.x() + corr * cos, topRight.y() + corr * sin);

        corr = RoundToNearestF(distance(bottomLeft, topLeft)) / (float)dimensionRight;
        norm = RoundToNearestF(distance(bottomRight, topRight));
        cos = (topRight.x() - bottomRight.x()) / norm;
        sin = (topRight.y() - bottomRight.y()) / norm;

        ResultPoint c2(topRight.x() + corr * cos, topRight.y() + corr * sin);

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
    static void ExtendSide(const BitMatrix& image, const ResultPoint& bottomLeft, ResultPoint& bottomRight,
                                       const ResultPoint& topLeft, ResultPoint& topRight, int dimension)
    {
        PointF DirTop = (topRight - topLeft) / dimension;
        PointF DirBottom = (bottomRight - bottomLeft) / dimension;
        topRight += DirTop;
        bottomRight += DirBottom;
    }

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
                          { Rectangle(width, height, 0.5), {topLeft, topRight, bottomRight, bottomLeft} });
    }

	static DecoderResult DecodeResult(const DetectorResult& det)
	{
		return Decode(det.bits());
	}

	/**
	 * Targeted grid refine using Reed-Solomon error locations.
	 * Mis-sampled modules (from corrected codewords) are re-sampled with a small shared
	 * neighbor offset — crumple bias is assumed coherent across nearby errors.
	 */
	static DetectorResult RefineGridWithDecoderFeedback(const BitMatrix& image, int width, int height,
														PerspectiveTransform mod2pix, DetectorResult bestRes,
														DecodeScore bestScore)
	{
		if (!bestRes.isValid())
			return bestRes;

		// Need concrete RS error sites; if every block failed Euclidean, nothing to aim at.
		if (bestScore.errorModules.empty()) {
			bestScore = EvaluateDecode(bestRes.bits());
			if (bestScore.errorModules.empty())
				return bestRes;
		}

		// Filter modules inside the sampled grid.
		std::vector<PointI> mods;
		mods.reserve(bestScore.errorModules.size());
		for (auto m : bestScore.errorModules) {
			if (m.x >= 0 && m.x < width && m.y >= 0 && m.y < height)
				mods.push_back(m);
		}
		if (mods.empty())
			return bestRes;

		PointF stepX = mod2pix(PointF{1.5, 0.5}) - mod2pix(PointF{0.5, 0.5});
		PointF stepY = mod2pix(PointF{0.5, 1.5}) - mod2pix(PointF{0.5, 0.5});
		if (length(stepX) < 1e-3 || length(stepY) < 1e-3)
			return bestRes;

		const float fracs[] = {0.2f, 0.35f, 0.5f};
		const PointF dirs[] = {
			{1, 0}, {-1, 0}, {0, 1}, {0, -1},
			{1, 1}, {1, -1}, {-1, 1}, {-1, -1},
		};

		auto applyCoherentOffset = [&](PointF offset) -> DetectorResult {
			BitMatrix bits = bestRes.bits().copy();
			for (auto m : mods) {
				PointF p = mod2pix(centered(m)) + offset;
				if (!image.isIn(p))
					continue;
				if (image.get(p))
					bits.set(m.x, m.y, true);
				else
					bits.set(m.x, m.y, false);
			}
			return {std::move(bits), QuadrilateralI(bestRes.position())};
		};

		for (float frac : fracs) {
			for (const auto& d : dirs) {
				PointF offset = (frac * d.x) * stepX + (frac * d.y) * stepY;
				auto cand = applyCoherentOffset(offset);
				if (!cand.isValid())
					continue;
				auto score = EvaluateDecode(cand.bits());
				if (score.score() < bestScore.score()) {
					bestScore = score;
					bestRes = std::move(cand);
					if (bestScore.ok && bestScore.errorsCorrected == 0)
						return bestRes;
					// Refresh target modules if RS found a better / different set.
					if (!bestScore.errorModules.empty()) {
						mods.clear();
						for (auto m : bestScore.errorModules) {
							if (m.x >= 0 && m.x < width && m.y >= 0 && m.y < height)
								mods.push_back(m);
						}
						if (mods.empty())
							return bestRes;
					}
				}
			}
		}

		// Optional light per-module pass: only modules whose neighbor sample differs, shared bias from
		// the best coherent direction (already applied in bestRes). Try a few local alternatives.
		if ((!bestScore.ok || bestScore.errorsCorrected > 0) && !mods.empty()) {
			const PointF localDirs[] = {
				{0, 0},
				0.3 * stepX, -0.3 * stepX, 0.3 * stepY, -0.3 * stepY,
			};
			BitMatrix bits = bestRes.bits().copy();
			int evalBudget = 24;
			for (auto m : mods) {
				if (evalBudget <= 0)
					break;
				PointF base = mod2pix(centered(m));
				bool curBit = bits.get(m.x, m.y);
				for (const auto& ld : localDirs) {
					PointF p = base + ld;
					if (!image.isIn(p))
						continue;
					bool bit = image.get(p);
					if (bit == curBit)
						continue;
					bits.set(m.x, m.y, bit);
					--evalBudget;
					auto score = EvaluateDecode(bits);
					if (score.score() < bestScore.score()) {
						bestScore = score;
						curBit = bit;
						bestRes = DetectorResult{bits.copy(), QuadrilateralI(bestRes.position())};
						if (bestScore.ok && bestScore.errorsCorrected == 0)
							return bestRes;
					} else {
						bits.set(m.x, m.y, curBit);
					}
				}
			}
		}

		return bestRes;
	}

	DetectorResult SampleGridTestOffseted(const BitMatrix& image, int width, int height, PerspectiveTransform mod2pix,
										  DMGridRefineOptions gridRefine = {}) {
        auto res = SampleGrid(image, width, height, mod2pix);
		auto doubleLine = testDoubleLine(res.bits());

		if(doubleLine.any()) {
			PointF tl = mod2pix({0, 0});
			PointF tr = mod2pix({static_cast<float>(width), 0});
			PointF bl = mod2pix({0, static_cast<float>(height)});
			PointF br = mod2pix({ static_cast<float>(width), static_cast<float>(height)});

			float dimInv = 0.62 / float(width);
			PointF DirTopLR = dimInv * (tr - tl);
			PointF DirBottomLR = dimInv * (br - bl);
			PointF DirRightBT = dimInv * (tr - br);
			PointF DirLeftBT = dimInv * (tl - bl);
			if(doubleLine.left) {
				tl += DirTopLR;
				bl += DirBottomLR;
			}
			if(doubleLine.right) {
				tr += -DirTopLR;
				br += -DirBottomLR;
			}
			if(doubleLine.top) {
				tl += -DirLeftBT;
				tr += -DirRightBT;
			}
			if(doubleLine.bottom) {
				bl += DirLeftBT;
				br += DirRightBT;
			}
			mod2pix = { Rectangle(width, height, 0), {tl, tr, br, bl} };
			res = SampleGrid(image, width, height, mod2pix);
		}

		auto [xMul, yMul] = testCenterLineBiOffset(res.bits());
		if(xMul != 0 || yMul != 0) {
			PointF xo = {float(xMul), 0.0f};
			PointF yo = {0.0f, float(yMul)};
			Warp w({{}, xo, {}}, {{}, yo, {}});
			w.isFinal = false;
			w.Resample(width, height);
			res = SampleGridWarped(image, width, height, w, mod2pix);
		}

		if (!gridRefine.any())
			return res;

		// 1) Region-growing grid from the L-corner if still not cleanly decodable.
		if (gridRefine.regionGrowing) {
			auto scoreCur = res.isValid() ? EvaluateDecode(res.bits()) : DecodeScore{};
			if (!scoreCur.ok) {
				auto grown = SampleGridRegionGrowing(image, width, height, mod2pix);
				if (grown.isValid()) {
					auto scoreGrown = EvaluateDecode(grown.bits());
					if (scoreGrown.score() < scoreCur.score()) {
						res = std::move(grown);
						scoreCur = scoreGrown;
					}
				}
			}
		}

		// 2) Decoder-feedback: re-sample RS-flagged modules with coherent neighbor offsets.
		if (gridRefine.rsFeedback) {
			auto scoreCur = res.isValid() ? EvaluateDecode(res.bits()) : DecodeScore{};
			if (!scoreCur.errorModules.empty()) {
				res = RefineGridWithDecoderFeedback(image, width, height, mod2pix, std::move(res), scoreCur);
			}
		}

		return res;
	}


	static DetectorResult SampleGridTestOffseted(const BitMatrix& image, const ResultPoint& topLeft, const ResultPoint& bottomLeft,
									const ResultPoint& bottomRight, const ResultPoint& topRight, int width, int height,
									DMGridRefineOptions gridRefine = {})
	{
		PerspectiveTransform mod2pix = { Rectangle(width, height, 0.5), {topLeft, topRight, bottomRight, bottomLeft} };

		return SampleGridTestOffseted(image, width, height, mod2pix, gridRefine);
	}

    static DetectorResult SampleGridWarped(const BitMatrix& image, const ResultPoint& topLeft, const ResultPoint& bottomLeft,
                                           const ResultPoint& bottomRight, const ResultPoint& topRight, int width, int height, const Warp& warp)
    {
        return SampleGridWarped(image, width, height, warp,
                                { Rectangle(width, height, 0.5), {topLeft, topRight, bottomRight, bottomLeft} });
    }

    /**
    * Returns the z component of the cross product between vectors BC and BA.
    */
    static float CrossProductZ(const ResultPoint& a, const ResultPoint& b, const ResultPoint& c)
    {
        return (c.x() - b.x()) * (a.y() - b.y()) - (c.y() - b.y()) * (a.x() - b.x());
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

	static bool TestDetectOld(const BitMatrix& image)
	{
		ResultPoint pointA, pointB, pointC, pointD;
        if (!DetectWhiteRect(image, pointA, pointB, pointC, pointD))
            return false;

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
            return false;

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
            return false;

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

        if (dimensionTop < 10 || dimensionTop > 144 || dimensionRight < 8 || dimensionRight > 144)
            return false;
		
		return true;
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

        if (dimensionTop < 10 || dimensionTop > 144 || dimensionRight < 8 || dimensionRight > 144)
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

	// static bool DetectWhiteRectWithSort(const BitMatrix& image, ResultPoint& br, ResultPoint& bl, ResultPoint& tl, ResultPoint& tr)
	// {
	// 	ResultPoint pointA, pointB, pointC, pointD;
    //     if (!DetectWhiteRect(image, pointA, pointB, pointC, pointD))
    //         return {};

    //     // Point A and D are across the diagonal from one another,
    //     // as are B and C. Figure out which are the solid black lines
    //     // by counting transitions
    //     std::array transitions = {
    //             TransitionsBetween(image, pointA, pointB),
    //             TransitionsBetween(image, pointA, pointC),
    //             TransitionsBetween(image, pointB, pointD),
    //             TransitionsBetween(image, pointC, pointD),
    //     };
    //     std::sort(transitions.begin(), transitions.end(),
    //               [](const auto& a, const auto& b) { return a.transitions < b.transitions; });

    //     // Sort by number of transitions. First two will be the two solid sides; last two
    //     // will be the two alternating black/white sides
    //     const auto& lSideOne = transitions[0];
    //     const auto& lSideTwo = transitions[1];

    //     // We accept at most 4 transisions inside the L pattern (i.e. 2 corruptions) to reduce false positive FormatErrors
    //     if (lSideTwo.transitions > 8)
    //         return {};

    //     // Figure out which point is their intersection by tallying up the number of times we see the
    //     // endpoints in the four endpoints. One will show up twice.
    //     std::map<const ResultPoint*, int> pointCount;
    //     pointCount[lSideOne.from] += 1;
    //     pointCount[lSideOne.to] += 1;
    //     pointCount[lSideTwo.from] += 1;
    //     pointCount[lSideTwo.to] += 1;

    //     for (const auto& [point, count] : pointCount) {
    //         if (count == 2) {
    //             bottomLeft = point; // this is definitely the bottom left, then -- end of two L sides
    //         }
    //         else {
    //             // Otherwise it's either top left or bottom right -- just assign the two arbitrarily now
    //             if (bottomRight == nullptr) {
    //                 bottomRight = point;
    //             }
    //             else {
    //                 topLeft = point;
    //             }
    //         }
    //     }

    //     if (bottomRight == nullptr || bottomLeft == nullptr || topLeft == nullptr)
    //         return {};

    //     // Bottom left is correct but top left and bottom right might be switched
    //     // Use the dot product trick to sort them out
    //     OrderByBestPatterns(bottomRight, bottomLeft, topLeft);

    //     // Which point didn't we find in relation to the "L" sides? that's the top right corner
    //     const ResultPoint* topRight;
    //     if (pointCount.find(&pointA) == pointCount.end()) {
    //         topRight = &pointA;
    //     }
    //     else if (pointCount.find(&pointB) == pointCount.end()) {
    //         topRight = &pointB;
    //     }
    //     else if (pointCount.find(&pointC) == pointCount.end()) {
    //         topRight = &pointC;
    //     }
    //     else {
    //         topRight = &pointD;
    //     }
	// }

    static DetectorResult DetectOldWithOffsets(const BitMatrix& image, DecoderResult& outDecoderResult, bool& correctedOffset,
											   DMGridRefineOptions gridRefine = {})
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

        if (dimensionTop < 10 || dimensionTop > 144 || dimensionRight < 8 || dimensionRight > 144)
            return {};


		

        ResultPoint correctedTopRight;
        {
            // The matrix is square

            int dimension = std::max(dimensionRight, dimensionTop);

            // ResultPoint topRightCorrected = *topRight;
            // ResultPoint bottomRightCorrected = *bottomRight;
            // ResultPoint topLeftCorrected = *topLeft;
            // ResultPoint bottomLeftCorrected = *bottomLeft;
            DetectorResult res;
            float dimInv = 1.0 / float(dimension);
            PointF DirTopLR = dimInv * (*topRight - *topLeft);
            PointF DirBottomLR = dimInv * (*bottomRight - *bottomLeft);

            PointF DirRightBT = dimInv * (*topRight - *bottomRight);
            PointF DirLeftBT = dimInv * (*topLeft - *bottomLeft);

            dimensionTop = dimensionRight = dimension;

            auto sgDebug = [&](const BitMatrix& image, const ResultPoint& topLeft, const ResultPoint& bottomLeft, const ResultPoint& bottomRight, const ResultPoint& topRight, int width, int height) {
                // drawDebugImageWithLines(image, "test3", {topLeft.x(), topLeft.y(), topRight.x(), topRight.y(),
                // bottomRight.x(), bottomRight.y(), bottomLeft.x(), bottomLeft.y()});
                return SampleGrid(image, topLeft, bottomLeft, bottomRight, topRight, dimensionTop, dimensionRight);
            };

            correctedOffset = true;
			bool tryInvert = false;
			for(int i = 1; i--; ) {
				res = SampleGrid(image, *topLeft, *bottomLeft, *bottomRight + DirBottomLR, *topRight + DirTopLR, dimensionTop, dimensionRight);
				if(outDecoderResult = DecodeResult(res); outDecoderResult.isValid()) return res;

				res = SampleGrid(image, *topLeft + DirLeftBT, *bottomLeft, *bottomRight, *topRight + DirRightBT, dimensionTop, dimensionRight);
				if(outDecoderResult = DecodeResult(res); outDecoderResult.isValid()) return res;


				correctedTopRight = CorrectTopRight(image, *bottomLeft, *bottomRight, *topLeft, *topRight, dimension);

				DirTopLR = dimInv * (correctedTopRight - *topLeft);
				DirRightBT = dimInv * (correctedTopRight - *bottomRight);

				res = SampleGrid(image, *topLeft - DirTopLR, *bottomLeft - DirBottomLR, *bottomRight, correctedTopRight, dimensionTop, dimensionRight);
				if(outDecoderResult = DecodeResult(res); outDecoderResult.isValid()) return res;

				res = SampleGrid(image, *topLeft, *bottomLeft - DirLeftBT, *bottomRight - DirRightBT, correctedTopRight, dimensionTop, dimensionRight);
				if(outDecoderResult = DecodeResult(res); outDecoderResult.isValid()) return res;
				if(tryInvert) {
					tryInvert = false;
					DirTopLR = -0.5 * DirTopLR;
					DirBottomLR = -0.5 * DirBottomLR;

					DirRightBT = -0.5 * DirRightBT;
					DirLeftBT = -0.5 * DirLeftBT;
				}
			}

            // ExtendSide(image, *bottomLeft, bottomRightCorrected, *topLeft, topRightCorrected, dimension);
            // correct top right point to match the white module
            // correctedTopRight = CorrectTopRight(image, *bottomLeft, *bottomRight, *topLeft, *topRight, dimension);
            // correctedTopRight = CorrectTopRight(image, *bottomLeft, bottomRightCorrected, *topLeft, topRightCorrected, dimension);

            // Redetermine the dimension using the corrected top right point
            // int dimensionCorrected = std::max(TransitionsBetween(image, *topLeft, correctedTopRight).transitions,
            //                                   TransitionsBetween(image, *bottomRight, correctedTopRight).transitions);
            // int dimensionCorrected = dimension;

            // int dimensionCorrected = std::max(TransitionsBetween(image, *topLeft, topRightCorrected).transitions,
            //                                   TransitionsBetween(image, bottomRightCorrected, correctedTopRight).transitions);
            // dimensionCorrected++;
            // if ((dimensionCorrected & 0x01) == 1) {
            //     dimensionCorrected++;
            // }
            correctedOffset = false;
			res = SampleGridTestOffseted(image, *topLeft, *bottomLeft, *bottomRight, correctedTopRight, dimensionTop, dimensionRight,
										 gridRefine);
            // res = SampleGrid(image, *topLeft, *bottomLeft, *bottomRight, correctedTopRight, dimensionTop, dimensionRight);
			// auto testValue = testCenterLineBiOffset(res.bits());
            outDecoderResult = DecodeResult(res);
            return res;
        }

        // DetectorResult res = SampleGrid(image, *topLeft, *bottomLeft, *bottomRight, correctedTopRight, dimensionTop, dimensionRight);

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
            auto meanModSize = average(modSizes, [](double dist) { return dist > 0; });

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
                    for (int i = 0; i <= 2 * (step / 4 + 1) * breadth; ++i) {
                        auto pEdge = p + step * d + (i & 1 ? (i + 1) / 2 : -i / 2) * dEdge;
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
                    }
                    else if (gaps == 0 && Size(line.points()) >= 2 * maxStepSize) {
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
            if (isWhite())
                return true;

            steps = e2e.stepToNextEdge(INT_MAX);
            if (!steps)
                return false;
            return step(steps);
        }
    };


    static DetectorResult Scan(EdgeTracer& startTracer, std::array<DMRegressionLine, 4>& lines, Warp* warp = nullptr, bool tryToTraceWarp = false, bool correctCorners = false,
							   DMGridRefineOptions gridRefine = {})
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

            for (auto* l : { &lineL, &lineB, &lineT, &lineR })
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
            if (tryToTraceWarp && warp) {
                if (!warp->isValid() || warp->xOffsets.size() > dimT || warp->yOffsets.size() > dimR) {
                    *warp = ComputeWarp(*startTracer.img, tl, bl, br, tr, 5, 5, dimT);
                }
                // warp->Resample(dimT, dimR);
            }
            if (warp) {
                if (warp->xOffsets.size() != dimT || warp->yOffsets.size() != dimR) {
                    warp->Resample(dimT, dimR);
                }
                if (correctCorners) {
                    auto TL = tl, BL = bl, BR = br, TR = tr;
                    CorrectCorners(*startTracer.img, TL, BL, BR, TR, dimT);
                    res = SampleGridWarped(*startTracer.img, TL, BL, BR, TR, dimT, dimR, *warp);
                }
                else {
                    res = SampleGridWarped(*startTracer.img, dimT, dimR, *warp, PerspectiveTransform(Rectangle(dimT, dimR, 0), sourcePoints));
                }
				if (res.isValid() && (gridRefine.regionGrowing || gridRefine.rsFeedback)) {
					auto scoreWarp = EvaluateDecode(res.bits());
					if (!scoreWarp.ok) {
						PerspectiveTransform mod2pix{Rectangle(dimT, dimR, 0), sourcePoints};
						if (gridRefine.regionGrowing) {
							auto grown = SampleGridRegionGrowing(*startTracer.img, dimT, dimR, mod2pix);
							if (grown.isValid() && EvaluateDecode(grown.bits()).score() < scoreWarp.score()) {
								res = std::move(grown);
								scoreWarp = EvaluateDecode(res.bits());
							}
						}
						if (gridRefine.rsFeedback && !scoreWarp.errorModules.empty())
							res = RefineGridWithDecoderFeedback(*startTracer.img, dimT, dimR, mod2pix, std::move(res), scoreWarp);
					}
				}
            }
            else {
                res = SampleGridTestOffseted(*startTracer.img, dimT, dimR, PerspectiveTransform(Rectangle(dimT, dimR, 0), sourcePoints),
											 gridRefine);
            }

            CHECK(res.isValid());
            return res;
        }

        return {};
    }


    static DetectorResults DetectNew(const BitMatrix& image, bool tryHarder, bool tryRotate, Warp* warp = nullptr, bool tryToTraceWarp = false, bool correctCorners = false,
									 DMGridRefineOptions gridRefine = {})
    {
#ifdef PRINT_DEBUG
        LogMatrixWriter lmw(log, image, 1, "dm-log.pnm");
        //	tryRotate = tryHarder = false;
#endif

        // disable expensive multi-line scan to detect off-center symbols for now
        tryHarder = false;

        //ResultPoint p1;
        //createBitmapFromBitMatrix(image,p1,p1,p1,p1);

        // a history log to remember where the tracing already passed by to prevent a later trace from doing the same work twice
        ByteMatrix history;
        if (tryHarder)
            history = ByteMatrix(image.width(), image.height());

        // instantiate RegressionLine objects outside of Scan function to prevent repetitive std::vector allocations
        std::array<DMRegressionLine, 4> lines;

        constexpr int minSymbolSize = 8 * 2; // minimum realistic size in pixel: 8 modules x 2 pixels per module

        for (auto dir : { PointF{-1, 0}, {1, 0}, {0, -1}, {0, 1} }) {
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

                if (auto res = Scan(tracer, lines, warp, tryToTraceWarp, correctCorners, gridRefine); res.isValid()) {
                    return res;
                }

                if (!tryHarder)
                    break; // only test center lines
            }

            if (!tryRotate)
                break; // only test left direction
        }

        return {};
    }


    void correctBottle(const BitMatrix& img, BitMatrix& outImg, bool horizontal, bool inverse) {

        outImg = img.copy();

        float factor = 15;

        int width = img.width();
        int height = img.height();


        if (horizontal) {

            int center = (int)(height / 2.0);
            factor = (float)center / 7.6;

            for (int y = 0; y < height; y++) {
				int dy = abs(y - center);
				int offset = (cos((float)dy / center) * factor - (factor * 0.75f));
				if(inverse) {
					for (int x = 0; x < width; x++) {
						outImg.set(std::clamp(x - offset, 0, width - 1), y, img.get(x, y));
					}
				} else {
					for (int x = 0; x < width; x++) {
						outImg.set(std::clamp(x + offset, 0, width - 1), y, img.get(x, y));
					}
				}
            }
        }
        else {


            int center = (int)(width / 2.0);
            factor = (float)center / 7.6;


			for (int x = 0; x < width; x++) {
				int dy = abs(x - center);
				int offset = (cos((float)dy / center) * factor - factor * 0.75f);
				if(inverse) {
					for (int y = 0; y < height; y++) {
						outImg.set(x, std::clamp(y - offset, 0, height - 1), img.get(x, y));
					}

				} else {
					for (int y = 0; y < height; y++) {
						outImg.set(x, std::clamp(y + offset, 0, height - 1), img.get(x, y));
					}

				}
            }
        }

    }

	float very_fast_cos(float x) {
		constexpr float two_pi = 2.0f * M_PI;
		constexpr float inv_two_pi = 1.0f / two_pi;
		
		x -= two_pi * static_cast<int>(x * inv_two_pi);
		
		if (x > M_PI) x = two_pi - x;
		float x_abs = (x < 0) ? -x : x;
		
		constexpr float a = -0.25f;
		constexpr float b = 1.0f;
		
		return a * x_abs * x_abs + b;
	}

    void createMaps(cv::Mat& mapXY, int outputSize, bool horizontal, bool inverse) {
        mapXY.create(outputSize, outputSize, CV_32FC2);

        float factor = float(outputSize) / 7.6 * 0.5;

        static cv::Mat offsetMap;

		constexpr size_t alignment = VEC_SIZE * 4;
		constexpr size_t elements_per_vector = VEC_SIZE;
		// auto start = std::chrono::high_resolution_clock::now();

        if(offsetMap.cols != outputSize) {
            offsetMap = cv::Mat(1, outputSize, CV_32F);

            float indexMul = 2.0 / float(outputSize - 1);
            size_t i = 0;
			float* mapRow = offsetMap.ptr<float>(0);

			while ((reinterpret_cast<uintptr_t>(mapRow + i) & (alignment - 1)) != 0 && i < outputSize) {
				mapRow[i] = (very_fast_cos(std::fabs(float(i) * indexMul - 1.0f)) - 0.75) * factor;
				i++;
			}

			if (i + elements_per_vector <= outputSize) {
				const size_t aligned_size = (outputSize - i) & ~(elements_per_vector - 1);
				float* aligned_data = static_cast<float*>(__builtin_assume_aligned(mapRow + i, alignment));

				for (size_t j = 0; j < aligned_size; j+=VEC_SIZE) {
					for(size_t jj = 0; jj < VEC_SIZE; jj++) {
						size_t ij = j + jj;
						aligned_data[ij] = (very_fast_cos(std::abs(float(i++) * indexMul - 1.0f)) - 0.75) * factor;
					}
				}
			}

			for (; i < outputSize; ++i) {
                mapRow[i] = (very_fast_cos(std::abs(float(i) * indexMul - 1.0f)) - 0.75) * factor;
            }
        }



        float* offsetRow = offsetMap.ptr<float>(0);
        float inverseMul = inverse ? -1.0f : 1.0f;

        cv::parallel_for_(cv::Range(0, outputSize), [&mapXY, inverseMul, offsetRow, &outputSize, horizontal, inverse](const cv::Range& range) {
            for (int y = range.start; y < range.end; y++) {
                float* row = mapXY.ptr<float>(y);
                for(int x = 0; x < outputSize; ++x) {
                    float& dx = row[x * 2];
                    float& dy = row[x * 2 + 1];
                    dx = static_cast<float>(x);
                    dy = static_cast<float>(y);
                    if(horizontal) {
                        dx += offsetRow[y] * inverseMul;
                    } else {
                        dy += offsetRow[x] * inverseMul;
                    }
                }
            }
        });
		// auto createMapsDuration = std::chrono::duration<double, std::micro>(std::chrono::high_resolution_clock::now() - start).count();
		// std::cout << "create maps duration size " << outputSize << " time " << createMapsDuration << std::endl;
    }

	void correctBottleCv(const cv::Mat& img, cv::Mat& outImg, bool horizontal, bool inverse, bool small) {

		// img.copyTo(outImg);

		static std::pair<cv::Mat, cv::Mat> mapsXY[8];

		if(outImg.cols <= 0 || outImg.cols != outImg.rows) {
			throw std::invalid_argument("Output matrix must be a square");
		}
		float outputSize = outImg.rows;

		uint8_t mapMask = (inverse ? 1 : 0) | (horizontal ? 0b10 : 0) | (small ? 0b100 : 0);

		auto& mapXY = mapsXY[mapMask];

		if(mapXY.first.empty()) {
			cv::Mat floatMap;
			createMaps(floatMap, outputSize, horizontal, inverse);
			cv::convertMaps(floatMap, {}, mapXY.first, mapXY.second, CV_16SC2, true);
			// cv::convertMaps(floatMap, {}, mapXY.first, mapXY.second, CV_32FC2, true);
			// cv::convertMaps(mapXY, {}, mapXY, {}, CV_16SC2);
		}
		
		cv::remap(img, outImg, mapXY.first, mapXY.second, cv::InterpolationFlags::INTER_NEAREST, 0, 0);
    }


    void rotate(const BitMatrix& img, BitMatrix& outImg, const PointF& sincos) {
        int  rows, cols, r, c, r1, c1, k, s;

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

	void rotateNew(const BitMatrix& img, BitMatrix& outImg, const PointF& xBasisD) {

		auto toFloatP = [](double x, double y) -> PointT<float> {
			return PointT<float>(static_cast<float>(x), static_cast<float>(y));
		};

		PointT<float> xBasis(static_cast<float>(xBasisD.x), static_cast<float>(xBasisD.y));
		PointT<float> yBasis(-xBasis.y, xBasis.x);

		float mul = std::max<float>(std::fabs(xBasis.x + xBasis.y), std::fabs(yBasis.x + yBasis.y));

		PointT<float> inImgSize(static_cast<float>(img.width() - 1), static_cast<float>(img.height() - 1));
		PointT<float> outImgSize(static_cast<float>(outImg.width() - 1), static_cast<float>(outImg.height() - 1));
		PointT<float> outImgSizeInv = {1.0f / outImgSize.x, 1.0f / outImgSize.y};

		for(int y = 0; y < outImg.width(); y++) {
			PointT<float> oldY = (static_cast<float>(y) * outImgSizeInv.y - 0.5f) * yBasis;
			for(int x = 0; x < outImg.height(); x++) {
				PointT<float> oldImgF = (float(x) * outImgSizeInv.x - 0.5f) * xBasis + oldY;
				oldImgF = mul * oldImgF;
				oldImgF.x += 0.5f;
				oldImgF.y += 0.5f;

				PointI p = static_cast<PointI>(oldImgF * inImgSize);
				outImg.set(x, y, IsValidPoint(p, img.width(), img.height()) ? img.get(p) : false);
			}
		}
    }

    void rotateCV45(const BitMatrix& img, BitMatrix& outImg) {
        auto M = cv::getRotationMatrix2D({float(outImg.width()) * 0.5f, float(outImg.height()) * 0.5f}, 45, 0.70710678118);
        auto outputMat = outImg.asMat();
        cv::warpAffine(img.asMat(), outputMat, M, outputMat.size(), cv::INTER_NEAREST, cv::BORDER_CONSTANT, BitMatrix::UNSET_V);
    }

    void rotate45(BitMatrix& img) {
        int  rows, cols, r, c, r1, c1, k, s;
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

            if (!((x1 < 0) || (y1 < 0) || ((x1 + 1) >= img.width()) || ((y1 + 1) >= img.height()))) {
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

    void line3(cv::Mat& mat, int x1, int y1, int x2, int y2, int thickness = 2) {
		cv::line(mat, {x1,y1},{x2,y2}, BitMatrix::SET_V, thickness, cv::LINE_4);
    }
	



    std::array rotateEasy = {
        PointF(cos(M_PI / 4), sin(M_PI / 4))
    };

    std::array rotateMediun = {
        PointF(cos(M_PI / 4), sin(M_PI / 4)),
        PointF(cos(M_PI / 6), sin(M_PI / 6)),
        PointF(cos(M_PI / 3), sin(M_PI / 3))
    };

	BitMatrix CreateSnapped(const BitMatrix& src, int snap = 8) {
		int w = src.width();
		int h = src.height();
		w += w % snap != 0 ? (snap - (w % snap)) : 0;
		h += h % snap != 0 ? (snap - (h % snap)) : 0;
		return BitMatrix(w,h);
	}

	cv::Mat CreateSnappedMat(const BitMatrix& src, int snap = 8) {
		int w = src.width();
		int h = src.height();
		w += w % snap != 0 ? (snap - (w % snap)) : 0;
		h += h % snap != 0 ? (snap - (h % snap)) : 0;
		return cv::Mat(h, w, CV_8UC1);
	}

    // ---- DetectCRPT as a sub-ladder ---------------------------------------------
    //
    // Setup finds the white rectangle (retrying at 45 degrees) and measures the
    // transitions along its sides. That is a precondition, not a variant, so it
    // stays in DetectCRPT and bails the way it always did. What follows are the
    // variants -- the two L-marker drawings, then the bottle unwarp in four
    // orientations behind one preparation step -- each a step run by the same
    // runner as the entry-point plans. Per-variant attribution falls out of the
    // runner instead of out of hand-placed counters.

    struct CrptContext : ladder::State
    {
        const BitMatrix& image;
        Warp* warp;
        bool needToTraceWarp;
        bool correctCorners;
        DMGridRefineOptions gridRefine;

        // Setup's products. The transitions point INTO the four ResultPoints,
        // so those live here rather than as setup locals.
        BitMatrix newimage;
        ResultPoint pointA, pointB, pointC, pointD;
        std::array<ResultPointsAndTransitions, 4> transitions;

        // Scratch the variants draw into. One BitMatrix for both phases, as
        // before: the L-marker variants copy newimage into it, the bottle
        // preparation reallocates it square and keeps a cv::Mat view of it.
        BitMatrix img2;
        cv::Mat img2Mat;
        cv::Mat resizedImg;
        size_t remapSize = 0;
    };

    using CrptStep = ladder::Step<CrptContext>;

    // The rectangle's two sides with the fewest transitions (side 0 and side n2),
    // drawn onto a copy of the image so the tracer sees an L-marker, then
    // DetectNew on that.
    static DetectorResult CrptLMarker(CrptContext& c, int n2)
    {
        c.newimage.copyTo(c.img2);
        auto mat = c.img2.asMat();
        line3(mat, c.transitions[0].from->x(), c.transitions[0].from->y(), c.transitions[0].to->x(), c.transitions[0].to->y());
        line3(mat, c.transitions[n2].from->x(), c.transitions[n2].from->y(), c.transitions[n2].to->x(), c.transitions[n2].to->y());
        return DetectNew(c.img2, true, true, c.warp, c.needToTraceWarp, c.correctCorners, c.gridRefine);
    }
    static DetectorResult CrptLMarker0(CrptContext& c, bool&) { return CrptLMarker(c, 1); }
    static DetectorResult CrptLMarker1(CrptContext& c, bool&) { return CrptLMarker(c, 2); }

    // The bottle unwarp's shared input: the ORIGINAL image resized to a fixed
    // square (256, or 128 for small crops) and re-binarised. Detects nothing
    // itself; the four orientations that follow read what it prepared.
    static DetectorResult CrptBottlePrepare(CrptContext& c, bool&)
    {
        const size_t remapSizeBig = 256;
        const size_t remapSizeHalfThreshold = 160;
        c.remapSize = remapSizeBig;
        if(std::max(c.image.width(), c.image.height()) < remapSizeHalfThreshold) {
            c.remapSize /= 2;
        }

        c.img2 = BitMatrix(c.remapSize, c.remapSize);
        c.img2Mat = c.img2.asMat();

        cv::resize(c.image.asMat(), c.resizedImg, {static_cast<int>(c.remapSize), static_cast<int>(c.remapSize)}, 0,0, cv::INTER_LINEAR);
        cv::threshold(c.resizedImg, c.resizedImg, 127, 255, cv::THRESH_BINARY);
        return {};
    }

    // One bottle orientation: unwarp the prepared square into img2, then DetectNew.
    static DetectorResult CrptBottle(CrptContext& c, int i)
    {
        correctBottleCv(c.resizedImg, c.img2Mat, i & 0b10, i & 0b01, c.remapSize != 256);
        return DetectNew(c.img2, true, true, c.warp, c.needToTraceWarp, c.correctCorners, c.gridRefine);
    }
    static DetectorResult CrptBottle0(CrptContext& c, bool&) { return CrptBottle(c, 0); }
    static DetectorResult CrptBottle1(CrptContext& c, bool&) { return CrptBottle(c, 1); }
    static DetectorResult CrptBottle2(CrptContext& c, bool&) { return CrptBottle(c, 2); }
    static DetectorResult CrptBottle3(CrptContext& c, bool&) { return CrptBottle(c, 3); }

    static const CrptStep kCrptPlan[] = {
        {"lmarker[0]",    CrptLMarker0},
        {"lmarker[1]",    CrptLMarker1},
        {"bottlePrepare", CrptBottlePrepare},
        {"bottle[0]",     CrptBottle0},
        {"bottle[1]",     CrptBottle1},
        {"bottle[2]",     CrptBottle2},
        {"bottle[3]",     CrptBottle3},
    };

    static DetectorResult DetectCRPT(const BitMatrix& image, DecoderResult& outDecodeResult, ResultedDefect& possibleResultedDefect, Warp* warp = nullptr, bool needToTraceWarp = false, bool correctCorners = false,
									 DMGridRefineOptions gridRefine = {})
    {
        CrptContext c{{outDecodeResult}, image, warp, needToTraceWarp, correctCorners, gridRefine};

        c.newimage = image.copy();
        if (!DetectWhiteRect(c.newimage, c.pointA, c.pointB, c.pointC, c.pointD)) {
			rotateCV45(image, c.newimage);
			if(!DetectWhiteRect(c.newimage, c.pointA, c.pointB, c.pointC, c.pointD)) {
				return {};
			}
        }

        c.transitions = {
                TransitionsBetween(c.newimage, c.pointA, c.pointB),
                TransitionsBetween(c.newimage, c.pointA, c.pointC),
                TransitionsBetween(c.newimage, c.pointB, c.pointD),
                TransitionsBetween(c.newimage, c.pointC, c.pointD),
        };
        std::sort(c.transitions.begin(), c.transitions.end(),
                  [](const auto& a, const auto& b) { return a.transitions < b.transitions; });

        if (c.transitions[0].transitions > 4) {
            possibleResultedDefect = ResultedDefect::LMarker;
        }

        // Every caller arrives with outDecodeResult invalid -- the previous
        // phase's decode failed, or it would have returned -- so the runner's
        // "dec valid after a step" is exactly the per-variant
        // `if (outDecodeResult = DecodeResult(res); outDecodeResult.isValid())`
        // this replaces. LastDetection: what came out of the final variant,
        // valid or not, is what DetectCRPT has always handed back.
        return ladder::RunLadder(kCrptPlan, c, ladder::Policy::DecodeToWin, ladder::Fallback::LastDetection);
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


    BitMatrixCursorI cur(image, { left, top }, { 0, 1 });
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
        || !image.isIn(PointF{ left + modSizeX / 2 + (dimT - 1) * modSize, top + modSizeY / 2 + (dimR - 1) * modSize }))
        return {};

    int right = left + width - 1;
    int bottom = top + height - 1;

    // Now just read off the bits (this is a crop + subsample)
    return { Deflate(image, dimT, dimR, top + modSizeX / 2, left + modSizeY / 2, modSize),
            {{left, top}, {right, top}, {right, bottom}, {left, bottom}} };
}

// ---- Detector phases -----------------------------------------------------------
//
// One function per phase, shared by the plans below. A phase runs its detector
// and reports through `decodedInside` whether that detector already decoded
// into ctx.dec; it applies no ResultedDefect. Where a plan wants a defect on a
// phase's detection it wraps the phase (the SamplegridV1 steps do), because
// which defect, and whether one is applied at all, belongs to the plan, not to
// the detector.

// The edge-tracing detector, no warp. The runner decodes.
static DetectorResult PhaseNewPlain(ladder::Context& ctx, bool& /*decodedInside*/)
{
    return DetectNew(ctx.image, ctx.tryHarder, ctx.tryRotate, nullptr, false, false, ctx.gridRefine);
}

// The edge-tracing detector, tracing a warp into ctx.warp for the phase that
// follows. The runner decodes.
static DetectorResult PhaseNewWarp(ladder::Context& ctx, bool& /*decodedInside*/)
{
    return DetectNew(ctx.image, ctx.tryHarder, ctx.tryRotate, &ctx.warp, true, false, ctx.gridRefine);
}

// DetectCRPT on the plain image. Decodes internally; its defect goes into
// ctx.crptDefect for whichever plan wants it.
static DetectorResult PhaseCrpt(ladder::Context& ctx, bool& decodedInside)
{
    decodedInside = true;
    return DetectCRPT(ctx.image, ctx.dec, ctx.crptDefect, nullptr, false, false, ctx.gridRefine);
}

// DetectCRPT on a copy, with the warp the preceding PhaseNewWarp traced.
// Decodes internally.
static DetectorResult PhaseCrptWarp(ladder::Context& ctx, bool& decodedInside)
{
    decodedInside = true;
    return DetectCRPT(ctx.image.copy(), ctx.dec, ctx.crptDefect, &ctx.warp, true, false, ctx.gridRefine);
}

const int CommonMatrixDimensions[] = { 20, 22, 24, 26, 32, 36, 40, 44 };

// The caller-supplied quadrilateral tried as every common symbol size: corners
// corrected and rotated for that size, a warp computed, the grid sampled
// through it. Decodes internally, per size.
static DetectorResult PhaseDefinedGrid(ladder::Context& ctx, bool& decodedInside)
{
    decodedInside = true;
    const BitMatrix& image = ctx.image;
    DetectorResult detRes;
    for (int dim : CommonMatrixDimensions) {
        PointF P[] = { ctx.corners[0], ctx.corners[1], ctx.corners[2], ctx.corners[3] };
        CorrectCorners(image, P[0], P[1], P[2], P[3], dim);
        int rotateSteps = FindRotation(image, P[0], P[1], P[2], P[3], dim);
        if (rotateSteps > 0) {
            PointF PP[4];
            std::copy(P, &P[4], PP);
            for (int i = 4; i--;) {
                P[(i + rotateSteps) % 4] = PP[i];
            }
        }
        auto&& [TL, BL, BR, TR] = P;
        auto warp = ComputeWarp(image, TL, BL, BR, TR, dim, dim, dim);
        detRes = SampleGridWarped(image, TL, BL, BR, TR, dim, dim, warp);
        if (detRes.isValid()) {
            ctx.dec = DecodeResult(detRes);
            if (ctx.dec.isValid()) {
                return detRes;
            }
        }
    }
    return {};
}

// ---- Step rules ------------------------------------------------------------------

static bool WhenTryHarder(const ladder::Context& ctx)
{
    return ctx.tryHarder;
}

static bool WhenNothingDetectedYet(const ladder::Context& ctx)
{
    return !ctx.last.isValid();
}

// ---- SamplegridV1 steps: the phases above, plus the defect each one applied ------

// s1: the offset-tolerant legacy detector. Decodes internally. Plan-specific
// (no other plan runs it), so it is a step rather than a phase.
static DetectorResult StepOldWithOffsets(ladder::Context& ctx, bool& decodedInside)
{
    bool correctedOffset = false;
    DetectorResult det = DetectOldWithOffsets(ctx.image, ctx.dec, correctedOffset, ctx.gridRefine);
    det.setResultedDefect(correctedOffset ? ResultedDefect::MissingSync : ResultedDefect::Default);
    decodedInside = true;
    return det;
}

// s2: PhaseCrpt with its defect applied.
static DetectorResult StepCrpt(ladder::Context& ctx, bool& decodedInside)
{
    DetectorResult det = PhaseCrpt(ctx, decodedInside);
    det.setResultedDefect(ctx.crptDefect);
    return det;
}

// s3: PhaseNewPlain, marked Default.
static DetectorResult StepNewPlain(ladder::Context& ctx, bool& decodedInside)
{
    DetectorResult det = PhaseNewPlain(ctx, decodedInside);
    det.setResultedDefect(ResultedDefect::Default);
    return det;
}

// s4: PhaseNewWarp; PrintShift on a valid detection only, as before.
static DetectorResult StepNewWarp(ladder::Context& ctx, bool& decodedInside)
{
    DetectorResult det = PhaseNewWarp(ctx, decodedInside);
    if (det.isValid())
        det.setResultedDefect(ResultedDefect::PrintShift);
    return det;
}

// s5 is PhaseCrptWarp as is: unlike s2, the inline code never applied
// ctx.crptDefect to this detection, so neither does the plan.

// s6: s5's detection again, marked PrintShift, decoded by the runner. The bits
// are the ones s5 already decoded, so this has never been seen to win; it is
// kept so the plan IS the original sequence and removing it becomes a plan
// edit rather than a code change.
static DetectorResult StepCrptWarpRedecode(ladder::Context& ctx, bool& /*decodedInside*/)
{
    DetectorResult det = std::move(ctx.last);
    if (det.isValid())
        det.setResultedDefect(ResultedDefect::PrintShift);
    return det;
}

// ---- Plans -----------------------------------------------------------------------

using OuterStep = ladder::Step<ladder::Context>;

// The stock reader: DetectNew, then DetectCRPT only when tryHarder and only if
// DetectNew found nothing. Wins on the first valid DETECTION -- the reader
// decodes -- so the decoder result DetectCRPT produces on the way is thrown
// away, exactly as it always was.
static const OuterStep kStockPlan[] = {
    {"newPlain", PhaseNewPlain},
    {"crpt",     PhaseCrpt, WhenTryHarder},
};

// The CRPT ladder.
static const OuterStep kSamplegridV1Plan[] = {
    {"oldWithOffsets",   StepOldWithOffsets},
    {"crpt",             StepCrpt},
    {"newPlain",         StepNewPlain},
    {"newWarp",          StepNewWarp},
    {"crptWarp",         PhaseCrptWarp},
    {"crptWarpRedecode", StepCrptWarpRedecode},
};

// A caller-located symbol: the ladder's middle four phases (DetectCRPT only if
// DetectNew found nothing), then the supplied quadrilateral at every size.
// No defects: this entry point's reader never reads them.
static const OuterStep kDefinedPlan[] = {
    {"newPlain",    PhaseNewPlain},
    {"crpt",        PhaseCrpt, WhenNothingDetectedYet},
    {"newWarp",     PhaseNewWarp},
    {"crptWarp",    PhaseCrptWarp},
    {"definedGrid", PhaseDefinedGrid},
};

// ---- Entry points ----------------------------------------------------------------

DetectorResults Detect(const BitMatrix& image, bool tryHarder, bool tryRotate, bool isPure, DMGridRefineOptions gridRefine)
{
    if (isPure)
        return DetectPure(image);

    DecoderResult discarded;
    ladder::Context ctx{{discarded}, image, tryHarder, tryRotate, gridRefine};
    return ladder::RunLadder(kStockPlan, ctx, ladder::Policy::FirstValidDetection);
}

DetectorResults DetectSamplegridV1(const BitMatrix& image, bool tryHarder, bool tryRotate, bool /*isPure*/, DecoderResult& outDecoderResult,
								   DMGridRefineOptions gridRefine)
{
    ladder::Context ctx{{outDecoderResult}, image, tryHarder, tryRotate, gridRefine};
    return ladder::RunLadder(kSamplegridV1Plan, ctx);
}

DetectorResults DetectDefined(const BitMatrix& image, const PointF& P0, const PointF& P1, const PointF& P2, const PointF& P3, bool tryHarder, bool tryRotate, bool /*isPure*/, DecoderResult& outDecoderResult,
							  DMGridRefineOptions gridRefine)
{
    ladder::Context ctx{{outDecoderResult}, image, tryHarder, tryRotate, gridRefine};
    ctx.corners = { P0, P1, P2, P3 };
    return ladder::RunLadder(kDefinedPlan, ctx);
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
