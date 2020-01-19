# build vlc_on_mac
cd vlc
mkdir build
cd build
../extras/package/macosx/build.sh -c -k"/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/"

# build VLCKit
# python must be >= 3.7
cd VLCKit
./buildMobileVLCKit.sh -dva aarch64
