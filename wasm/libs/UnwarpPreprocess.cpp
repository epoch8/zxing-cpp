#include "UnwarpPreprocess.h"
#include <vector>
#include <cmath>
#include <algorithm>

using namespace cv;
using namespace std;

Point2f mean(const vector<Point2f>& points) {
    Point2f centroid(points[0]);
    for (int i = 1; i < points.size(); i++) {
        centroid += points[i];
    }
    centroid /= static_cast<float>(points.size());
    return centroid;
}

vector<Point2f> orderPoints(vector<Point2f> pts) {
    vector<Point2f> rect(4);
    vector<float> sum(pts.size()), diff(pts.size());

    for (size_t i = 0; i < pts.size(); ++i) {
        sum[i] = pts[i].x + pts[i].y;
        diff[i] = pts[i].x - pts[i].y;
    }

    rect[0] = pts[min_element(sum.begin(), sum.end()) - sum.begin()]; // Top-left
    rect[2] = pts[max_element(sum.begin(), sum.end()) - sum.begin()]; // Bottom-right
    rect[1] = pts[min_element(diff.begin(), diff.end()) - diff.begin()]; // Top-right
    rect[3] = pts[max_element(diff.begin(), diff.end()) - diff.begin()]; // Bottom-left

    Point2f centroid = (rect[0] + rect[1] + rect[2] + rect[3]) / 4.0f;
    vector<float> angles(4);
    for (int i = 0; i < 4; ++i) {
        angles[i] = atan2(rect[i].y - centroid.y, rect[i].x - centroid.x);
    }

    vector<size_t> indices = { 0, 1, 2, 3 };
    sort(indices.begin(), indices.end(), [&angles](size_t i1, size_t i2) { return angles[i1] > angles[i2]; });

    vector<Point2f> sortedRect(4);
    for (int i = 0; i < 4; ++i) {
        sortedRect[i] = rect[indices[i]];
    }

    return sortedRect;
}

pair<vector<Point>, vector<Point2f>> findMainContour(Mat thresh, float epsilon) {
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    findContours(thresh, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);
    // Check if any contours were found
    if (contours.empty()) {
		return {{}, {}};
    }

    for (const auto& cnt : contours) {
        if (contourArea(cnt) < 100) continue;
        vector<Point> approx;
        approxPolyDP(cnt, approx, epsilon * arcLength(cnt, true), true);
        if (approx.size() == 4) {
            return { cnt, vector<Point2f>(approx.begin(), approx.end()) };
        }
    }

    auto cnt = *max_element(contours.begin(), contours.end(), [](const vector<Point>& a, const vector<Point>& b) {
        return contourArea(a) < contourArea(b);
    });

    RotatedRect rect = minAreaRect(cnt);
    vector<Point2f> box(4);
    rect.points(box.data());
    return { cnt, box };
}

float distanceToSegment(Point2f pt, Point2f start, Point2f end) {
    Point2f ab = end - start;
    Point2f ap = pt - start;

    float segmentLengthSq = ab.dot(ab);
    if (segmentLengthSq == 0) return norm(ap);

    float t = ap.dot(ab) / segmentLengthSq;
    t = max(0.0f, min(1.0f, t));

    Point2f closestPoint = start + t * ab;
    return norm(pt - closestPoint);
}

void filterContourPoints(vector<Point>& contour, const vector<Point2f>& corners, float threshold) {

    int j = 0;

    for (const auto& pt : contour) {
        for (int i = 0; i < 4; ++i) {
            Point2f start = corners[i];
            Point2f end = corners[(i + 1) % 4];
            float dist = distanceToSegment(pt, start, end);
            if (dist < threshold) {
                contour[j++] = pt;
                break;
            }
        }
    }
    contour.resize(j);

    // return filtered;
}

