#!/usr/bin/env bash
set -eu

case ${1:-} in
  military-command://lanchat|military-command://lanchat/)
    cd /opt/jw/lanchat
    exec ./run.sh
    ;;
  military-command://scout-monitor|military-command://scout-monitor/)
    cd /opt/jw/qbcl/binr
    exec ./ScoutMonitor.sh
    ;;
  *) exit 2 ;;
esac
