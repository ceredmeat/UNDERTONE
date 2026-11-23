/* SPDX-License-Identifier: GPL-2.0-or-later */
/* UNDERTONE: a subharmonic machine for the Digitakt MKI (core 4.4 + machine-pages, OS 1.54), inspired by
 * the way the Moog Subharmonicon builds sound (two oscillators, each with two subharmonics: the
 * oscillator's pitch divided by 1-16) and by the Labyrinth's wavefolder.
 *
 * Page 1 (A-H): TUNE  WAVE  SUB1  SUB2  INTV  SUB3  SUB4  LEV
 *   WAVE saw to square for all six tones; SUB1/SUB2 divide osc 1, SUB3/SUB4 divide osc 2 (1-16);
 *   INTV is osc 2's interval from osc 1, -24..+24 semitones.
 * Page 2:       FINE  OSCL  SUBL  FOLD  QNT   DRFT  WAVE  VIEW  (VIEW's ladder fills G and H)
 *   FINE osc 2's detune; OSCL and SUBL the levels of the two oscillators and of the four subs; FOLD a
 *   wavefolder; QNT how INTV is tuned: FREE (with FINE), ET (equal temperament) or JI (just
 *   intonation, pure ratios); DRFT a slow analog drift of the six pitches.
 * Machine 35. */
#include "digitakt-mk1/core3.h"

#define UT_ID 35
enum { K_TUNE, K_WAVE, K_SUB1, K_SUB2, K_INTV, K_SUB3, K_SUB4, K_LEV,
       K_FINE, K_OSCL, K_SUBL, K_FOLD, K_QNT, K_DRFT, K_VIEW };

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static int32_t div_of(int32_t v) { return 1 + clampi(v * 16 / 128, 0, 15); }          /* 1..16 */
static int32_t intv_of(int32_t v) { return clampi((v - 64) * 24 / 63, -24, 24); }    /* semitones */
static int32_t qnt_of(int32_t v) { return clampi(v * 3 / 128, 0, 2); }                /* FREE ET JI */

/* just intonation within the octave, semitone n -> num/den */
static const uint8_t ji_num[12] = { 1, 16, 9, 6, 5, 4, 45, 3, 8, 5, 9, 15 };
static const uint8_t ji_den[12] = { 1, 15, 8, 5, 4, 3, 32, 2, 5, 3, 5, 8 };

/* ---- the sound (ev_render_voices: interrupt level, 1500 blocks a second) ---------------------------- */

static struct ut {
    uint32_t ph[6];                 /* osc 1, osc 2, sub 1-4 */
    int32_t drift[6], dtarget[6];   /* cents x 16 */
    uint32_t noise;
} ut[8];

static uint32_t inc_for(int32_t pitch)                                  /* 16.16 semitones */
{
    pitch = clampi(pitch, 0, 87 << 16);
    return (23410327u >> 13) * (fw_pitch_tab[pitch / 384] >> 16);
}
static int32_t knob(int t, int k) { return clampi(cm_src_value(t, k) >> 8, 0, 127); }

/* triangle folding: whatever passes +-1.0 (Q15) is reflected back, again and again */
static inline int32_t fold(int32_t x)
{
    uint32_t u = (uint32_t)(x + 32768) & 131071u;                       /* 0 .. 4.0 */
    int32_t v = (int32_t)u;
    if (v > 65536)
        v = 131072 - v;
    return v - 32768;
}