vector<vector<Point2f>> splitContourIntoSides(const vector<Point>& contour, const vector<Point2f>& corners) {
    vector<Point2f> contourPts(contour.begin(), contour.end());
    vector<int> indices(4);
    for (int i = 0; i < 4; ++i) {
        double minDist = numeric_limits<double>::max();
        for (size_t j = 0; j < contourPts.size(); ++j) {
            double dist = norm(contourPts[j] - corners[i]);
            if (dist < minDist) {
                minDist = dist;
                indices[i] = j;
            }
        }
    }

    vector<vector<Point2f>> sides(4);
    for (int i = 0; i < 4; ++i) {
        int startIdx = indices[i];
        int endIdx = indices[(i + 1) % 4];
        if (startIdx < endIdx) {
            sides[i] = vector<Point2f>(contourPts.begin() + startIdx, contourPts.begin() + endIdx + 1);
        }
        else {
            sides[i] = vector<Point2f>(contourPts.begin() + startIdx, contourPts.end());
            sides[i].insert(sides[i].end(), contourPts.begin(), contourPts.begin() + endIdx + 1);
        }
        vector<Point2f> approxSide;
        approxPolyDP(sides[i], approxSide, 0.0001 * arcLength(sides[i], false), false);
        sides[i] = approxSide;
    }

    return sides;
}

vector<Point2f> adjustCornersToContour(const vector<Point>& contour, const vector<Point2f>& corners, float alpha = 0.01f) {
    auto fitLine = [](const vector<Point2f>& points) -> Vec3f {
        if (points.size() < 2) return { 0, 0, 0 };
        Point2f centroid = mean(points);
        Mat A(points.size(), 2, CV_32F);
        for (size_t i = 0; i < points.size(); ++i) {
            A.at<float>(i, 0) = points[i].x - centroid.x;
            A.at<float>(i, 1) = points[i].y - centroid.y;
        }
        Mat w, u, vt;
        SVDecomp(A, w, u, vt);
        Vec3f line(vt.at<float>(1, 0), vt.at<float>(1, 1), -(vt.at<float>(1, 0) * centroid.x + vt.at<float>(1, 1) * centroid.y));
        float norm = sqrt(line[0] * line[0] + line[1] * line[1]);
        if (norm == 0) return { 0, 0, 0 };
        line /= norm;
        return line;
    };

    auto lineIntersection = [](const Vec3f& line1, const Vec3f& line2) -> Point2f {
        Matx22f matrix(line1[0], line1[1], line2[0], line2[1]);
        Vec2f rhs(-line1[2], -line2[2]);
        Matx21f intersection;
        if (solve(matrix, rhs, intersection)) {
            return Point2f(intersection(0), intersection(1));
        }
        return Point2f();
    };

    vector<vector<Point2f>> sides = splitContourIntoSides(contour, corners);
    vector<Vec3f> lines(4);
    for (int i = 0; i < 4; ++i) {
        lines[i] = fitLine(sides[i]);
    }

    vector<Point2f> newCorners(4);
    for (int i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        Point2f intersection = lineIntersection(lines[i], lines[j]);
        if (intersection != Point2f()) {
            newCorners[i] = intersection;
        }
        else {
            newCorners[i] = corners[i];
        }
    }

    vector<Point2f> contourPts(contour.begin(), contour.end());
    vector<Point2f> adjusted(4);
    for (int i = 0; i < 4; ++i) {
        double minDist = numeric_limits<double>::max();
        Point2f closestPt;
        for (const auto& pt : contourPts) {
            double dist = norm(pt - newCorners[i]);
            if (dist < minDist) {
                minDist = dist;
                closestPt = pt;
            }
        }
        adjusted[i] = alpha * newCorners[i] + (1 - alpha) * closestPt;
    }

    return adjusted;
}

Mat adaptiveBinarization(const Mat& image) {
    Mat gray, blurred;
    cvtColor(image, gray, COLOR_BGR2GRAY);
    bilateralFilter(gray, blurred, 6, 75, 75);

    Mat gradX, gradY;
    Sobel(blurred, gradX, CV_32F, 1, 0, 3);
    Sobel(blurred, gradY, CV_32F, 0, 1, 3);

    Mat gradMag;
    magnitude(gradX, gradY, gradMag);

    Mat thresh;
    adaptiveThreshold(blurred, thresh, 70, ADAPTIVE_THRESH_GAUSSIAN_C, THRESH_BINARY_INV, 21, 12);

    return thresh;
}

cv::Point2d normalizeVector(const cv::Point2d& v) {
    double norm = cv::norm(v);
    return norm > 0 ? v / norm : v;
}

