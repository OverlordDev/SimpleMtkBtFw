#!/bin/bash
# Reproducible build of the MediaTek-whitelist BlueToolFixup.
# Requirements: Xcode, git, a MacKernelSDK checkout and the Lilu 1.7.x DEBUG kext (for its SDK headers).
#   MKS=/path/to/MacKernelSDK LILU=/path/to/Lilu.kext ./build.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MKS="${MKS:?set MKS to a MacKernelSDK directory}"
LILU="${LILU:?set LILU to a Lilu.kext (DEBUG build, contains Contents/Resources/Headers)}"
WORK="${WORK:-$HERE/build-work}"
UPSTREAM_COMMIT=65389020769837e92e9fb939bac34bd9a3e25bb2

rm -rf "$WORK" && mkdir -p "$WORK" && cd "$WORK"
git clone https://github.com/acidanthera/BrcmPatchRAM.git src
cd src
git checkout "$UPSTREAM_COMMIT"
git apply "$HERE/mediatek.patch"
ln -s "$MKS" MacKernelSDK
cp -R "$LILU" Lilu.kext
xcodebuild -project BrcmPatchRAM.xcodeproj -scheme BlueToolFixup -configuration Release \
    CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY="" build
echo "Built: $(pwd)/build/Products/Release/BlueToolFixup.kext"
