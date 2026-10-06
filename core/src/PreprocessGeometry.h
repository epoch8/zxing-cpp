/*
* Copyright 2026 CRPT
*/
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Preprocess.h"

// Internal to libZXing (not a public header, not vendored): what an op does to
// positions, so a result found on a preprocessed image can be placed on the
// image the op was given (ADR 0006 step 8, the coordinate map).

namespace ZXing {

/// An affine map of positions, x' = m[0] x + m[1] y + m[2], y' = m[3] x + m[4] y + m[5].
struct Affine
{
	double m[6] = {1, 0, 0, 0, 1, 0};

	bool identity() const noexcept { return m[0] == 1 && m[1] == 0 && m[2] == 0 && m[3] == 0 && m[4] == 1 && m[5] == 0; }

	/// this after `first`: a point goes through `first`, then through this.
	Affine after(const Affine& first) const noexcept
	{
		const double* a = m;
		const double* b = first.m;
		return {{a[0] * b[0] + a[1] * b[3], a[0] * b[1] + a[1] * b[4], a[0] * b[2] + a[1] * b[5] + a[2],
				 a[3] * b[0] + a[4] * b[3], a[3] * b[1] + a[4] * b[4], a[3] * b[2] + a[4] * b[5] + a[5]}};
	}

	Affine inverse() const noexcept
	{
		const double det = m[0] * m[4] - m[1] * m[3];
		if (det == 0)
			return {};
		const double i0 = m[4] / det, i1 = -m[1] / det, i3 = -m[3] / det, i4 = m[0] / det;
		return {{i0, i1, -(i0 * m[2] + i1 * m[5]), i3, i4, -(i3 * m[2] + i4 * m[5])}};
	}
};

/// The map from the op's input to its output, for an input of inW x inH that
/// came out outW x outH. Identity for the ops that keep geometry; a resize for
/// ScaledUnsharp and SuperResolution (cv::resize's pixel-centre convention, the
/// real size ratio); a shift for CenterCrop; cv::getRotationMatrix2D for Rotate.
Affine PreprocessGeometry(PreprocessOp op, const PreprocessParams& params, int inW, int inH, int outW, int outH) noexcept;

} // namespace ZXing
