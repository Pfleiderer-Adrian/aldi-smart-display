#!/bin/sh
# Optional Wi-Fi client setup, confined to Home Panel and /tmp.
app_dir=${0%/*}
"$app_dir/home-panel" --prepare-network "$app_dir/secrets.json" || exit 1
[ -d /sys/class/net/wlan1 ] || { echo "Home Panel: wlan1 missing" >&2; exit 1; }
status=$(wpa_cli -p/tmp/home-panel-wpa -iwlan1 status 2>/dev/null)
case "$status" in *wpa_state=COMPLETED*) exit 0 ;; esac
# Only stop a supplicant owned by this application.
if [ -f /tmp/home-panel-wpa.pid ]; then
 read -r pid < /tmp/home-panel-wpa.pid
 case "$pid" in *[!0-9]*|'') ;; *)
  if [ -f /proc/$pid/comm ]; then
   read -r name < /proc/$pid/comm
   [ "$name" != wpa_supplicant ] || kill -TERM "$pid"
  fi ;;
 esac
fi
sleep 1
rm -f /tmp/home-panel-wpa/wlan1
ifconfig wlan1 up || exit 1
/usr/bin/setsid wpa_supplicant -B -Dnl80211 -iwlan1 -c/tmp/home-panel-wifi.conf -P/tmp/home-panel-wpa.pid >/tmp/home-panel-wpa.log 2>&1 || exit 1
i=0
while [ "$i" -lt 30 ]; do
 status=$(wpa_cli -p/tmp/home-panel-wpa -iwlan1 status 2>/dev/null)
 case "$status" in *wpa_state=COMPLETED*)
  /usr/bin/setsid udhcpc -i wlan1 -b -p /tmp/home-panel-dhcp.pid -t 4 -T 3 >/tmp/home-panel-dhcp.log 2>&1
  exit $? ;;
 esac
 i=$((i+1));sleep 1
done
echo "Home Panel: Wi-Fi association failed; UI will show offline" >&2
exit 1
