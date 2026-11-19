#!/usr/bin/env bash
set -eu

case ${1:-} in
  intelligent-cooperative://lanchat|intelligent-cooperative://lanchat/|intelligent-cooperative-tools://lanchat|intelligent-cooperative-tools://lanchat/|intelligent-command://lanchat|intelligent-command://lanchat/|intelligent-command-tools://lanchat|intelligent-command-tools://lanchat/|dispatch-tools://lanchat|dispatch-tools://lanchat/)
    cd /opt/jw/lanchat
    exec ./run.sh
    ;;
  intelligent-cooperative://scout-monitor|intelligent-cooperative://scout-monitor/|intelligent-cooperative-tools://scout-monitor|intelligent-cooperative-tools://scout-monitor/|intelligent-command://scout-monitor|intelligent-command://scout-monitor/|intelligent-command-tools://scout-monitor|intelligent-command-tools://scout-monitor/|dispatch-tools://scout-monitor|dispatch-tools://scout-monitor/)
    cd /opt/jw/qbcl/binr
    exec ./ScoutMonitor.sh
    ;;
  *) exit 2 ;;
esac
