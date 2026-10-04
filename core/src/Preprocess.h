/*
* Copyright 2026 CRPT
*/
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ImageView.h"

#include <cstdint>
#include <vector>

namespace ZXing {

// The image preprocessing the CRPT smart ladder runs on a crop before each
// decode attempt -- moved here verbatim from mediapipe's
// calculators/zxing/preprocessing_pipeline.h, op by op, so that preprocessing
// becomes a libZXing stage like the decoder, the samplers and the locators
// (README_DM_LADDER.md part 3). Same OpenCV calls, same constants, same order:
// a moved op must give the calculator's pixels exactly.
//
// The boundary carries pixels only: an ImageView in, a PreprocessImage out. No
// OpenCV type crosses it -- libZXing links its own static OpenCV, so a cv::Mat
// allocated on one side must never be released on the other.
//
// ABI: this header is vendored into mediapipe (calculators/zxing/ZXing/) and
// must stay byte-identical there, like DecodeHints.h and Result.h.

enum class PreprocessOp : uint8_t
{
	Original,
	AdaptiveThreshold,
	UnsharpMask,
	AdaptiveMean,
	BlurThreshold,
	BlurThreshold2,
	ScaledUnsharp,
	OtsuInverted,
	HistogramSharpening,
	Morphological,
	BlackEnhancement,
	BoostWhite,
	CLAHE,
	NormalizeEx,
	SauvolaBinarization,
	ClaheEnhancement,
	TopHatTransformation,
	BlackHatTransformation,
	AnisotropicDiffusion,
	SuperResolution,
	Erode,
	Rotate,
	CenterCrop,
};

// The arguments of the ops that take any; every other op ignores them. The
// defaults are the calculator's defaults.
struct PreprocessParams
{
	int window = 25;           // SauvolaBinarization
	double sauvolaK = 0.2;     //
	double sauvolaR = 128;     //
	double clipLimit = 2.0;    // ClaheEnhancement
	int gridSize = 8;          //
	int kernelSize = 15;       // TopHatTransformation, BlackHatTransformation
	int iterations = 10;       // AnisotropicDiffusion
	double lambda = 0.25;      //
	double diffusionK = 30;    //
	double scale = 2.0;        // SuperResolution
	bool whiteOnBlack = false; // Erode
	int erosionSize = 2;       //
	double angleDegrees = 0;   // Rotate
	double factor = 1.0;       // CenterCrop
};

// What an op produced: 8-bit pixels, `channels` interleaved, rows packed
// (row stride = width * channels). Empty when the op produced nothing.
struct PreprocessImage
{
	int width = 0, height = 0, channels = 0;
	std::vector<uint8_t> pixels;

	bool empty() const { return pixels.empty(); }
};

// Runs `op` on `in` into `out`. `in` is read as 8-bit with in.pixStride()
// channels (1, 3 or 4) in whatever order the caller's buffer holds them; the
// ops treat 3 channels as BGR and 4 as BGRA, as the calculator always did.
//
// Returns false, with `out` empty, when the op produced nothing or threw: no
// exception leaves libZXing (ADR 0001).
bool Preprocess(PreprocessOp op, const PreprocessParams& params, const ImageView& in, PreprocessImage& out) noexcept;

} // namespace ZXing
