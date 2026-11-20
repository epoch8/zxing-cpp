# ZXingCpp iOS Framework

To use the iOS (wrapper) framework in other apps, it is easiest
to build the library project and include the resulting xcframework
file in your app.

## Rebuild the XCFramework

Use Xcode, CMake, and the complete OpenCV XCFramework with iOS arm64 and
iOS Simulator arm64/x86_64 binaries. Place it at `vendor/opencv2.xcframework`
next to this README or pass its absolute path as shown below.

```bash
OPENCV_IOS_XCFRAMEWORK=/absolute/path/to/opencv2.xcframework \
  bash wrappers/ios/build-release.sh
```

The script builds the C++20 reader and Objective-C writer from this checkout.
It writes `wrappers/ios/ZXing.xcframework` after both platforms build. Pass an
output path as the first argument to write the framework elsewhere.
