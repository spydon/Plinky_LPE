#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

mkdocs build
rm -rf docs/build
mv site docs/build