void ut_render(int32_t *blocks)
{
    for (int t = 0; t < 8; t++) {
        if (fw_render_machine[t] != UT_ID)
            continue;
        struct ut *s = &ut[t];
        int32_t *out = &blocks[32 * t];
        const volatile int16_t *p = &fw_voice_params[53 * t];
        int start = (fw_voice_start >> t) & 1;
        int32_t al = fw_amp_level[2 * t + 1];
        if (!start && fw_amp_level[2 * t] == FW_AMP_DECAY && al > -(1 << 15) && al < (1 << 15)) {
            for (int i = 0; i < 32; i++)
                out[i] = 0;
            continue;
        }
        if (start)
            for (int i = 0; i < 6; i++)
                s->ph[i] = (uint32_t)i * 0x2aaaaaaau;                   /* spread, as free-running VCOs */
        if (!s->noise)
            s->noise = 0x9e3779b9u + (uint32_t)t;

        /* the analog drift: each pitch wanders towards a new random target, slowly */
        int32_t drft = knob(t, K_DRFT);
        for (int i = 0; i < 6; i++) {
            if (drft) {
                uint32_t x = s->noise;
                x ^= x << 13; x ^= x >> 17; x ^= x << 5;
                s->noise = x;
                if ((x & 255) == 0)                                     /* a new target now and then */
                    s->dtarget[i] = ((int32_t)(x >> 20) - 2048) * drft / 1024;  /* up to +-16 cents (x16) */
                s->drift[i] += (s->dtarget[i] - s->drift[i]) >> 6;
            } else
                s->drift[i] = 0;
        }

        /* pitches: osc 1 from the note and TUNE; osc 2 INTV away (FREE + FINE, ET, or JI) */
        int32_t pitch = ((int32_t)p[K_TUNE] - 0x4000) * 256 + fw_voice_note[t] + (3 << 16);
        int32_t iv = intv_of(knob(t, K_INTV)), q = qnt_of(knob(t, K_QNT));
        uint32_t i1 = inc_for(pitch + s->drift[0] * 41), i2;                 /* 1/16 cent = 41 */
        if (q == 2) {                                                   /* JI: osc 1 x a pure ratio */
            int32_t oct = iv >= 0 ? iv / 12 : -((-iv + 11) / 12), n = iv - oct * 12;
            uint32_t base = inc_for(pitch + oct * (12 << 16) + s->drift[1] * 41);
            i2 = (base / ji_den[n]) * ji_num[n];
        } else {
            int32_t fine = q == 0 ? (knob(t, K_FINE) - 64) * 65536 / 128 : 0;   /* +-50 cents */
            i2 = inc_for(pitch + (iv << 16) + fine + s->drift[1] * 41);
        }
        uint32_t inc[6] = { i1, i2,
                            i1 / (uint32_t)div_of(knob(t, K_SUB1)), i1 / (uint32_t)div_of(knob(t, K_SUB2)),
                            i2 / (uint32_t)div_of(knob(t, K_SUB3)), i2 / (uint32_t)div_of(knob(t, K_SUB4)) };
        for (int i = 2; i < 6; i++)                                     /* the subs drift too: 1/16 cent */
            inc[i] += (uint32_t)((int32_t)(inc[i] >> 15) * s->drift[i] * 19 / 16);   /* = (inc >> 15) x 1.18 */

        int32_t wave = knob(t, K_WAVE), oscl = knob(t, K_OSCL), subl = knob(t, K_SUBL);
        int32_t fg = 256 + knob(t, K_FOLD) * 24;                        /* fold gain, 8.8: x1 .. x13 */
        int32_t vel = clampi(fw_voice_vel[t] >> 8, 0, 127), vgain = 64 + (vel >> 1);
        int32_t lev = (p[K_LEV] >> 8) & 0x7f, gain = (lev * lev * vgain) >> 7;   /* Q14 */
        uint32_t a = s->ph[0], b = s->ph[1], c = s->ph[2], d = s->ph[3], e = s->ph[4], f = s->ph[5];

        for (int i = 0; i < 32; i++) {
            /* saws of the two oscillators and of the four subs, then the same as squares */
            int32_t so = ((int32_t)(a >> 17) + (int32_t)(b >> 17)) - 32768;            /* +-1.0 */
            int32_t ss = ((int32_t)(c >> 18) + (int32_t)(d >> 18) + (int32_t)(e >> 18) + (int32_t)(f >> 18)) - 32768;
            int32_t qo = ((int32_t)(a >> 31) + (int32_t)(b >> 31)) * -32767 + 32767;   /* 2 squares */
            int32_t qs = (((int32_t)(c >> 31) + (int32_t)(d >> 31) + (int32_t)(e >> 31) +
                           (int32_t)(f >> 31)) * -16383) + 32767;                      /* 4 squares */
            int32_t vo = so + (((qo - so) * wave) >> 7);
            int32_t vs = ss + (((qs - ss) * wave) >> 7);
            int32_t v = (vo * oscl + vs * subl) >> 7;                   /* two buses: past 1.0 folds */
            v = fold((v * fg) >> 8);
            out[i] = (v * gain) << 2;                                   /* Q15 x Q14 -> Q31 */
            a += inc[0]; b += inc[1]; c += inc[2]; d += inc[3]; e += inc[4]; f += inc[5];
        }
        s->ph[0] = a; s->ph[1] = b; s->ph[2] = c; s->ph[3] = d; s->ph[4] = e; s->ph[5] = f;
    }
}

/* ---- the page: value texts --------------------------------------------------------------------------- */

/* Everything from here on is the screen: built for size, to leave RAM for other mods. The sound code
 * above is untouched. */
