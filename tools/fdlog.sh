#!/bin/bash
# tools/fdlog.sh [N] [cmd [wait]]: optional test command(s) to FrontierDog.test (';' separated), then the last N log lines
N=${1:-40}; CMD=${2:-}; W=${3:-4}
if [ -n "$CMD" ]; then
  ssh gamepc "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\Users\\iilly\\Downloads\\claude-tmp\\fdcmd.ps1 -Cmd \"$CMD\" -Wait $W" >/dev/null
fi
ssh gamepc "powershell -NoProfile -Command \"[Console]::OutputEncoding=[Text.Encoding]::UTF8; Get-Content 'C:\\Program Files (x86)\\Steam\\steamapps\\common\\Red Dead Redemption 2\\FrontierDog.log' -Encoding UTF8 | Select -Last $N\"" | LANG=en_US.UTF-8 python3 -c '
import sys
for l in sys.stdin.buffer.read().decode("utf-8","replace").splitlines(): print(l[:230])'
