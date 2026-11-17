#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "font.h"

typedef struct { u8 r, g, b; } Col;
typedef struct { u8 *fb; int w; } Surf;

// ---------- LED (ptm:sysm cmd 0x0801, SetInfoLEDPattern, 0x64 bytes) ----------
static const u8 spdDelay[3] = {0x60, 0x30, 0x18};
static const int spdMs[3]   = {900, 500, 250};
static const char *spdName[3] = {"SLOW", "MED", "FAST"};

static Result setLed(u8 r, u8 g, u8 b, bool on, bool blink, int spd) {
    Handle h;
    Result res = srvGetServiceHandle(&h, "ptm:sysm");
    if (R_FAILED(res)) return res;
    u8 p[0x64];
    memset(p, 0, sizeof p);
    p[0] = blink ? spdDelay[spd] : 0x10;
    for (int i = 0; i < 32; i++) {
        bool v = on && (!blink || i < 16);
        p[4 + i]  = v ? r : 0;
        p[36 + i] = v ? g : 0;
        p[68 + i] = v ? b : 0;
    }
    u32 *cmd = getThreadCommandBuffer();
    cmd[0] = 0x08010640;
    memcpy(&cmd[1], p, sizeof p);
    res = svcSendSyncRequest(h);
    if (R_SUCCEEDED(res)) res = cmd[1];
    svcCloseHandle(h);
    return res;
}

// While the low battery alert is running, the user's settings are ignored.
static bool lowBat = false;
static Result ledSet(u8 r, u8 g, u8 b, bool on, bool blink, int spd) {
    if (lowBat) return 0;
    return setLed(r, g, b, on, blink, spd);
}

// ---------- tiny software renderer (framebuffer is 240 high, column-major, BGR) ----------
static inline void blendpx(Surf *s, int x, int y, Col c, int a) {
    if (x < 0 || y < 0 || x >= s->w || y >= 240) return;
    u8 *p = s->fb + (x * 240 + (239 - y)) * 3;
    p[0] = (p[0] * (255 - a) + c.b * a) / 255;
    p[1] = (p[1] * (255 - a) + c.g * a) / 255;
    p[2] = (p[2] * (255 - a) + c.r * a) / 255;
}
static void rect(Surf *s, int x, int y, int w, int h, Col c) {
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) blendpx(s, x + i, y + j, c, 255);
}
static void rrect(Surf *s, int x, int y, int w, int h, int rad, Col c) {
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
        int dx = 0, dy = 0;
        if (i < rad) dx = rad - i; else if (i >= w - rad) dx = i - (w - rad) + 1;
        if (j < rad) dy = rad - j; else if (j >= h - rad) dy = j - (h - rad) + 1;
        if (dx * dx + dy * dy > rad * rad) continue;
        blendpx(s, x + i, y + j, c, 255);
    }
}
static void circle(Surf *s, int cx, int cy, int r, Col c) {
    for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++)
        if (i * i + j * j <= r * r) blendpx(s, cx + i, cy + j, c, 255);
}
static void text(Surf *s, int x, int y, const char *t, Col c) {
    for (; *t; t++, x += 6) {
        int ch = *t;
        if (ch < 32 || ch > 126) continue;
        for (int j = 0; j < 10; j++) for (int i = 0; i < 6; i++)
            if (font[ch - 32][j] & (0x20 >> i)) blendpx(s, x + i, y + j, c, 255);
    }
}

// ---------- colors ----------
static const Col BG = {0xEC, 0xE6, 0xE0}, OUT = {0x2B, 0x2D, 0x3A}, INN = {0xF4, 0xF4, 0xF4};
static const Col LINE = {0x8A, 0x8A, 0xA0}, DOT = {0x6E, 0x6E, 0x8C};
static const Col UIBG = {0xEE, 0xEA, 0xE5}, BTN = {0xD6, 0xE4, 0xE8}, ACT = {0x2A, 0x9D, 0xB0};
static const Col TXT = {0x2D, 0x7F, 0x90}, WHITE = {255, 255, 255}, DIS = {0xCC, 0xCC, 0xCC}, DISTXT = {0x99, 0x99, 0x99};

