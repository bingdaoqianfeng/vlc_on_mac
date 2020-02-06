# build vlc_on_mac
cd vlc
mkdir build
cd build
../extras/package/macosx/build.sh -c -k"/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/"
#brew install libxml2
#export PATH="/usr/local//opt/libxml2/bin/:$PATH"
#export CPPFLAGS="-I/usr/local/opt/libxml2/include/libxml2/"


# how to build ios vlc
# https://wiki.videolan.org/IOSCompile/  
# vlc-ios-3.2.4.tar.gz
# how to run ios vlc on iphont 
# https://code.videolan.org/videolan/vlc-ios/-/wikis/Beginner-Guide

# So you are trying to build VLCKit either because you found a bug or is eager to see the inner works of VLC.    
# how to build VLCKit
# python must be >= 3.7
cd VLCKit
./buildMobileVLCKit.sh -dva aarch64

# how to replace MobileVLCKit.framework to your build MobileVLCKit.framework.
cd Pods/MobileVLCKit
rm MobileVLCKit.framework
ln -s /yourrootpath/VLCKit/build/MobileVLCKit.framework/

# about opengl document
# Vertex Shader Program
https://www.jianshu.com/p/60c15b51c601
# GPU 编程与CG 语言之阳春白雪下里巴人.pdf
https://vdisk.weibo.com/s/amPF49uDZe8sj/1455848634
