#!/bin/sh
app_dir=${0%/*}
cd "$app_dir" || exit 1
app_dir=$(pwd)
[ -f "$app_dir/home-panel" ] || exit 1
"$app_dir/home-panel" --prepare-network "$app_dir/secrets.json" || exit 1
[ ! -d /tmp/hello-world.lock ] || { echo "Hello World is running; stop it first." >&2; exit 1; }
# RAM lock prevents two copies from competing for the framebuffer.
mkdir /tmp/home-panel.lock 2>/dev/null || exit 0
echo $$ > /tmp/home-panel.lock/launcher.pid
cdr_pid=""
app_pid=""
cleanup() {
    [ -z "$app_pid" ] || { kill -TERM "$app_pid" 2>/dev/null; wait "$app_pid" 2>/dev/null; }
    [ -z "$cdr_pid" ] || kill -CONT "$cdr_pid" 2>/dev/null
    rm -f /tmp/home-panel.lock/launcher.pid /tmp/home-panel.lock/app.pid
    rm -r /tmp/home-panel.lock
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
"$app_dir/home-panel" &
app_pid=$!
echo "$app_pid" > /tmp/home-panel.lock/app.pid
wait "$app_pid"
result=$?
app_pid=""
exit "$result"
