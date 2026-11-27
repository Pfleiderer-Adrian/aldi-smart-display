#!/bin/sh
# Optional Wi-Fi client setup, confined to MOVA Panel and /tmp.
app_dir=${0%/*}
"$app_dir/mova-panel" --prepare-network "$app_dir/secrets.json" || exit 1
[ -f /tmp/mova-panel-wifi.conf ] || exit 0
[ -d /sys/class/net/wlan1 ] || { echo "MOVA Panel: wlan1 missing" >&2; exit 1; }
status=$(wpa_cli -p/tmp/mova-panel-wpa -iwlan1 status 2>/dev/null)
case "$status" in *wpa_state=COMPLETED*) "$app_dir/mova-panel" --sync-time; exit $? ;; esac
# Only stop a supplicant owned by this application.
if [ -f /tmp/mova-panel-wpa.pid ]; then
 read -r pid < /tmp/mova-panel-wpa.pid
 case "$pid" in *[!0-9]*|'') ;; *)
  if [ -f /proc/$pid/comm ]; then
   read -r name < /proc/$pid/comm
   [ "$name" != wpa_supplicant ] || kill -TERM "$pid"
  fi ;;
 esac
fi
sleep 1
rm -f /tmp/mova-panel-wpa/wlan1
ifconfig wlan1 up || exit 1
/usr/bin/setsid wpa_supplicant -B -Dnl80211 -iwlan1 -c/tmp/mova-panel-wifi.conf -P/tmp/mova-panel-wpa.pid >/tmp/mova-panel-wpa.log 2>&1 || exit 1
i=0
while [ "$i" -lt 30 ]; do
 status=$(wpa_cli -p/tmp/mova-panel-wpa -iwlan1 status 2>/dev/null)
 case "$status" in *wpa_state=COMPLETED*)
  /usr/bin/setsid udhcpc -i wlan1 -b -p /tmp/mova-panel-dhcp.pid -t 4 -T 3 >/tmp/mova-panel-dhcp.log 2>&1
  result=$?
  [ "$result" -eq 0 ] || exit "$result"
  "$app_dir/mova-panel" --sync-time
  exit $? ;;
 esac
 i=$((i+1));sleep 1
done
echo "MOVA Panel: Wi-Fi association failed; UI will show offline" >&2
exit 1
