set -euo pipefail

# 1.记录原始url
ORIGIN_DISTRIBUTION_URL=$(grep "distributionUrl" gradle/wrapper/gradle-wrapper.properties | cut -d "=" -f 2)
echo "origin gradle url: $ORIGIN_DISTRIBUTION_URL"
# 2.切换gradle版本
NEW_DISTRIBUTION_URL="https\:\/\/services.gradle.org\/distributions\/gradle-8.0-bin.zip"
sed -i.bak "s/distributionUrl=.*$/distributionUrl=$NEW_DISTRIBUTION_URL/" gradle/wrapper/gradle-wrapper.properties
echo "new gradle url: " $(grep "distributionUrl" gradle/wrapper/gradle-wrapper.properties | cut -d "=" -f 2)

# 3.开始发布
KUIKLY_AGP_VERSION="7.4.2" KUIKLY_KOTLIN_VERSION="2.0.21-KBA-010" ./gradlew -c settings.2.0.ohos.gradle.kts :demo:linkSharedDebugSharedOhosArm64  --stacktrace


# 4.还原文件
mv gradle/wrapper/gradle-wrapper.properties.bak gradle/wrapper/gradle-wrapper.properties

# 5.拷贝so
echo "Copying artifact files:"
OHOS_RENDER_PROJECT_DIR=./ohosApp

TARGET_SO_PATH=$PWD/demo/build/bin/ohosArm64/sharedDebugShared/libshared.so
OHO_SO_PROJECT_PATH=$OHOS_RENDER_PROJECT_DIR/entry/libs/arm64-v8a
mkdir -p "$OHO_SO_PROJECT_PATH"
# 先删除旧的 libshared.so，避免它已被其他进程（如 hvigor/hdc）mmap 时，
# 原地覆写导致文件头被破坏（file 识别为 data、SHA 不匹配）。
rm -f "$OHO_SO_PROJECT_PATH/libshared.so"
cp -f "$TARGET_SO_PATH" "$OHO_SO_PROJECT_PATH/libshared.so"
# 校验拷贝结果：必须是 ELF 且大小与源一致，否则立即失败。
DST_SO="$OHO_SO_PROJECT_PATH/libshared.so"
if ! file "$DST_SO" | grep -q "ELF"; then
    echo "ERROR: $DST_SO is not a valid ELF file after copy"
    file "$DST_SO"
    exit 1
fi
SRC_SIZE=$(wc -c < "$TARGET_SO_PATH" | tr -d ' ')
DST_SIZE=$(wc -c < "$DST_SO" | tr -d ' ')
if [ "$SRC_SIZE" != "$DST_SIZE" ]; then
    echo "ERROR: size mismatch after copy: src=$SRC_SIZE dst=$DST_SIZE"
    exit 1
fi
echo "libshared.so: copied from $TARGET_SO_PATH to ohos demo directory: $OHO_SO_PROJECT_PATH (size=$DST_SIZE, ELF ok)"

TARGET_SO_HEADER_PATH=$PWD/demo/build/bin/ohosArm64/sharedDebugShared/libshared_api.h
OHO_SO_HEADER_PATH=$OHOS_RENDER_PROJECT_DIR/entry/src/main/cpp/thirdparty/biz_entry
mkdir -p "$OHO_SO_HEADER_PATH"
rm -f "$OHO_SO_HEADER_PATH/libshared_api.h"
cp -f "$TARGET_SO_HEADER_PATH" "$OHO_SO_HEADER_PATH/libshared_api.h"
echo "libshared_api.h: copied from $TARGET_SO_HEADER_PATH to ohos demo directory: $OHO_SO_HEADER_PATH"
echo "Copy ops done!"