#!/bin/bash
# Remove omarchy-quicklook. Cache and state (~/.cache, ~/.local/state/omarchy-quicklook) are kept.
set -euo pipefail

# D-Bus file first, so Files cannot re-activate the unit while it is being stopped.
rm -f ~/.local/share/dbus-1/services/org.gnome.NautilusPreviewer.service
systemctl --user stop omarchy-quicklook.service 2>/dev/null || true
systemctl --user disable omarchy-quicklook.service 2>/dev/null || true
rm -f ~/.config/systemd/user/omarchy-quicklook.service ~/.local/bin/omarchy-quicklook
systemctl --user daemon-reload

echo "omarchy-quicklook удалён. Files снова вызывает sushi из пакета, но без обходов 2026-10-13 (окно 875x600, холодный старт)."
echo "Кэш и состояние остались: rm -rf ~/.cache/omarchy-quicklook ~/.local/state/omarchy-quicklook"