std::vector<int> findNewCornersIndices(const std::vector<cv::Point2f>& contour, const std::vector<cv::Point2f>& corners) {
    cv::Point2f basis0 = corners[2] - corners[0];
    cv::Point2f basis1 = corners[3] - corners[1];
    basis0 = normalizeVector(basis0);
    basis1 = normalizeVector(basis1);

    int minIndex02 = 0;
    int minIndex13 = 0;
    int maxIndex02 = 0;
    int maxIndex13 = 0;

    double min02dot = contour[0].dot(basis0);
    double min13dot = contour[0].dot(basis1);
    double max02dot = min02dot;
    double max13dot = min13dot;

    for (size_t i = 1; i < contour.size(); i++) {
        double dot02 = contour[i].dot(basis0);
        double dot13 = contour[i].dot(basis1);
        if (dot02 < min02dot) {
            min02dot = dot02; minIndex02 = i;
        }
        if (dot02 > max02dot) {
            max02dot = dot02; maxIndex02 = i;
        }
        if (dot13 < min13dot) {
            min13dot = dot13; minIndex13 = i;
        }
        if (dot13 > max13dot) {
            max13dot = dot13; maxIndex13 = i;
        }
    }

    std::vector<int> indices = { minIndex02, minIndex13, maxIndex02, maxIndex13 };
    std::sort(indices.begin(), indices.end());
    return std::move(indices);
}

void fitPolynomial(const std::vector<float>& x, const std::vector<float>& y,
                   std::vector<float>& coeffs, int degree) {

    int actual_degree = std::min(degree, static_cast<int>(x.size()) - 1);

    if(actual_degree < 3) {
        int a = 2;
    }

    cv::Mat X(x.size(), actual_degree + 1, CV_32F);
    cv::Mat Y(y.size(), 1, CV_32F);

    for (size_t i = 0; i < x.size(); ++i) {
        for (int j = 0; j <= actual_degree; ++j) {
            X.at<float>(i, j) = std::pow(x[i], j);
        }
        Y.at<float>(i, 0) = y[i];
    }

    cv::Mat C;
    cv::solve(X, Y, C, cv::DECOMP_QR);

    coeffs.resize(degree + 1);
    for (int i = 0; i <= degree; ++i) {
        coeffs[i] = i <= actual_degree ? C.at<float>(i, 0) : 0.0;
    }
}

float evaluatePolynomial(const std::vector<float>& coeffs, float x) {
    float result = 0.0;
    for (size_t i = 0; i < coeffs.size(); ++i) {
        result += coeffs[i] * std::pow(x, i);
    }
    return result;
}

float evaluatePolynomial3(const std::vector<float>& coeffs, float x) {
    return coeffs[0] + coeffs[1] * x + coeffs[2] * x * x + coeffs[3] * x * x * x;
}

void smoothAndResampleWithPolyfit(const std::vector<float>& originalX, const std::vector<float>& originalY, std::vector<float>& smoothedY, int targetPoints) {
    // Adjust degree if there aren't enough points

    std::vector<float> coeffs;
    fitPolynomial(originalX, originalY, coeffs, 3);

    float xMin = *std::min_element(originalX.begin(), originalX.end());
    float xMax = *std::max_element(originalX.begin(), originalX.end());

    smoothedY.resize(targetPoints);

    // smoothedY[0] = 0;
    // smoothedY.back() = evaluatePolynomial3(coeffs, xMax);

    float first = evaluatePolynomial3(coeffs, xMin);
    float last = evaluatePolynomial3(coeffs, xMax);
    // float mul = (last - first) / float(targetPoints - 1);

    for (int i = 0; i < targetPoints; ++i) {
        float alpha = i / float(targetPoints - 1);
        float newX = xMin + (xMax - xMin) * alpha;
        smoothedY[i] = evaluatePolynomial3(coeffs, newX) - (first + (last - first) * alpha);
    }
}

std::vector<float> resampleContourSide(const cv::Point2f& startPoint, const cv::Point2f& endPoint, const std::vector<cv::Point2f>& points, int targetPoints) {
    cv::Point2f unitVector = endPoint - startPoint;
    double norm = cv::norm(unitVector);
    if (norm > 0) unitVector /= norm;

    cv::Point2f perpVector(-unitVector.y, unitVector.x);

    std::vector<float> originalX;
    originalX.reserve(points.size());
    std::vector<float> originalY;
    originalY.reserve(points.size());

    for (size_t i = 0; i < points.size(); ++i) {
        cv::Point2f movedPoint = points[i] - startPoint;
        originalX.push_back(movedPoint.dot(unitVector));
        originalY.push_back(movedPoint.dot(perpVector));
    }

    std::vector<float> smoothedY;
    smoothAndResampleWithPolyfit(originalX, originalY, smoothedY, targetPoints);

    return smoothedY;
}

