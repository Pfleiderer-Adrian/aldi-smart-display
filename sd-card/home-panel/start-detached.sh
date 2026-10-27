#!/bin/sh
# Start outside the ADB session. Usage: /bin/sh /path/to/start-detached.sh
app_dir=${0%/*}
cd "$app_dir" || exit 1
app_dir=$(pwd)
[ ! -d /tmp/home-panel.lock ] || { echo "Panel already running or stale lock" >&2; exit 1; }
/usr/bin/setsid /bin/sh "$app_dir/start.sh" </dev/null >/tmp/home-panel-live.log 2>&1 &
# Wi-Fi association may take up to 30 seconds before the UI starts.
i=0
while [ "$i" -lt 45 ]; do
 if [ -f /tmp/home-panel.lock/app.pid ]; then
  echo "Home Panel started independently of ADB"
  exit 0
 fi
 sleep 1
 [ -f /tmp/home-panel.lock/launcher.pid ] || break
 i=$((i+1))
done
echo "Panel failed to start; check /tmp/home-panel-live.log" >&2
exit 1
