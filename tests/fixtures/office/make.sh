#!/bin/bash
# Regenerates the office fixtures with headless LibreOffice and a PRIVATE throwaway profile.
# LC_ALL=C.UTF-8 keeps the default page A4 whatever the session locale (en_US would give Letter).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'kill "${pid:-}" 2>/dev/null || true; rm -rf "$tmp"' EXIT
export LC_ALL=C.UTF-8
prof=(-env:UserInstallation=file://$tmp/profile --headless --invisible --norestore --nolockcheck)
printf 'Hello quicklook\n' > "$tmp/legacy.txt"
soffice "${prof[@]}" --convert-to doc:"MS Word 97" --outdir "$here" "$tmp/legacy.txt" >/dev/null
soffice "${prof[@]}" --convert-to odt --outdir "$here" "$tmp/legacy.txt" >/dev/null
mv "$here/legacy.odt" "$here/a.odt"
soffice "${prof[@]}" --accept="socket,host=localhost,port=2917;urp;" & pid=$!
python3 "$here/mkenc.py" "$here" 2917
wait "$pid" 2>/dev/null || true