std::vector<std::vector<float>> resampleContour(std::vector<int>& cornerIndices, const std::vector<cv::Point2f>& contour, int targetPoints) {

    std::vector<cv::Point2f> newCorners;
    for (int i : cornerIndices) {
        newCorners.push_back(contour[i]);
    }

    std::vector<std::vector<float>> contourSides;
    for (size_t i = 0; i < cornerIndices.size() - 1; ++i) {
        std::vector<cv::Point2f> sidePoints(contour.begin() + cornerIndices[i], contour.begin() + cornerIndices[i + 1] + 1);
        contourSides.push_back(resampleContourSide(contour[cornerIndices[i]], contour[cornerIndices[i + 1]], sidePoints, targetPoints));
    }

    // Handle the last side that wraps around
    std::vector<cv::Point2f> lastSidePoints;
    lastSidePoints.insert(lastSidePoints.end(), contour.begin() + cornerIndices.back(), contour.end());
    lastSidePoints.insert(lastSidePoints.end(), contour.begin(), contour.begin() + cornerIndices[0] + 1);
    contourSides.push_back(resampleContourSide(contour[cornerIndices.back()], contour[cornerIndices[0]], lastSidePoints, targetPoints));

    return contourSides;
}

void createRemapGridNew(Mat& outRemapX, Mat& outRemapY, const vector<Point2f>& corners, const vector<vector<float>>& sideOffsets, int inputSize, int inputOffset, int outputSize = 160, int offset = 25) {

    //     # 0---3---3
    //     # |       |
    //     # 0       2
    //     # |       |
    //     # 1---1---2

    int validSideSize = outputSize - offset * 2;

    for (const auto& of : sideOffsets) {
        if (of.size() != validSideSize) {
            invalid_argument("sideOffsets size must be outputSize - offset * 2");
        }
    }

    const auto& sideL = sideOffsets[0];
    const auto& sideB = sideOffsets[1];
    const auto& sideR = sideOffsets[2];
    const auto& sideT = sideOffsets[3];

    float resizeMul = float(inputSize - inputOffset * 2) / float(validSideSize);

    for (int y = 0; y < outputSize; y++) {
        for (int x = 0; x < outputSize; x++) {
            outRemapX.at<float>({ x,y }) = (float(x) - float(offset)) * resizeMul + float(inputOffset);
            outRemapY.at<float>({ x,y }) = (float(y) - float(offset)) * resizeMul + float(inputOffset);
        }
    }

    for (int j = 0, jinv = validSideSize - 1; j < validSideSize; j++, jinv--) {
        for (int i = 0; i < outputSize; i++) {
            float alpha = float(i - offset) / float(validSideSize - 1);
            outRemapX.at<float>({ i, j + offset }) += -sideL[j] * (1.0 - alpha) + sideR[jinv] * alpha;
            outRemapY.at<float>({ j + offset, i }) += -sideT[jinv] * (1.0 - alpha) + sideB[j] * alpha;
        }
    }

}

void modifyGridDataOffsets(Mat& outRemapX, Mat& outRemapY, const Mat& xOfs, const Mat& yOfs, int outputSize = 160, int offset = 25) {
    if (xOfs.rows != (outputSize - offset * 2) || yOfs.rows != (outputSize - offset * 2)) {
        invalid_argument("xOfs size must be outputSize - offset * 2");
    }

    //     # 0---3---3
    //     # |       |
    //     # 0       2
    //     # |       |
    //     # 1---1---2

    vector<Point2f> validPoints;
    vector<Point2f> validValues;
    float scale = float(xOfs.rows);

    for (int j = offset; j < outputSize - offset; j++) {
        for (int i = 0; i < outputSize; i++) {
            outRemapX.at<float>({ j,i }) += xOfs.at<float>(0, j - offset) * scale;
            outRemapY.at<float>({ i,j }) += yOfs.at<float>(0, j - offset) * scale;
        }
    }
}


