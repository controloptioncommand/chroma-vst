#!/bin/bash
# Установка chroma: копирует плагины в пользовательские папки и снимает карантин macOS.
# Запуск: распакуйте архив, затем в Терминале: bash install-mac.sh
set -e
cd "$(dirname "$0")"

VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3_DIR" "$AU_DIR"

rm -rf "$VST3_DIR/chroma.vst3" "$AU_DIR/chroma.component"
cp -R chroma.vst3 "$VST3_DIR/"
cp -R chroma.component "$AU_DIR/"

# плагин подписан ad-hoc (без Apple Developer ID) — снимаем карантин, иначе macOS его заблокирует
xattr -dr com.apple.quarantine "$VST3_DIR/chroma.vst3" "$AU_DIR/chroma.component" 2>/dev/null || true

# сбросить кэш Audio Units
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo "chroma установлен:"
echo "  $VST3_DIR/chroma.vst3"
echo "  $AU_DIR/chroma.component"
echo "В Ableton: Settings > Plug-Ins > Rescan (включите 'Use VST3 Plug-In Custom Folder' или 'Use Audio Units')."
