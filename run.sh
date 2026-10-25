#!/system/bin/sh
# Run from MT Manager as ROOT. Put libcheat.so AND injector in the SAME folder as this script.
PKG=com.standrise.game
DIR="$(cd "$(dirname "$0")" && pwd)"
LIB=/data/local/tmp/libcheat.so
INJ=/data/local/tmp/injector

[ "$(id -u)" = "0" ] || { echo "Need root: run with root in MT Manager"; exit 1; }
[ -f "$DIR/libcheat.so" ] && [ -f "$DIR/injector" ] || { echo "libcheat.so and injector must be next to run.sh"; exit 1; }

cp "$DIR/libcheat.so" $LIB; cp "$DIR/injector" $INJ
chmod 755 $LIB $INJ
chcon u:object_r:apk_data_file:s0 $LIB 2>/dev/null
setenforce 0
chmod 666 /dev/input/event* 2>/dev/null
[ -f /data/local/tmp/mod_rot ] || echo 1 > /data/local/tmp/mod_rot

am force-stop $PKG
# try to force OpenGL ES (the menu hook needs GLES, it cannot draw on Vulkan)
COMP=$(cmd package resolve-activity --brief -c android.intent.category.LAUNCHER $PKG 2>/dev/null | tail -n 1)
if [ -n "$COMP" ]; then
  am start -n "$COMP" -e unity "-force-gles30" >/dev/null 2>&1
else
  monkey -p $PKG -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
fi
echo "Waiting for game..."
PID=""
for i in $(seq 1 60); do
  PID=$(pidof $PKG | awk '{print $1}')
  [ -n "$PID" ] && grep -q libil2cpp.so /proc/$PID/maps 2>/dev/null && break
  sleep 1
done
[ -n "$PID" ] || { echo "Game process not found"; exit 1; }
sleep 3
echo "Injecting into pid $PID"
$INJ $PID $LIB
sleep 12
echo "--- mod log ---"
logcat -d -s mod 2>/dev/null | tail -15
echo "(expect: libcheat loaded / il2cpp hooks installed / eglSwapBuffers hooked / swap called)"
