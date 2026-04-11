#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

mkdocs build
rsync -a --delete --exclude='markdown/' site/ docs/
rm -rf site/
