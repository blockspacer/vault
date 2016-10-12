# Script for building gtest for ios
#
# Usage: build-gtest src_dir dst_dir
#

if [[ -z "$1" ]] || [[ -z "$2" ]]; then
  echo "Usage: build-gtest src_dir dst_dir"
  exit 1
fi

SRC_DIR="$1"
DST_DIR="$2"

GTEST_ZIP="release-1.7.0.zip"
GTEST_SHA1="f85f6d2481e2c6c4a18539e391aa4ea8ab0394af"
GTEST_URL="https://github.com/google/googletest/archive/${GTEST_ZIP}"
GTEST_SRC_DIR="${SRC_DIR}/googletest-${GTEST_ZIP%.zip}"

${TOOLS_DIR}/download_and_extract.sh ${GTEST_URL} ${GTEST_SHA1} ${SRC_DIR}

pushd ${GTEST_SRC_DIR}
autoreconf --install
autoconf
./configure
make clean
make

# gtest doesn't have a make install, so do it manually.
mkdir -p "${DST_DIR}/include"
cp -Rf include/* "${DST_DIR}/include"

mkdir -p "${DST_DIR}/lib"
cp -Rf lib/.libs/*.a "${DST_DIR}/lib"
cp -Rf lib/.libs/*.dylib "${DST_DIR}/lib"

popd
