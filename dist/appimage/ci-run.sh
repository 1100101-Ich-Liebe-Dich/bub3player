#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# CI helper: runs a command, and if it fails, puts the end of its output into
# an annotation (readable on the pull request without opening the log).
#   dist/appimage/ci-run.sh "Titel" befehl args...
title=$1; shift
log=$(mktemp)
"$@" 2>&1 | tee "$log"
code=${PIPESTATUS[0]}
if [ "$code" -ne 0 ]; then
    msg=$(tail -n 60 "$log" | sed -e 's/%/%25/g' -e 's/\r//g' | awk '{printf "%s%%0A", $0}')
    echo "::error title=$title::$msg"
fi
exit "$code"
