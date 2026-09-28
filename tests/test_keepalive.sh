#!/usr/bin/env bash
set -euo pipefail
python3 tests/test_keepalive.py "${1:-8080}"
