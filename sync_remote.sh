#!/usr/bin/env bash
set -euo pipefail

# git push -u gitee master  # default
git push --force-with-lease gitee master
git push --force-with-lease github master

