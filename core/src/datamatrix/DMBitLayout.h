/*
* Copyright 2020 Axel Waggershauser
*/
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Point.h"

#include <array>
#include <vector>

namespace ZXing {

class BitMatrix;
class ByteArray;

namespace DataMatrix {

class Version;

BitMatrix BitMatrixFromCodewords(const ByteArray& codewords, int width, int height);
ByteArray CodewordsFromBitMatrix(const BitMatrix& bits, const Version& version);

/**
 * For each codeword in VisitMatrix order, the 8 module positions in data-area coordinates (col, row).
 */
std::vector<std::array<PointI, 8>> CodewordBitPositions(int dataWidth, int dataHeight);

/** Map a data-area module (col,row) to full-symbol module coordinates including borders/alignment. */
PointI DataModuleToSymbol(const Version& version, int dataCol, int dataRow);

} // namespace DataMatrix
} // namespace ZXing
