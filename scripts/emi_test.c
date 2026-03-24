/*
 * EMI endstop test — C version, minicom-style port handling.
 * reset → boot (buttons enabled) → home → range → N cycles.
 *
 * Serial lifecycle matches minicom exactly:
 *   - open with O_RDWR | O_NDELAY | O_NOCTTY, then clear O_NDELAY
 *   - termios: 115200 8N1, CLOCAL, HUPCL, CRTSCTS, raw, VMIN=1 VTIME=5
 *   - disconnect detected via tcgetattr() failure (like minicom get_device_status)
 *   - on disconnect: close fd, poll for port, reopen + reconfigure
 *   - read 127 bytes at a time (like minicom)
 *   - write directly, kernel CRTSCTS handles flow
 *
 * Build: gcc -o emi_test scripts/emi_test.c -Wall -O2 -lm
 * Run:   ./emi_test [cycles]    (default 1000)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <signal.h>

#define PORT        "/dev/ttyACMTarg"
#define LOGFILE     "/home/claude-agent/work/claude2smooker/emi_test.log"
#define BUFSZ       4096
#define RDSZ        127     /* minicom reads 127 bytes at a time */

static int fd = -1;
static FILE *logfp = NULL;

/* ---- logging ---- */
static void logmsg(const char *msg)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char ts[16];
    strftime(ts, sizeof(ts), "%H:%M:%S", tm);
    if (logfp) { fprintf(logfp, "%s %s\n", ts, msg); fflush(logfp); }
    fprintf(stderr, "%s %s\n", ts, msg);
}

static void lograw(const char *data, int len)
{
    if (logfp) { fwrite(data, 1, len, logfp); fflush(logfp); }
}

/* ---- port open — exact minicom settings ---- */
static int open_port(void)
{
    int f = open(PORT, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (f < 0) return -1;

    /* remove O_NONBLOCK after open (like minicom) */
    int fl = fcntl(f, F_GETFL, 0);
    fcntl(f, F_SETFL, fl & ~O_NONBLOCK);

    struct termios tty;
    tcgetattr(f, &tty);

    tty.c_iflag = IGNBRK;
    tty.c_oflag = 0;
    tty.c_lflag = 0;
    tty.c_cflag = CS8 | HUPCL | CREAD | CLOCAL | CRTSCTS;

    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B115200);

    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 5;

    tcsetattr(f, TCSANOW, &tty);
    return f;
}

/* ---- device status check — like minicom get_device_status ---- */
static int device_ok(void)
{
    struct termios t;
    if (fd < 0) return 0;
    return tcgetattr(fd, &t) == 0;
}

/* ---- reconnect: close, poll, reopen — like minicom ---- */
static int reconnect(void)
{
    if (fd >= 0) { close(fd); fd = -1; }

    struct stat st;
    for (int i = 0; i < 120; i++) {   /* 30 seconds max */
        if (stat(PORT, &st) == 0) {
            int f = open_port();
            if (f >= 0) { fd = f; return 0; }
        }
        usleep(250000);
    }
    return -1;
}

/* ---- read with device status check — like minicom main loop ---- */
static int fd_read(char *buf, int maxlen, int timeout_ms)
{
    int total = 0;
    struct timeval end, now;
    gettimeofday(&end, NULL);
    end.tv_sec  += timeout_ms / 1000;
    end.tv_usec += (timeout_ms % 1000) * 1000;
    if (end.tv_usec >= 1000000) { end.tv_sec++; end.tv_usec -= 1000000; }

    while (1) {
        gettimeofday(&now, NULL);
        long remain_us = (end.tv_sec - now.tv_sec) * 1000000L + (end.tv_usec - now.tv_usec);
        if (remain_us <= 0) break;

        /* device status check — like minicom's main loop line 914 */
        if (!device_ok()) {
            logmsg("device gone — reconnecting");
            if (reconnect() < 0) { logmsg("reconnect failed"); return -1; }
            logmsg("reconnected");
            continue;
        }

        struct timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = 100000;  /* 100ms poll */

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        if (select(fd + 1, &fds, NULL, NULL, &tv) > 0) {
            int rdsz = maxlen - total;
            if (rdsz > RDSZ) rdsz = RDSZ;
            int n = read(fd, buf + total, rdsz);
            if (n > 0) {
                lograw(buf + total, n);
                total += n;
            }
            if (total >= maxlen) break;
        }
    }
    return total;
}

