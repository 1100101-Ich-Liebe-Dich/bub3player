#!/bin/bash
# CI helper: runs a command, and if it fails, puts the end of its output into
# an annotation (readable on the pull request without opening the log).
#   dist/appimage/ci-run.sh "Titel" befehl args...
title=$1; shift
log=$(mktemp)
set -o pipefail
if "$@" 2>&1 | tee "$log"; then
    exit 0
fi
code=$?
msg=$(tail -n 60 "$log" | sed -e 's/%/%25/g' -e 's/\r//g' | awk '{printf "%s%%0A", $0}')
echo "::error title=$title::$msg"
exit $code
