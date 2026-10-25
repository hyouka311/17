// Reads the touchscreen straight from /dev/input (run.sh does chmod 666 on the nodes as root)
// and maps raw portrait coordinates to the game's landscape screen.
#include "touch.h"
#include <thread>
#include <atomic>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <android/log.h>

static std::atomic<int>  rawX{0}, rawY{0};
static std::atomic<bool> down{false};
static int maxX = 1, maxY = 1, rot = 1;   // rot: 0=portrait, 1=landscape (90), 3=landscape (270)

static bool Bit(int b, const unsigned char* a) { return a[b / 8] & (1 << (b % 8)); }

static int FindTouchDevice() {
    for (int i = 0; i < 32; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        unsigned char abs[(ABS_MAX + 8) / 8];
        memset(abs, 0, sizeof(abs));
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs);
        if (Bit(ABS_MT_POSITION_X, abs) && Bit(ABS_MT_POSITION_Y, abs)) {
            input_absinfo ax{}, ay{};
            ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ax);
            ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ay);
            maxX = ax.maximum > 0 ? ax.maximum : 1;
            maxY = ay.maximum > 0 ? ay.maximum : 1;
            __android_log_print(ANDROID_LOG_INFO, "mod", "touch dev %s max=%d,%d", path, maxX, maxY);
            return fd;
        }
        close(fd);
    }
    return -1;
}

static void Reader() {
    if (FILE* f = fopen("/data/local/tmp/mod_rot", "r")) { fscanf(f, "%d", &rot); fclose(f); }
    int fd = -1;
    while ((fd = FindTouchDevice()) < 0) {
        __android_log_print(ANDROID_LOG_WARN, "mod", "no touch device readable (did run.sh chmod /dev/input?)");
        sleep(3);
    }
    int slot = 0;
    input_event ev;
    while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type != EV_ABS) continue;
        if (ev.code == ABS_MT_SLOT) slot = ev.value;
        else if (slot != 0) continue;                      // first finger only
        else if (ev.code == ABS_MT_POSITION_X) rawX = ev.value;
        else if (ev.code == ABS_MT_POSITION_Y) rawY = ev.value;
        else if (ev.code == ABS_MT_TRACKING_ID) down = (ev.value != -1);
    }
}

void TouchStart() { std::thread(Reader).detach(); }

bool TouchGet(float W, float H, float& x, float& y) {
    float nx = (float)rawX / maxX, ny = (float)rawY / maxY;   // 0..1 in portrait space
    if (rot == 1)      { x = ny * W;          y = (1.f - nx) * H; }
    else if (rot == 3) { x = (1.f - ny) * W;  y = nx * H; }
    else               { x = nx * W;          y = ny * H; }
    return down;
}