// ---------- top screen: the 3DS picture with the LED in the top right corner ----------
static void drawPicture(Surf *s, bool lit, Col c) {
    rect(s, 0, 0, 400, 240, BG);
    rrect(s, 40, 40, 320, 181, 16, OUT);
    rrect(s, 44, 44, 312, 173, 13, INN);
    rect(s, 44, 66, 312, 2, LINE);          // hinge line
    rect(s, 84, 44, 2, 22, LINE);           // small left divider
    rect(s, 320, 44, 2, 22, LINE);          // divider before the LED box
    rect(s, 322, 44, 34, 22, (Col){0xDA, 0xDA, 0xDA});
    circle(s, 160, 198, 7, DOT);            // the two dots at the bottom
    circle(s, 238, 198, 7, DOT);
    const int cx = 339, cy = 55, R = 42;
    if (lit) {
        for (int j = -R; j <= R; j++) for (int i = -R; i <= R; i++) {
            float d = sqrtf((float)(i * i + j * j)) / R;
            if (d >= 1.0f) continue;
            float a = (1.0f - d); a *= a;
            blendpx(s, cx + i, cy + j, c, (int)(a * 235));
        }
        rect(s, cx - 4, cy - 4, 8, 8, c);
    } else {
        rect(s, cx - 4, cy - 4, 8, 8, (Col){0x80, 0x80, 0x80});
    }
}

// ---------- bottom screen UI ----------
typedef struct { int x, y, w, h; } Rc;
static bool inRc(Rc r, int px, int py) { return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h; }
static void button(Surf *s, Rc r, const char *label, bool active, bool enabled) {
    Col bg = !enabled ? DIS : active ? ACT : BTN;
    Col fg = !enabled ? DISTXT : active ? WHITE : TXT;
    rrect(s, r.x, r.y, r.w, r.h, 5, bg);
    text(s, r.x + (r.w - (int)strlen(label) * 6) / 2, r.y + (r.h - 10) / 2, label, fg);
}

static const u8 presets[8][3] = {
    {255,0,0},{0,255,0},{0,0,255},{255,255,0},{0,255,255},{255,0,255},{255,255,255},{255,110,0}
};
static const int tsecs[5] = {10, 30, 60, 300, 600};
static const char *tname[5] = {"10 SEC", "30 SEC", "1 MIN", "5 MIN", "10 MIN"};

