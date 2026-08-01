#!/usr/bin/env bash

set -euo pipefail

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "build-deb.sh must run on Linux" >&2
  exit 2
fi

for command_name in cmake dpkg dpkg-deb dpkg-shlibdeps strip; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    echo "missing required command: $command_name" >&2
    exit 2
  fi
done

source_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
output_argument="${1:-${source_root}/dist}"
mkdir -p "$output_argument"
output_dir="$(cd "$output_argument" && pwd -P)"

if [[ -z "${MOOS_IVP_ROOT:-}" || ! -f "${MOOS_IVP_ROOT}/lib/libmarineview.a" ]]; then
  echo "MOOS_IVP_ROOT must name a configured, built MOOS-IvP checkout" >&2
  exit 2
fi

project_version="$(
  sed -nE 's/^project\(alog2media VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' \
    "${source_root}/CMakeLists.txt"
)"
if [[ -z "$project_version" ]]; then
  echo "could not read the alog2media project version" >&2
  exit 2
fi

base_version="${ALOG2MEDIA_VERSION:-$project_version}"
if [[ "$base_version" != "$project_version" ]]; then
  echo "requested version $base_version does not match project version $project_version" >&2
  exit 2
fi

apt_codename="${ALOG2MEDIA_APT_CODENAME:-}"
if [[ -z "$apt_codename" && -r /etc/os-release ]]; then
  # shellcheck disable=SC1091
  apt_codename="$(. /etc/os-release && printf '%s' "${VERSION_CODENAME:-}")"
fi
if [[ ! "$apt_codename" =~ ^[a-z0-9]+$ ]]; then
  echo "ALOG2MEDIA_APT_CODENAME must be a Debian/Ubuntu codename" >&2
  exit 2
fi

deb_revision="${ALOG2MEDIA_DEB_REVISION:-1~${apt_codename}}"
deb_version="${base_version}-${deb_revision}"
if ! dpkg --compare-versions "$deb_version" ge 0; then
  echo "invalid Debian version: $deb_version" >&2
  exit 2
fi

architecture="$(dpkg --print-architecture)"
work_root="$(mktemp -d)"
trap 'rm -rf "$work_root"' EXIT
build_dir="${work_root}/build"
package_root="${work_root}/debian/alog2media"

cmake -S "$source_root" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_INSTALL_LIBDIR=lib/alog2media \
  -DBUILD_TESTING=OFF \
  -DMOOS_IVP_ROOT="$MOOS_IVP_ROOT"
cmake --build "$build_dir" --parallel "${ALOG2MEDIA_BUILD_JOBS:-2}"
DESTDIR="$package_root" cmake --install "$build_dir"

strip --strip-unneeded "$package_root/usr/bin/alog2media"
while IFS= read -r -d '' library; do
  strip --strip-unneeded "$library"
done < <(find "$package_root/usr/lib/alog2media" -type f -print0)

install -Dm644 "$source_root/packaging/debian/copyright" \
  "$package_root/usr/share/doc/alog2media/copyright"

mkdir -p "$work_root/debian"
printf '%s\n' \
  'Source: alog2media' \
  'Section: utils' \
  'Priority: optional' \
  '' \
  'Package: alog2media' \
  'Architecture: any' \
  'Description: headless pMarineViewer scene exporter' \
  > "$work_root/debian/control"

private_library_dir="$package_root/usr/lib/alog2media"
shlibs_output="$(
  cd "$work_root"
  dpkg-shlibdeps --ignore-missing-info -O \
    -l"$private_library_dir" \
    -e"$package_root/usr/bin/alog2media" \
    -e"$(find "$private_library_dir" -type f -name 'libMOOSGeodesy.*' -print -quit)"
)"
runtime_dependencies="${shlibs_output#shlibs:Depends=}"
if [[ -z "$runtime_dependencies" || "$runtime_dependencies" == "$shlibs_output" ]]; then
  echo "dpkg-shlibdeps did not produce runtime dependencies" >&2
  exit 1
fi
runtime_dependencies="${runtime_dependencies}, ffmpeg, fonts-dejavu-core"

installed_size="$(du -sk "$package_root/usr" | awk '{print $1}')"
mkdir -p "$package_root/DEBIAN"
printf '%s\n' \
  'Package: alog2media' \
  "Version: $deb_version" \
  'Section: utils' \
  'Priority: optional' \
  "Architecture: $architecture" \
  'Maintainer: Charles Benjamin <85846095+cbenjamin23@users.noreply.github.com>' \
  "Installed-Size: $installed_size" \
  "Depends: $runtime_dependencies" \
  'Homepage: https://github.com/cbenjamin23/alog2media' \
  "X-Alog2media-Codename: $apt_codename" \
  'Description: headless pMarineViewer scene exporter' \
  ' Convert a MOOS-IvP .alog into an MP4, animated GIF, or lossless PNG' \
  ' snapshot without opening alogview or a display-server window.' \
  > "$package_root/DEBIAN/control"

package_path="${output_dir}/alog2media_${deb_version}_${architecture}.deb"
dpkg-deb --root-owner-group --build "$package_root" "$package_path"
dpkg-deb --info "$package_path"
printf 'Created %s\n' "$package_path"