bool cvUnwarpPreprocessPredefined(cv::Mat& outResult, const cv::Mat& imageIn, const std::vector<std::pair<cv::Mat, cv::Mat>>& warps, std::function<bool(const cv::Mat&)> processResult, const UnwarpParams& params) {
    int maxSize = 320;

    Mat thresh;
    Mat image = imageIn;
    float scale = 1;
    if (std::max(image.cols, image.rows) > maxSize) {
        scale = static_cast<float>(maxSize) / static_cast<float>(std::max(image.cols, image.rows));
        cv::resize(image, image, { static_cast<int>(static_cast<float>(image.rows) * scale), static_cast<int>(static_cast<float>(image.cols) * scale) }, 0, 0, cv::INTER_AREA);
    }
    thresh = adaptiveBinarization(image);


    // Морфологические операции
    int imageMinDim = std::min(image.rows, image.cols);
    int kernelSize = 15;
    if(imageMinDim < 200) {
        kernelSize = std::max(kernelSize * imageMinDim / 200, 5);
        if(!(kernelSize & 1)) {
            kernelSize++;
        }
    }

    Mat kernel = getStructuringElement(MORPH_ELLIPSE, Size(kernelSize, kernelSize));
    Mat morph;
    morphologyEx(thresh, morph, MORPH_CLOSE, kernel);
    // Поиск контура и углов

	auto [contourInt, oldCorners] = findMainContour(morph, params.approxPolyEpsilon);

	if(contourInt.empty()) return false;

    std::vector<cv::Point2f> contour;
    contour.reserve(contourInt.size());
    std::transform(
            contourInt.begin(),
            contourInt.end(),
            std::back_inserter(contour),
            [](const cv::Point& p) { return cv::Point2f(p); }
    );

    auto newCornerInds = findNewCornersIndices(contour, oldCorners);
    std::vector<cv::Point2f> corners;
    corners.reserve(4);
    for (auto& i : newCornerInds) {
        corners.push_back(contour[i]);
    }

    int sideLength = std::min(image.cols, image.rows);
    int sidePad = sideLength / 10;
    std::vector<cv::Point2f> dst = {
            {float(sidePad), float(sidePad)},
            {float(sidePad), float(sideLength - sidePad)},
            {float(sideLength - sidePad), float(sideLength - sidePad)},
            {float(sideLength - sidePad), float(sidePad)}
    };

    auto M = cv::getPerspectiveTransform(corners, dst);

    Mat perspectiveCorrected;
    cv::warpPerspective(image, perspectiveCorrected, M, { sideLength, sideLength }, INTER_LINEAR, BORDER_REPLICATE);

    cv::perspectiveTransform(contour, contour, M);
    for (size_t i = 0; i < 4; i++) {
        corners[i] = contour[newCornerInds[i]];
    }

    auto sideOffsets = resampleContour(newCornerInds, contour, params.outputSize - params.offset * 2);

    Mat remapX(params.outputSize, params.outputSize, CV_32F, Scalar(0));
    Mat remapY(params.outputSize, params.outputSize, CV_32F, Scalar(0));

    createRemapGridNew(remapX, remapY, dst, sideOffsets, sideLength, sidePad, params.outputSize, params.offset);


    //Try without offset
    remap(perspectiveCorrected, outResult, remapX, remapY, INTER_LINEAR, BORDER_REPLICATE, Scalar(255, 255, 255));
    bool success = processResult(outResult);
    if (success) {
        return true;
    }

    cv::Mat remapXWarped;
    cv::Mat remapYWarped;
    cv::Mat xOfsResized;
    cv::Mat yOfsResized;

    for (const auto& [xOfs, yOfs] : warps) {
        remapX.copyTo(remapXWarped);
        remapY.copyTo(remapYWarped);

        if(xOfs.rows != params.outputSize - params.offset * 2 || yOfs.rows != params.outputSize - params.offset * 2) {
            resizeWarp(xOfs, xOfsResized, params);
            resizeWarp(yOfs, yOfsResized, params);
            modifyGridDataOffsets(remapXWarped, remapYWarped, xOfsResized, yOfsResized, params.outputSize, params.offset);
        } else {
            modifyGridDataOffsets(remapXWarped, remapYWarped, xOfs, yOfs, params.outputSize, params.offset);
        }

        remap(perspectiveCorrected, outResult, remapXWarped, remapYWarped, INTER_LINEAR, BORDER_REPLICATE, Scalar(255, 255, 255));

        bool success = processResult(outResult);

        if (success) {
            return true;
        }
    }

    return false;

}

void resizeWarp(const cv::Mat& warpIn, cv::Mat& warpOut, const UnwarpParams& params)
{
    cv::resize(warpIn, warpOut, { 1,params.outputSize - params.offset * 2 });
}