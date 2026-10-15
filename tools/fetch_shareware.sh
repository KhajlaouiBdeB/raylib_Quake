#!/usr/bin/env bash
# Only the Quake 1.06 shareware distribution; never registered data.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
destination="$root/data/id1/pak0.pak"
archive_sha=ec6c9d34b1ae0252ac0066045b6611a7919c2a0d78a3a66d9387a8f597553239
pak_sha=35a9c55e5e5a284a159ad2a62e0e8def23d829561fe2f54eb402dbc0a9a946af
# GNU sha256sum on Linux, shasum on macOS (its sha256sum has no --check);
# 7z/7zz/7za (macOS: brew install sevenzip).
if [[ $(uname -s) != Darwin ]] && command -v sha256sum >/dev/null; then sha256_cmd=(sha256sum)
elif command -v shasum >/dev/null; then sha256_cmd=(shasum -a 256)
elif command -v sha256sum >/dev/null; then sha256_cmd=(sha256sum)
else echo 'FAIL: missing sha256sum or shasum' >&2; exit 1; fi
check() { printf '%s  %s\n' "$1" "$2" | "${sha256_cmd[@]}" --check --status; }
sevenzip=
for tool in 7z 7zz 7za; do
    if command -v "$tool" >/dev/null; then sevenzip=$tool; break; fi
done
[[ -n "$sevenzip" ]] || { echo 'FAIL: missing 7z/7zz/7za (macOS: brew install sevenzip)' >&2; exit 1; }
for tool in curl unzip; do
    command -v "$tool" >/dev/null || { echo "FAIL: missing $tool" >&2; exit 1; }
done
mkdir -p "$root/data/id1"
if [[ -e "$destination" ]]; then
    check "$pak_sha" "$destination" || { echo 'FAIL: existing pak0.pak checksum mismatch; refusing to overwrite' >&2; exit 1; }
    echo "PASS: existing shareware pak0.pak SHA256 $pak_sha"
    exit 0
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
"$sevenzip" e -so "$work/resource.1" ID1/PAK0.PAK > "$work/pak0.pak"
check "$pak_sha" "$work/pak0.pak" || { echo 'FAIL: extracted pak0.pak checksum mismatch' >&2; exit 1; }
mv -- "$work/pak0.pak" "$destination"
echo "PASS: installed data/id1/pak0.pak SHA256 $pak_sha"
