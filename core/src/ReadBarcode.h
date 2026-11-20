/*
* Copyright 2019 Axel Waggershauser
*/
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DecodeHints.h"
#include "ImageView.h"
#include "Result.h"

#include <string>
#include <string_view>

namespace ZXing {

/**
 * Read barcode from an ImageView
 *
 * @param buffer  view of the image data including layout and format
 * @param hints  optional DecodeHints to parameterize / speed up decoding
 * @return #Result structure
 */
Result ReadBarcode(const ImageView& buffer, const DecodeHints& hints = {});

/**
 * Read barcodes from an ImageView
 *
 * @param buffer  view of the image data including layout and format
 * @param hints  optional DecodeHints to parameterize / speed up decoding
 * @return #Results list of results found, may be empty
 */
Results ReadBarcodes(const ImageView& buffer, const DecodeHints& hints = {});
Results readbarcodescrpt_detector_v1_samplegridv1(const ImageView& buffer, const PointF& P0, const PointF& P1, const PointF& P2, const PointF& P3, const DecodeHints& hints = {});
Results readbarcodescrpt_samplegridv1(const ImageView& buffer, const DecodeHints& hints = {}, bool returnEdges = false);

/**
 * Check a CRPT ladder spec (what DecodeHints::setDMLadder takes) against the
 * plan a strategy runs: "samplegridv1" for readbarcodescrpt_samplegridv1,
 * "stock" for ReadBarcodes. Returns an empty string when the spec is valid,
 * otherwise the reason. For callers that build hints from configuration: a
 * bad spec should fail when the configuration is read, not surface later as
 * a decode that quietly returns nothing.
 */
std::string ValidateDMLadderSpec(std::string_view spec, std::string_view plan);

} // ZXing

