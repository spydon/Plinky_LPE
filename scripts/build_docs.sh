#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

mkdocs build
rm -rf site/assets/javascripts/lunr/min site/assets/javascripts/lunr/tinyseg.js site/assets/javascripts/lunr/wordcut.js
rsync -a --delete --exclude='markdown/' site/ docs/
rm -rf site/