#pragma GCC optimize ("Os", "no-tree-loop-distribute-patterns", "no-optimize-strlen")
static int32_t put(char *buf, const char *s)
{
    int i = 0;
    while ((buf[i] = s[i]))
        i++;
    return 1;
}
static int32_t put_num(char *buf, const char *prefix, int32_t v, const char *suffix)
{
    char d[8];
    int n = 0, i = 0;
    while (*prefix)
        buf[i++] = *prefix++;
    if (v < 0) {
        buf[i++] = '-';
        v = -v;
    }
    do {
        d[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        buf[i++] = d[--n];
    while (*suffix)
        buf[i++] = *suffix++;
    buf[i] = 0;
    return 1;
}
static int32_t v127(int32_t value) { return clampi(value >> 8, 0, 127); }
static int32_t shown(int k, int32_t fallback)
{
    int32_t v = cm_src_shown(k);
    return v == CM_NO_VALUE ? fallback : v127(v);
}
static int32_t div_text(char *buf, int32_t value, int32_t ctx, int32_t m)
{
    (void)m;
    return put_num(buf, ctx ? "pitch / " : "/", div_of(v127(value)), "");
}
static int32_t intv_text(char *buf, int32_t value, int32_t ctx, int32_t m)
{
    int32_t iv = intv_of(v127(value));
    (void)m;
    return put_num(buf, iv > 0 ? "+" : "", iv, ctx ? " semitones" : "");
}
static int32_t fine_text(char *buf, int32_t value, int32_t ctx, int32_t m)
{
    int32_t c = (v127(value) - 64) * 50 / 64;
    (void)m;
    return put_num(buf, c > 0 ? "+" : "", c, ctx ? " cents" : "");
}
static int32_t qnt_text(char *buf, int32_t value, int32_t ctx, int32_t m)
{
    static const char *const n[3] = { "FREE", "ET", "JI" };
    (void)ctx; (void)m;
    return put(buf, n[qnt_of(v127(value))]);
}
static int32_t view_text(char *buf, int32_t value, int32_t ctx, int32_t m)
{
    (void)m;
    return put(buf, v127(value) < 64 ? (ctx ? "Pitch ladder" : "LADR") : (ctx ? "Waveform" : "WAVE"));
}

/* ---- the page: graphics (UI task; y = 0 is the bottom row) ------------------------------------------- */

static void dot(void *bmp, int32_t x, int32_t y) { fw_fillrect(bmp, x, y, x, y, 1); }

/* log2(n) x 64 for n = 1..16: where a subharmonic sits below its oscillator */
static const uint16_t log2_64[17] = { 0, 0, 64, 101, 128, 149, 165, 180, 192, 203, 213, 221, 229, 237,
                                     244, 250, 256 };

/* The waveform: saw to square, w columns */
static void draw_wave_shape(void *bmp, int32_t x, int32_t y, int32_t w, int32_t h, int32_t wave)
{
    int32_t prev = -1;
    for (int32_t c = 0; c < w; c++) {
        int32_t ph = c * 256 / w;                                       /* 0..255, two cycles */
        int32_t pc = (ph * 2) & 255;
        int32_t saw = pc - 128, sq = pc < 128 ? 127 : -127;
        int32_t v = saw + (((sq - saw) * wave) >> 7);
        int32_t r = (v + 128) * (h - 1) / 255;
        int32_t lo = prev < 0 ? r : prev < r ? prev : r, hi = prev < 0 ? r : prev > r ? prev : r;
        fw_fillrect(bmp, x + c, y + lo, x + c, y + hi, 1);
        prev = r;
    }
}

/* The ladder: one for each oscillator, side by side. The top rung is the oscillator; each subharmonic
 * is a rung below it, as far down as it sounds (one octave per four rows: /2 four rows, /16 sixteen).
 * A rung's width is its level. */
static void one_ladder(void *bmp, int32_t cx, int32_t top, int32_t oscw, int32_t subw, int32_t d1, int32_t d2)
{
    fw_fillrect(bmp, cx - oscw, top, cx + oscw, top, 1);                /* the oscillator */
    fw_fillrect(bmp, cx - oscw, top - 1, cx + oscw, top - 1, 1);
    for (int32_t k = 0; k < 2; k++) {
        int32_t y = top - 1 - log2_64[k ? d2 : d1] / 16;               /* 4 rows an octave */
        fw_fillrect(bmp, cx - subw, y, cx + subw, y, 1);
    }
}

static void draw_ladder(void *bmp, int32_t x0, int32_t x1, int32_t y)
{
    int32_t q = (x1 - x0) / 4, top = y + 16;
    int32_t oscw = 1 + shown(K_OSCL, 100) * 5 / 127, subw = shown(K_SUBL, 90) * 5 / 127;
    one_ladder(bmp, x0 + q, top, oscw, subw, div_of(shown(K_SUB1, 8)), div_of(shown(K_SUB2, 16)));
    one_ladder(bmp, x0 + 3 * q, top, oscw, subw, div_of(shown(K_SUB3, 24)), div_of(shown(K_SUB4, 40)));
}

static int32_t g_x = -1, h_x = -1;                                     /* WAVE (page 2) and VIEW */

static void draw_panel(void *bmp, int32_t y, int32_t view)
{
    int32_t x0 = (g_x >= 0 ? g_x : h_x - 16) + 1, x1 = (h_x >= 0 ? h_x : g_x + 16) + 16;
    fw_fillrect(bmp, x0, y, x1, y + 16, 0);
    if (view < 64)
        draw_ladder(bmp, x0, x1, y);
    else
        draw_wave_shape(bmp, x0, y + 1, x1 - x0 + 1, 15, shown(K_WAVE, 0));
}

/* WAVE: page 1 (knob B) its shape; page 2 (knob G) its half of VIEW's panel */
static int32_t draw_wave(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    (void)flag;
    if (x >= 88) {
        g_x = x;
        draw_panel(bmp, y, shown(K_VIEW, 0));
        return 1;
    }
    draw_wave_shape(bmp, x + 1, y + 1, 16, 15, v127(value));
    return 1;
}
static int32_t draw_view(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    (void)flag;
    h_x = x;
    draw_panel(bmp, y, v127(value));
    return 1;
}

static int32_t num_text(char *buf, int32_t value, int32_t ctx, int32_t m)    /* 0-127, as a number */
{
    int32_t v = (value >> 8) < 0 ? 0 : (value >> 8) > 127 ? 127 : (value >> 8), i = 0;
    char d[4];
    int n = 0;
    (void)ctx; (void)m;
    do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) buf[i++] = d[--n];
    buf[i] = 0;
    return 1;
}

/* ---- the page ------------------------------------------------------------------------------------------ */

#define KNOB(nm, ln, d, f) { .name = nm, .lname = ln, .look = 0x86, .flags = CM_RANGE | CM_DEFAULT, \
                             .max = 0x7f00, .def = (d) << 8, .fmt = f }
