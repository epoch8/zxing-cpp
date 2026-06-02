// swift-tools-version:5.4
import Foundation
import PackageDescription

let packageDirectory = URL(fileURLWithPath: #file).deletingLastPathComponent().path
let frameworkSearchPaths = [
    "-F\(packageDirectory)/ZXing.xcframework/ios-arm64",
    "-F\(packageDirectory)/ZXing.xcframework/ios-arm64_x86_64-simulator",
    "-F\(packageDirectory)/vendor/opencv2.xcframework/ios-arm64_x86_64-simulator",
    "-F\(packageDirectory)/vendor/opencv2.xcframework/ios-arm64"
]

let package = Package(
    name: "ZXingCppWrapper",
    platforms: [
        .iOS(.v12)
    ],
    products: [
        .library(
            name: "ZXingCppWrapper",
            type: .static,
            targets: ["ZXingCppWrapper"])
    ],
    targets: [
        .binaryTarget(
            name: "ZXingCpp",
            path: "ZXing.xcframework"
        ),
        .binaryTarget(
            name: "opencv2",
            path: "vendor/opencv2.xcframework"
        ),
        .target(
            name: "ZXingCppWrapper",
            dependencies: ["ZXingCpp", "opencv2"],
            path: "Sources/Wrapper",
            publicHeadersPath: ".",
            cSettings: [
                .unsafeFlags(frameworkSearchPaths)
            ],
            cxxSettings: [
                .unsafeFlags(frameworkSearchPaths)
            ]
        )
    ],
    cxxLanguageStandard: .cxx17
)
