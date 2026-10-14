#!/bin/bash
# Build, test and install omarchy-quicklook as the GNOME Files previewer. Re-run to upgrade.
set -euo pipefail
cd "$(dirname "$0")"

pkgs=(cmake ninja qt6-base qt6-declarative qt6-multimedia qt6-multimedia-ffmpeg qt6-svg poppler-qt6
  taglib libheif libraw uchardet karchive syntax-highlighting kimageformats libreoffice-fresh uwsm)
if ! pacman -Q "${pkgs[@]}" &>/dev/null; then
  if command -v omarchy-pkg-add >/dev/null; then omarchy-pkg-add "${pkgs[@]}"
  else sudo pacman -S --needed --noconfirm "${pkgs[@]}"; fi
fi

# Separate lines, not one && chain: under set -e a failure mid-chain does not stop the
# script, and a stale binary would get installed.
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure --no-tests=error -LE perf

# install, never cp: it replaces symlinks (cp writes through them) and works over a running binary.
install -Dm755 build/omarchy-quicklook ~/.local/bin/omarchy-quicklook
install -Dm644 systemd/omarchy-quicklook.service ~/.config/systemd/user/omarchy-quicklook.service
install -Dm644 dbus/org.gnome.NautilusPreviewer.service ~/.local/share/dbus-1/services/org.gnome.NautilusPreviewer.service

# Tolerant of whole, dangling and missing links.
systemctl --user stop sushi.service 2>/dev/null || true
systemctl --user disable sushi.service 2>/dev/null || true
systemctl --user daemon-reload
systemctl --user enable omarchy-quicklook.service
systemctl --user restart omarchy-quicklook.service

busctl --user status org.gnome.NautilusPreviewer | grep -E '^(UserUnit|Comm)=' \
  || { echo "org.gnome.NautilusPreviewer не появился: journalctl --user -u omarchy-quicklook" >&2; exit 1; }
echo "Готово: omarchy-quicklook установлен и держит org.gnome.NautilusPreviewer. Выделите файл в Files и нажмите пробел."
