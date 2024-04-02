#!/bin/sh
set -e
if test "$CONFIGURATION" = "Debug"; then :
  cd /Users/akor/Work/E8/git_repos/zxing-cpp/wrappers/ios/_builds
  make -f /Users/akor/Work/E8/git_repos/zxing-cpp/wrappers/ios/_builds/CMakeScripts/ReRunCMake.make
fi
if test "$CONFIGURATION" = "Release"; then :
  cd /Users/akor/Work/E8/git_repos/zxing-cpp/wrappers/ios/_builds
  make -f /Users/akor/Work/E8/git_repos/zxing-cpp/wrappers/ios/_builds/CMakeScripts/ReRunCMake.make
fi
if test "$CONFIGURATION" = "MinSizeRel"; then :
  cd /Users/akor/Work/E8/git_repos/zxing-cpp/wrappers/ios/_builds
  make -f /Users/akor/Work/E8/git_repos/zxing-cpp/wrappers/ios/_builds/CMakeScripts/ReRunCMake.make
fi
if test "$CONFIGURATION" = "RelWithDebInfo"; then :
  cd /Users/akor/Work/E8/git_repos/zxing-cpp/wrappers/ios/_builds
  make -f /Users/akor/Work/E8/git_repos/zxing-cpp/wrappers/ios/_builds/CMakeScripts/ReRunCMake.make
fi

