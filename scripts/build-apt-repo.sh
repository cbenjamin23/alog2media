#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: build-apt-repo.sh DEB_DIRECTORY OUTPUT_DIRECTORY" >&2
  exit 2
fi

for command_name in apt-ftparchive dpkg-deb dpkg-scanpackages gpg gzip xz; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    echo "missing required command: $command_name" >&2
    exit 2
  fi
done

source_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
deb_dir="$(cd "$1" && pwd -P)"
output_dir="$2"
public_key_gpg="${source_root}/packaging/apt/alog2media-archive-keyring.gpg"
public_key_asc="${source_root}/packaging/apt/alog2media-archive-keyring.asc"

if [[ -z "${APT_SIGNING_FINGERPRINT:-}" ]]; then
  echo "APT_SIGNING_FINGERPRINT is required" >&2
  exit 2
fi
if [[ -e "$output_dir" ]] && find "$output_dir" -mindepth 1 -print -quit | grep -q .; then
  echo "output directory must be absent or empty: $output_dir" >&2
  exit 2
fi
mkdir -p "$output_dir"
output_dir="$(cd "$output_dir" && pwd -P)"

tracked_fingerprint="$(
  gpg --batch --with-colons --show-keys "$public_key_gpg" |
    awk -F: '$1 == "fpr" {print $10; exit}'
)"
if [[ "$tracked_fingerprint" != "$APT_SIGNING_FINGERPRINT" ]]; then
  echo "signing key does not match the tracked APT public key" >&2
  exit 1
fi

mapfile -d '' debs < <(find "$deb_dir" -maxdepth 1 -type f -name '*.deb' -print0)
if [[ "${#debs[@]}" -eq 0 ]]; then
  echo "no .deb packages found in $deb_dir" >&2
  exit 2
fi

declare -A suite_arches=()
for deb in "${debs[@]}"; do
  package="$(dpkg-deb -f "$deb" Package)"
  suite="$(dpkg-deb -f "$deb" X-Alog2media-Codename)"
  architecture="$(dpkg-deb -f "$deb" Architecture)"
  if [[ "$package" != "alog2media" || ! "$suite" =~ ^[a-z0-9]+$ || \
        ! "$architecture" =~ ^[a-z0-9]+$ ]]; then
    echo "invalid alog2media package metadata: $deb" >&2
    exit 1
  fi
  pool_dir="$output_dir/pool/$suite/main/a/alog2media"
  mkdir -p "$pool_dir"
  cp "$deb" "$pool_dir/"
  suite_arches["$suite"]+=" $architecture"
done

for suite in "${!suite_arches[@]}"; do
  architectures="$(xargs -n1 <<< "${suite_arches[$suite]}" | LC_ALL=C sort -u | xargs)"
  for architecture in $architectures; do
    binary_dir="$output_dir/dists/$suite/main/binary-$architecture"
    mkdir -p "$binary_dir"
    (
      cd "$output_dir"
      dpkg-scanpackages --arch "$architecture" "pool/$suite" /dev/null
    ) > "$binary_dir/Packages"
    gzip -9n -c "$binary_dir/Packages" > "$binary_dir/Packages.gz"
    xz -9e -c "$binary_dir/Packages" > "$binary_dir/Packages.xz"
  done

  release_dir="$output_dir/dists/$suite"
  (
    cd "$output_dir"
    apt-ftparchive \
      -o "APT::FTPArchive::Release::Origin=alog2media" \
      -o "APT::FTPArchive::Release::Label=alog2media" \
      -o "APT::FTPArchive::Release::Suite=$suite" \
      -o "APT::FTPArchive::Release::Codename=$suite" \
      -o "APT::FTPArchive::Release::Architectures=$architectures" \
      -o 'APT::FTPArchive::Release::Components=main' \
      -o 'APT::FTPArchive::Release::Description=alog2media signed package repository' \
      release "dists/$suite"
  ) > "$release_dir/Release"

  gpg --batch --yes --local-user "$APT_SIGNING_FINGERPRINT" \
    --clearsign --output "$release_dir/InRelease" "$release_dir/Release"
  gpg --batch --yes --local-user "$APT_SIGNING_FINGERPRINT" \
    --armor --detach-sign --output "$release_dir/Release.gpg" \
    "$release_dir/Release"
done

cp "$public_key_gpg" "$output_dir/alog2media-archive-keyring.gpg"
cp "$public_key_asc" "$output_dir/alog2media-archive-keyring.asc"
cp "$source_root/packaging/apt/index.html" "$output_dir/index.html"
touch "$output_dir/.nojekyll"

printf 'Created signed APT repository at %s\n' "$output_dir"
