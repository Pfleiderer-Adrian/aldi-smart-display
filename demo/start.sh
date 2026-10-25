#!/bin/sh
app_dir=${0%/*}
cd "$app_dir" || exit 1
app_dir=$(pwd)
[ -f "$app_dir/hello-world" ] || exit 1
# RAM lock prevents two copies from competing for the framebuffer.
mkdir /tmp/hello-world.lock 2>/dev/null || exit 0
echo $$ > /tmp/hello-world.lock/launcher.pid
cdr_pid=""
app_pid=""
cleanup() {
    [ -z "$app_pid" ] || { kill -TERM "$app_pid" 2>/dev/null; wait "$app_pid" 2>/dev/null; }
    [ -z "$cdr_pid" ] || kill -CONT "$cdr_pid" 2>/dev/null
    rm -f /tmp/hello-world.lock/launcher.pid /tmp/hello-world.lock/app.pid
    rm -r /tmp/hello-world.lock
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
"$app_dir/hello-world" &
app_pid=$!
echo "$app_pid" > /tmp/hello-world.lock/app.pid
wait "$app_pid"
result=$?
app_pid=""
exit "$result"
