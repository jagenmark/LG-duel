#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
python_bin="${PYTHON:-python3}"

case "${1:-}" in
  start|stop|restart|status)
    exec "$python_bin" "$script_dir/lg_launch.py" "$@"
    ;;
  *)
    exec "$python_bin" "$script_dir/lg_control.py" "$@"
    ;;
esac