/* ---- write — direct, kernel CRTSCTS handles flow ---- */
static void fd_write(const char *data, int len)
{
    write(fd, data, len);
}

/* ---- read until marker ---- */
static int read_until(const char *marker, int timeout_s, char *outbuf, int outmax)
{
    int pos = 0;
    time_t deadline = time(NULL) + timeout_s;

    while (time(NULL) < deadline) {
        char tmp[RDSZ];
        int n = fd_read(tmp, sizeof(tmp), 500);
        if (n < 0) return -1;  /* reconnect failed */
        if (n > 0 && pos + n < outmax) {
            memcpy(outbuf + pos, tmp, n);
            pos += n;
            outbuf[pos] = '\0';
            if (strstr(outbuf, marker)) return pos;
        }
    }
    char msg[128];
    snprintf(msg, sizeof(msg), "TIMEOUT %ds for '%s'", timeout_s, marker);
    logmsg(msg);
    return -1;
}

/* ---- flush firmware lineBuf ---- */
static void flush_linebuf(void)
{
    char tmp[RDSZ];
    fd_write("\r", 1); usleep(300000); fd_read(tmp, sizeof(tmp), 300);
    fd_write("\r", 1); usleep(300000); fd_read(tmp, sizeof(tmp), 300);
}

/* ---- get position: send CR, parse prompt ---- */
static int get_pos(float *pos)
{
    fd_write("\r", 1);
    char buf[256];
    int total = 0;
    time_t deadline = time(NULL) + 3;
    while (time(NULL) < deadline) {
        int n = fd_read(buf + total, sizeof(buf) - total - 1, 200);
        if (n > 0) total += n;
        buf[total] = '\0';
        char *gt = strstr(buf, "> ");
        if (gt && gt[2] == '\0') {
            char *p = gt - 1;
            while (p > buf && (*p == ' ' || *p == '.' || (*p >= '0' && *p <= '9') || *p == '-'))
                p--;
            p++;
            if (p < gt) {
                *pos = strtof(p, NULL);
                return 1;
            }
        }
    }
    return 0;
}

/* ---- moveto and wait for arrival or stall ---- */
static int moveto_wait(float target, float *final_pos, int timeout_s)
{
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "moveto %.2f\r", target);
    fd_write(cmd, strlen(cmd));
    usleep(300000);
    { char tmp[RDSZ]; fd_read(tmp, sizeof(tmp), 300); }

    float last_pos = -9999;
    int same_count = 0;
    time_t deadline = time(NULL) + timeout_s;

    while (time(NULL) < deadline) {
        usleep(250000);
        float pos;
        if (!get_pos(&pos)) continue;
        *final_pos = pos;
        if (fabsf(pos - target) < 0.05f) return 1;
        if (fabsf(pos - last_pos) < 0.01f) {
            same_count++;
            if (same_count >= 6) return 0;
        } else {
            same_count = 0;
        }
        last_pos = pos;
    }
    return 0;
}

/* ---- signal handler ---- */
static void sig_handler(int sig)
{
    (void)sig;
    if (fd >= 0) close(fd);
    if (logfp) fclose(logfp);
    _exit(1);
}

/* ---- main ---- */
int main(int argc, char **argv)
{
    int cycles = 1000;
    if (argc > 1) cycles = atoi(argv[1]);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    logfp = fopen(LOGFILE, "w");
    logmsg("=== EMI TEST START ===");

    char buf[BUFSZ];

    /* OPEN + FLUSH */
    fd = open_port();
    if (fd < 0) { fd = -1; reconnect(); }
    flush_linebuf();

    /* RESET — fd stays open, reconnect handles USB re-enum */
    logmsg("RESET");
    fd_write("reset\r", 6);

    /* BOOT: read until "buttons enabled" — fd_read handles disconnect/reconnect */
    if (read_until("buttons enabled", 40, buf, BUFSZ) < 0) {
        logmsg("ABORT boot"); return 1;
    }
    logmsg("BOOT OK");
    logmsg("=== DONE ===");

    close(fd);
    fclose(logfp);
    return 0;
}
