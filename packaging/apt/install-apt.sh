#!/bin/sh
set -eu

base_url=${ALOG2MEDIA_APT_BASE_URL:-https://cbenjamin23.github.io/alog2media/apt}
keyring=/usr/share/keyrings/alog2media-archive-keyring.gpg
sources_file=/etc/apt/sources.list.d/alog2media.list

if [ "$(id -u)" -ne 0 ]; then
  printf '%s\n' 'Run this setup script with sudo, for example: sudo bash install-apt.sh' >&2
  exit 1
fi

if ! command -v curl >/dev/null 2>&1; then
  printf '%s\n' 'curl is required. Install it with: sudo apt-get update && sudo apt-get install curl' >&2
  exit 1
fi

if [ ! -r /etc/os-release ]; then
  printf '%s\n' 'Cannot identify this Linux distribution: /etc/os-release is missing.' >&2
  exit 1
fi

# shellcheck disable=SC1091
. /etc/os-release
case "${VERSION_CODENAME:-}" in
  jammy|noble) ;;
  *)
    printf 'Unsupported Ubuntu codename: %s\nSupported codenames: jammy, noble\n' "${VERSION_CODENAME:-unknown}" >&2
    exit 1
    ;;
esac

if ! command -v dpkg >/dev/null 2>&1; then
  printf '%s\n' 'dpkg is required; this setup script supports Debian-family systems.' >&2
  exit 1
fi
architecture=$(dpkg --print-architecture)
case "$architecture" in
  amd64|arm64) ;;
  *)
    printf 'Unsupported architecture: %s\nSupported architectures: amd64, arm64\n' "$architecture" >&2
    exit 1
    ;;
esac

temporary_dir=$(mktemp -d)
trap 'rm -rf "$temporary_dir"' EXIT HUP INT TERM
curl --fail --silent --show-error --location \
  "$base_url/alog2media-archive-keyring.gpg" \
  --output "$temporary_dir/keyring.gpg"
install -d -m 0755 "$(dirname "$keyring")"
install -m 0644 "$temporary_dir/keyring.gpg" "$keyring"

printf 'deb [arch=%s signed-by=%s] %s %s main\n' \
  "$architecture" "$keyring" "$base_url" "$VERSION_CODENAME" \
  > "$temporary_dir/alog2media.list"
install -m 0644 "$temporary_dir/alog2media.list" "$sources_file"

printf '%s\n' \
  'Repository configured successfully.' \
  'Next, run:' \
  '  sudo apt update' \
  '  sudo apt install alog2media'