int main(void) {
    gfxInitDefault();
    gfxSet3D(false);
    srvInit();
    ptmuInit();

    int col[3] = {0, 255, 0};
    bool autoOff = false, blink = false, shown = true, ledActive = false, dirty = false;
    int tIdx = 1, spd = 1;
    u64 endMs = 0;
    Result last = 0;
    bool anyApply = false, testLow = false;
    u64 nextBat = 0;

    Rc sl[3], sw[8];
    for (int j = 0; j < 3; j++) sl[j] = (Rc){30, 8 + j * 20, 260, 18};
    for (int i = 0; i < 8; i++) sw[i] = (Rc){8 + i * 38, 74, 34, 24};
    Rc bStay = {8, 106, 148, 24}, bAuto = {164, 106, 148, 24}, bTime = {8, 136, 304, 24};
    Rc bSteady = {8, 166, 96, 24}, bBlink = {112, 166, 96, 24}, bSpeed = {216, 166, 96, 24};
    Rc bApply = {8, 198, 148, 26}, bOff = {164, 198, 148, 26};

    while (aptMainLoop()) {
        hidScanInput();
        u32 d = hidKeysDown(), h = hidKeysHeld(), u = hidKeysUp();
        touchPosition t; hidTouchRead(&t);
        if (d & KEY_START) break;

        if (h & KEY_TOUCH) {
            for (int j = 0; j < 3; j++) if (inRc(sl[j], t.px, t.py)) {
                int v = (t.px - 40) * 255 / 240;
                col[j] = v < 0 ? 0 : v > 255 ? 255 : v;
                shown = true; dirty = true;
            }
        }
        if (d & KEY_TOUCH) {
            for (int i = 0; i < 8; i++) if (inRc(sw[i], t.px, t.py)) {
                for (int k = 0; k < 3; k++) col[k] = presets[i][k];
                shown = true; dirty = true;
            }
            if (inRc(bStay, t.px, t.py)) autoOff = false;
            if (inRc(bAuto, t.px, t.py)) autoOff = true;
            if (autoOff && inRc(bTime, t.px, t.py)) tIdx = (tIdx + 1) % 5;
            if (inRc(bSteady, t.px, t.py)) { blink = false; dirty = true; }
            if (inRc(bBlink, t.px, t.py))  { blink = true;  dirty = true; }
            if (inRc(bSpeed, t.px, t.py))  { spd = (spd + 1) % 3; dirty = true; }
            if (inRc(bApply, t.px, t.py)) {
                last = ledSet(col[0], col[1], col[2], true, blink, spd);
                anyApply = true; ledActive = true; shown = true; dirty = false;
                endMs = autoOff ? osGetTime() + (u64)tsecs[tIdx] * 1000 : 0;
            }
            if (inRc(bOff, t.px, t.py)) {
                last = ledSet(0, 0, 0, false, false, 0);
                anyApply = true; ledActive = false; shown = false; dirty = false; endMs = 0;
            }
        }
        // change colour/blink while the LED is on: re-send when the finger lifts
        if ((u & KEY_TOUCH) && dirty && ledActive) {
            last = ledSet(col[0], col[1], col[2], true, blink, spd);
            dirty = false;
        }
        // ----- low battery alert: overrides everything with a fast red blink -----
        if (d & KEY_SELECT) { testLow = !testLow; nextBat = 0; }   // SELECT = test the alert
        if (osGetTime() >= nextBat) {
            nextBat = osGetTime() + 5000;
            u8 lvl = 5, chg = 0;
            PTMU_GetBatteryLevel(&lvl);
            PTMU_GetBatteryChargeState(&chg);
            bool nowLow = testLow || (lvl <= 1 && !chg);
            if (nowLow && !lowBat) {
                lowBat = true;
                setLed(255, 0, 0, true, true, 2);
            } else if (!nowLow && lowBat) {
                lowBat = false;
                if (ledActive) setLed(col[0], col[1], col[2], true, blink, spd);
                else setLed(0, 0, 0, false, false, 0);
            }
        }
        // auto off
        if (!lowBat && ledActive && endMs && osGetTime() >= endMs) {
            last = ledSet(0, 0, 0, false, false, 0);
            ledActive = false; shown = false; endMs = 0;
        }

        // ----- draw top -----
        Surf top = {gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), 400};
        bool phase = !blink || ((osGetTime() / spdMs[spd]) % 2 == 0);
        bool lit = shown && phase;
        Col pc = {col[0], col[1], col[2]};
        if (lowBat) { lit = ((osGetTime() / 250) % 2 == 0); pc = (Col){255, 0, 0}; }
        drawPicture(&top, lit, pc);

        // ----- draw bottom -----
        Surf bot = {gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL), 320};
        rect(&bot, 0, 0, 320, 240, UIBG);
        const char *ln[3] = {"R", "G", "B"};
        for (int j = 0; j < 3; j++) {
            Col ch = {j == 0 ? 255 : 0, j == 1 ? 200 : 0, j == 2 ? 255 : 0};
            text(&bot, 12, sl[j].y + 4, ln[j], TXT);
            rect(&bot, 40, sl[j].y + 6, 240, 6, (Col){0xC8, 0xC8, 0xC8});
            rect(&bot, 40, sl[j].y + 6, col[j] * 240 / 255, 6, ch);
            rect(&bot, 36 + col[j] * 240 / 255, sl[j].y + 1, 8, 16, TXT);
            char v[8]; snprintf(v, sizeof v, "%3d", col[j]);
            text(&bot, 290, sl[j].y + 4, v, TXT);
        }
        for (int i = 0; i < 8; i++) {
            bool sel = col[0] == presets[i][0] && col[1] == presets[i][1] && col[2] == presets[i][2];
            if (sel) rrect(&bot, sw[i].x - 2, sw[i].y - 2, sw[i].w + 4, sw[i].h + 4, 5, TXT);
            rrect(&bot, sw[i].x, sw[i].y, sw[i].w, sw[i].h, 4, (Col){presets[i][0], presets[i][1], presets[i][2]});
        }
        button(&bot, bStay, "STAY ON", !autoOff, true);
        button(&bot, bAuto, "AUTO OFF", autoOff, true);
        char tl[40];
        if (autoOff && ledActive && endMs) {
            u64 now = osGetTime();
            snprintf(tl, sizeof tl, "OFF IN %d SEC", (int)((endMs > now ? endMs - now : 0) / 1000) + 1);
        } else snprintf(tl, sizeof tl, "OFF AFTER: %s", tname[tIdx]);
        button(&bot, bTime, tl, false, autoOff);
        button(&bot, bSteady, "STEADY", !blink, true);
        button(&bot, bBlink, "BLINK", blink, true);
        char sp[24]; snprintf(sp, sizeof sp, "SPEED: %s", spdName[spd]);
        button(&bot, bSpeed, sp, false, blink);
        button(&bot, bApply, "APPLY", false, true);
        button(&bot, bOff, "TURN OFF", false, true);

        char st[48];
        if (lowBat) snprintf(st, sizeof st, testLow ? "LOW BATTERY TEST (SELECT=STOP)" : "LOW BATTERY - RED BLINK");
        else if (!anyApply) snprintf(st, sizeof st, "READY - SELECT TESTS LOW BATTERY");
        else if (R_FAILED(last)) snprintf(st, sizeof st, "ERROR %08lX", (unsigned long)last);
        else if (autoOff && ledActive) snprintf(st, sizeof st, "KEEP THE APP OPEN FOR AUTO OFF");
        else snprintf(st, sizeof st, "OK");
        text(&bot, 8, 228, st, TXT);

        gfxFlushBuffers(); gfxSwapBuffers(); gspWaitForVBlank();
    }
    ptmuExit();
    srvExit();
    gfxExit();
    return 0;
}
