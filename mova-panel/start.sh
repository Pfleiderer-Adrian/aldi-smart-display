#!/bin/sh
# Script-only display orientation: landscape or portrait (90 degrees clockwise).
# Change orientation below, or set MOVA_ORIENTATION before starting this script.
orientation=portrait
MOVA_ORIENTATION=${MOVA_ORIENTATION:-$orientation}
export MOVA_ORIENTATION
app_dir=${0%/*}
cd "$app_dir" || exit 1
app_dir=$(pwd)
[ -f "$app_dir/mova-panel" ] || exit 1
"$app_dir/mova-panel" --prepare-network "$app_dir/secrets.json" || exit 1
[ ! -d /tmp/home-panel.lock ] || { echo "Home Panel is running; stop it first." >&2; exit 1; }
[ ! -d /tmp/hello-world.lock ] || { echo "Hello World is running; stop it first." >&2; exit 1; }
# RAM lock prevents two copies from competing for the framebuffer.
mkdir /tmp/mova-panel.lock 2>/dev/null || exit 0
echo $$ > /tmp/mova-panel.lock/launcher.pid
cdr_pid=""
app_pid=""
cleanup() {
    [ -z "$app_pid" ] || { kill -TERM "$app_pid" 2>/dev/null; wait "$app_pid" 2>/dev/null; }
    [ -z "$cdr_pid" ] || kill -CONT "$cdr_pid" 2>/dev/null
    rm -f /tmp/mova-panel.lock/launcher.pid /tmp/mova-panel.lock/app.pid
    rm -r /tmp/mova-panel.lock
}
trap cleanup 0
trap 'exit 0' 1 2 15
for f in /proc/[0-9]*/comm; do
    read -r name < "$f" 2>/dev/null || continue
    [ "$name" = cdr ] || continue
    pid=${f#/proc/}; pid=${pid%/comm}
    state=$(grep '^State:' /proc/$pid/status)
    case "$state" in *stopped*) ;; *) kill -STOP "$pid" || exit 1; cdr_pid=$pid ;; esac
    break
done
[ ! -f "$app_dir/network-start.sh" ] || /bin/sh "$app_dir/network-start.sh"
"$app_dir/mova-panel" &
app_pid=$!
echo "$app_pid" > /tmp/mova-panel.lock/app.pid
wait "$app_pid"
result=$?
app_pid=""
exit "$result"