#define DRAWN(nm, ln, d, f, dr) { .name = nm, .lname = ln, .look = 0x86, \
                                  .flags = CM_RANGE | CM_DEFAULT | CM_DRAW, .max = 0x7f00, .def = (d) << 8, \
                                  .fmt = f, .draw = dr }

static const struct cm_src_page ut_pages[2] = {
    { "TONES", { 0, 1, 2, 3, 4, 5, 6, 7 }, 0, { 0, 0, 0 } },
    { "SHAPE", { 8, 9, 10, 11, 12, 13, 1, 14 }, 0, { 0, 0, 0 } },
};

static const struct cm_ui32 ut_page = {
    .base = {
        .abi = cm_ui_v32,
        .page_from = 3,
        .group = "UNDERTONE",
        .knob = {
            [K_WAVE] = DRAWN("WAVE", "Saw to square", 0, num_text, draw_wave),
            [K_SUB1] = KNOB("SUB1", "Osc 1 sub 1", 8, div_text),              /* /2 */
            [K_SUB2] = { .name = "SUB2", .lname = "Osc 1 sub 2", .look = 0x86,
                         .flags = CM_RANGE | CM_DEFAULT | CM_NOT_SAMPLE, .max = 0x7f00, .def = 16 << 8,
                         .fmt = div_text },                                   /* /3 */
            [K_INTV] = KNOB("INTV", "Osc 2 interval", 83, intv_text),         /* +7 */
            [K_SUB3] = KNOB("SUB3", "Osc 2 sub 1", 24, div_text),             /* /4 */
            [K_SUB4] = KNOB("SUB4", "Osc 2 sub 2", 40, div_text),             /* /6 */
        },
    },
    .extra = {
        KNOB("FINE", "Osc 2 fine tune", 64, fine_text),
        KNOB("OSCL", "Oscillator level", 100, num_text),
        KNOB("SUBL", "Subharmonic level", 90, num_text),
        KNOB("FOLD", "Wavefolder", 0, num_text),
        KNOB("QNT", "Interval tuning", 64, qnt_text),
        KNOB("DRFT", "Analog drift", 20, num_text),
        DRAWN("VIEW", "Ladder or waveform", 0, view_text, draw_view),
    },
    .page_count = 2,
    .pages = ut_pages,
};

/* The menu's 11 x 7 icon (top row bit 25): a pitch ladder, tall then shorter. */
static const uint32_t ut_px[11] = {
    0xfe000000, 0x00000000, 0xf8000000, 0x00000000, 0xe0000000, 0x00000000,
    0xfc000000, 0x00000000, 0xf0000000, 0x00000000, 0xc0000000,
};
static const uint32_t ut_mask[11] = {
    0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000,
    0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000,
};
static const struct {
    const void *vt;
    int32_t width, height, one;
    const uint32_t *px, *mask;
    int32_t zero;
} ut_icon = { fw_bitmap_vt, 11, 7, 1, ut_px, ut_mask, 0 };

const struct cm_machine ut_machine = {
    UT_ID, "UNDERTONE", "UNDR", &ut_icon, 3, UT_ID, CM_UI_TAG, (const struct cm_ui *)&ut_page,
};
