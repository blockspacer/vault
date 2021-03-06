# Script for building keyczar for ios
#
# Usage: build-keyczar src_dir dst_dir
#

set -e

if [[ -z "$1" ]] || [[ -z "$2" ]]; then
  echo "Usage: build-keyczar src_dir dst_dir"
  exit 1
fi

SRC_DIR="$1"
DST_DIR="$2"

KEYCZAR_TAR_GZ="Cpp_release_0.71.tar.gz"
KEYCZAR_SHA1="c7bc0fce0cdd6a150a6fdf457bb8aec28dcece8a"
KEYCZAR_URL="https://github.com/google/keyczar/archive/${KEYCZAR_TAR_GZ}"
KEYCZAR_SRC_DIR="${SRC_DIR}/keyczar-Cpp_release_0.71/cpp"
KEYZAR_RELEASE_DIR="${SRC_DIR}/keyczar-Cpp_release_0.71"

if [ ${PLATFORM} == 'iPhoneOS' ] || [ ${PLATFORM} == 'iPhoneSimulator' ]; then
  SCONS_MODE=opt-ios
else
  SCONS_MODE=opt-mac
fi

echo "mode: ${SCONS_MODE}"

if [ ! -e ${KEYCZAR_SRC_DIR} ]; then
  "${TOOLS_DIR}/download_and_extract.sh" ${KEYCZAR_URL} ${KEYCZAR_SHA1} ${SRC_DIR}

  pushd ${KEYZAR_RELEASE_DIR}
  echo "pushd to ${KEYZAR_RELEASE_DIR}"
  patch --verbose -p1 < "${PATCH_DIR}/keyczar-ios.patch"
  patch --verbose -p1 < "${PATCH_DIR}/keyczar-arm64.patch"
  popd
fi

echo "Building keyczar for ${PLATFORM}${SDK_VERSION}"
pushd ${KEYCZAR_SRC_DIR}
cd src && sh ./tools/swtoolkit/hammer.sh --mode=${SCONS_MODE} prefix="${DST_DIR}" --compat --verbose

echo "Installing keyczar libs to ${DST_DIR}/lib"
./tools/swtoolkit/hammer.sh --mode=${SCONS_MODE} prefix="${DST_DIR}" --compat install
popd
