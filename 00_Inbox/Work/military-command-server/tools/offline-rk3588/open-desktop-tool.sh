#!/usr/bin/env bash
set -eu

case ${1:-} in
  military-command://lanchat|military-command://lanchat/)
    cd /opt/jw/lanchat
    exec ./run.sh
    ;;
  *) exit 2 ;;
esac
