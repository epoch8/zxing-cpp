# ZXingCpp iOS Framework

To use the iOS (wrapper) framework in other apps, it is easiest
to build the library project and include the resulting xcframework
file in your app.

## How to build and use

To build the xcframework:
	$ chmod +x download_libs.sh
	$ ./download_libs.sh
	$ ./build-release.sh
    

# IMPORTANT!
Check that opencv binaries succesfully downloaded and placed into 
wrappers/ios/vendor/opencv2.xcframework/ios-arm64/opencv2.framework/Versions/A/opencv2
and
wrappers/ios/vendor/opencv2.xcframework/ios-arm64_x86_64-simulator/opencv2.framework/Versions/A/opencv2

Links for download is in download_libs.sh