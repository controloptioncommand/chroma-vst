#!/usr/bin/env bash
# Пересобирает docs/chroma-manual.pdf: свежие скриншоты из теста → нарезка → печать HTML в PDF.
# Нужны: Python 3 с Pillow, Node.js с Playwright. На Linux тест запускается через xvfb-run.
set -euo pipefail
cd "$(dirname "$0")/../.."

TEST=./build/ChromaTest_artefacts/Release/ChromaTest
SHOTS=$(mktemp -d)
trap 'rm -rf "$SHOTS"' EXIT

[ -d build ] || cmake -B build -DCHROMA_BUILD_TESTS=ON
cmake --build build --target ChromaTest
RUN=()
if [[ "$(uname)" == "Linux" ]]; then RUN=(xvfb-run -a); fi
"${RUN[@]}" "$TEST" "$SHOTS" | tail -1

python3 docs/manual/crop.py "$SHOTS" docs/manual/img
NODE_PATH="${NODE_PATH:-$(npm root -g)}" node docs/manual/print.js docs/manual/chroma-manual.html docs/chroma-manual.pdf
echo "docs/chroma-manual.pdf: $(python3 -c 'import os; print(os.path.getsize("docs/chroma-manual.pdf") // 1024)') KB"
