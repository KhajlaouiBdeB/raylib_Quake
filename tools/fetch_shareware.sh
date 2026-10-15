#!/usr/bin/env bash
# Only the Quake 1.06 shareware distribution; never registered data.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
destination="$root/data/id1/pak0.pak"
# The shareware license texts, which must accompany the PAK when it is
# redistributed (the browser build ships them).
licenses="$root/data/shareware"
license_files=(SLICNSE.TXT LICINFO.TXT)
have_licenses() { for f in "${license_files[@]}"; do [[ -s "$licenses/$f" ]] || return 1; done; }
archive_sha=ec6c9d34b1ae0252ac0066045b6611a7919c2a0d78a3a66d9387a8f597553239
pak_sha=35a9c55e5e5a284a159ad2a62e0e8def23d829561fe2f54eb402dbc0a9a946af
# macOS has no GNU sha256sum (BSD sha256sum lacks --check) and Homebrew installs 7-Zip as 7zz.
sha256() { if command -v shasum >/dev/null; then shasum -a 256 -- "$1"; else sha256sum -- "$1"; fi | cut -d' ' -f1; }
check() { [[ $(sha256 "$2") == "$1" ]]; }
sevenz=$(command -v 7z || command -v 7zz || true)
for tool in curl unzip; do
    command -v "$tool" >/dev/null || { echo "FAIL: missing $tool" >&2; exit 1; }
done
command -v shasum >/dev/null || command -v sha256sum >/dev/null || { echo 'FAIL: missing shasum/sha256sum' >&2; exit 1; }
[[ -n $sevenz ]] || { echo 'FAIL: missing 7z/7zz' >&2; exit 1; }
mkdir -p "$root/data/id1"
if [[ -e "$destination" ]]; then
    check "$pak_sha" "$destination" || { echo 'FAIL: existing pak0.pak checksum mismatch; refusing to overwrite' >&2; exit 1; }
    echo "PASS: existing shareware pak0.pak SHA256 $pak_sha"
    have_licenses && exit 0
fi
work=$(mktemp -d "$root/data/fetch.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
curl --fail --location --proto '=https' --proto-redir '=https' \
    --connect-timeout 15 --max-time 120 --retry 2 \
    https://www.gamers.org/pub/idgames/idstuff/quake/quake106.zip -o "$work/quake106.zip"
check "$archive_sha" "$work/quake106.zip" || { echo 'FAIL: quake106.zip checksum mismatch' >&2; exit 1; }
echo "PASS: quake106.zip SHA256 $archive_sha"
unzip -p "$work/quake106.zip" resource.1 > "$work/resource.1"
# resource.1 is a DOS self-extracting LZH; do not execute its DOS code.
mkdir -p "$licenses"
for f in "${license_files[@]}"; do
    "$sevenz" e -so "$work/resource.1" "$f" > "$work/$f"
    [[ -s "$work/$f" ]] || { echo "FAIL: $f not found in the archive" >&2; exit 1; }
    mv -- "$work/$f" "$licenses/$f"
done
echo "PASS: installed data/shareware/${license_files[*]}"
if [[ ! -e "$destination" ]]; then
    "$sevenz" e -so "$work/resource.1" ID1/PAK0.PAK > "$work/pak0.pak"
    check "$pak_sha" "$work/pak0.pak" || { echo 'FAIL: extracted pak0.pak checksum mismatch' >&2; exit 1; }
    mv -- "$work/pak0.pak" "$destination"
    echo "PASS: installed data/id1/pak0.pak SHA256 $pak_sha"
fi
