#include "dare.h"
#include <string.h>

/* the first seven are the required ones, in this order (the UART test reads them by index) */
const char *const DARES[] = {
    "SAUTER 10 FOIS",
    "FAIRE LA GRIMACE LA PLUS MOCHE DU MONDE",
    "FAIRE UNE PIROUETTE",
    "FAIRE 3 POMPES",
    "RACONTER UNE BLAGUE",
    "DEBARRASSER LA TABLE AU PROCHAIN REPAS",
    "REPETE : VISE CE CHIC SLIP CHARLIE",
    "IMITER UNE POULE PENDANT 10 SECONDES",
    "CHANTER UNE CHANSON LA BOUCHE FERMEE",
    "MARCHER COMME UN PINGOUIN",
    "DANSER COMME UN ROBOT 10 SECONDES",
    "PARLER COMME UN ROBOT JUSQU'AU PROCHAIN TOUR",
    "RECITER L'ALPHABET EN 15 SECONDES",
    "TENIR SUR UN PIED 15 SECONDES",
    "FAIRE UN COMPLIMENT A CHAQUE JOUEUR",
    "IMITER UN SINGE PENDANT 10 SECONDES",
    "FAIRE 5 FLEXIONS EN CHANTANT",
    "RIRE COMME UN MECHANT DE DESSIN ANIME",
    "MIMER UN ANIMAL, LES AUTRES DEVINENT",
    "FAIRE LA STATUE PENDANT 20 SECONDES",
    "REPETE 3 FOIS : UN CHASSEUR SACHANT CHASSER",
    "PARLER AVEC UN ACCENT RIGOLO 1 MINUTE",
    "FAIRE LE TOUR DE LA PIECE A CLOCHE-PIED",
    "IMITER TON ANIMAL PREFERE",
    "FAIRE UN CALIN A QUELQU'UN",
    "CHANTER JOYEUX ANNIVERSAIRE COMME A L'OPERA",
    "MARCHER A RECULONS JUSQU'A LA PORTE",
    "FAIRE 10 BONDS DE KANGOUROU",
    "FAIRE LA DANSE DU POULET",
    "TOUCHER TES ORTEILS 10 FOIS",
    "DIRE TON PRENOM A L'ENVERS",
    "FAIRE UN DEFILE DE MODE RIGOLO",
    "IMITER UN DINOSAURE QUI A PEUR D'UNE SOURIS",
    "FAIRE LE CHAT QUI RONRONNE 20 SECONDES",
    "RESTER SERIEUX PENDANT 30 SECONDES",
    "TOURNER 3 FOIS SUR TOI-MEME",
    "SIFFLER UNE CHANSON",
    "FAIRE UNE TETE DE POISSON 10 SECONDES",
    "IMITER LE BRUIT D'UNE MOTO",
    "MIMER UN METIER, LES AUTRES DEVINENT",
    "RANGER 5 OBJETS DANS LA MAISON",
    "DIRE BONJOUR DANS 3 LANGUES",
    "FAIRE 10 SAUTS EN ETOILE",
    "CHUCHOTER JUSQU'AU PROCHAIN TOUR",
    "HURLER COMME UN LOUP",
    "MARCHER COMME UN CRABE",
    "REPETE : LES CHAUSSETTES DE L'ARCHIDUCHESSE",
    "DESSINER UNE MAISON LES YEUX FERMES",
    "IMITER QUELQU'UN DE LA FAMILLE",
    "GARDER UN LIVRE SUR LA TETE 10 SECONDES",
    "INVENTER UN MOT ET DIRE CE QU'IL VEUT DIRE",
    "CHANTER LE REFRAIN DE TA CHANSON PREFEREE",
    "LOUCHER PENDANT 10 SECONDES",
    "BAILLER COMME UN LION",
    "FAIRE 5 ABDOS",
    "IMITER UN ELEPHANT AVEC TON BRAS",
    "RACONTER UNE HISTOIRE EN 3 PHRASES",
};
const int DARE_COUNT = sizeof(DARES) / sizeof(DARES[0]);
_Static_assert(sizeof(DARES) / sizeof(DARES[0]) <= 64, "the deck is a 64-bit mask");
_Static_assert(sizeof(DARES) / sizeof(DARES[0]) >= 47, "at least 47 dares");

void dare_start(dare_t *g, uint8_t first, bool keep_deck)
{
    uint64_t used = keep_deck ? g->used : 0;
    uint16_t rest = keep_deck ? g->rest % DARE_WEDGES : 0;
    memset(g, 0, sizeof(*g));
    game_round_start(&g->r, first);
    g->used = used;
    g->rest = rest;
    g->dare = g->pending = DARE_NONE;
}

int dare_left(const dare_t *g)
{
    int n = 0;
    for (int i = 0; i < DARE_COUNT; i++) {
        n += !(g->used >> i & 1);
    }
    return n;
}

int dare_pick(dare_t *g, uint32_t rnd)
{
    int left = dare_left(g);
    if (left == 0) {
        g->used = 0; /* every dare came up once: a full deck again */
        left = DARE_COUNT;
    }
    int k = (int)(rnd % (uint32_t)left);
    for (int i = 0; i < DARE_COUNT; i++) {
        if (!(g->used >> i & 1) && k-- == 0) {
            g->used |= 1ull << i;
            return i;
        }
    }
    return 0; /* not reached */
}

dare_result_t dare_spin(dare_t *g, uint32_t len_ms, uint16_t segs, uint32_t rnd)
{
    if (g->r.status != GAME_PLAYING) {
        return DARE_ERR_OVER;
    }
    if (g->dare != DARE_NONE) {
        return DARE_ERR_SHOWN;
    }
    if (dare_spinning(g)) {
        return DARE_ERR_SPINNING;
    }
    g->spin_len = len_ms > 0 ? len_ms : 1;
    g->spin_ms = 0;
    g->segs = segs;
    g->clicks = 0;
    g->pending = (int16_t)dare_pick(g, rnd);
    return DARE_OK;
}

/* the spin's turn so far in thousandths of a wedge: fast at first, slowing to a stop
 * (the speed falls linearly to zero, so the turn is segs * (1 - (1 - t/T)^2)) */
static uint32_t spun(const dare_t *g)
{
    if (!dare_spinning(g)) {
        return 0;
    }
    uint64_t t = g->spin_len, u = t - (g->spin_ms < g->spin_len ? g->spin_ms : g->spin_len);
    return (uint32_t)((uint64_t)g->segs * 1000 * (t * t - u * u) / (t * t));
}

uint32_t dare_wheel_pos(const dare_t *g) { return (uint32_t)g->rest * 1000 + spun(g); }

bool dare_spin_step(dare_t *g, uint32_t dt_ms, int *clicks)
{
    *clicks = 0;
    if (g->r.status != GAME_PLAYING || !dare_spinning(g)) {
        return false;
    }
    g->spin_ms = g->spin_ms + dt_ms < g->spin_len ? g->spin_ms + dt_ms : g->spin_len;
    /* a click as each border between two wedges passes the pointer (half a wedge on) */
    uint16_t passed = (uint16_t)((spun(g) + 500) / 1000);
    *clicks = passed - g->clicks;
    g->clicks = passed;
    if (g->spin_ms < g->spin_len) {
        return false;
    }
    g->rest = (uint16_t)((g->rest + g->segs) % DARE_WEDGES);
    g->spin_len = g->spin_ms = 0;
    g->dare = g->pending;
    g->pending = DARE_NONE;
    return true;
}

dare_result_t dare_pass(dare_t *g)
{
    if (g->r.status != GAME_PLAYING) {
        return DARE_ERR_OVER;
    }
    if (g->dare == DARE_NONE) {
        return DARE_ERR_NONE;
    }
    g->done[g->r.turn]++;
    g->r.turn = game_other(g->r.turn);
    g->r.moves++;
    g->dare = DARE_NONE;
    return DARE_OK;
}

#ifndef GAME_RULES_ONLY
/* ---- the game module: console, touch and drawing ---- */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "app.h"
#include "display.h"

_Static_assert(sizeof(dare_t) <= GAME_STATE_MAX, "dare_t does not fit in a game slot");

#define CX (SCREEN_W / 2)
#define WHEEL_CY 128
#define WHEEL_R 74

static void dare_new_round(void *st, uint8_t first) { dare_start(st, first, true); }

static void print_dare(const dare_t *g)
{
    app_out("DARE %c idx=%d left=%d text=%s", game_mark_char(g->r.turn), g->dare, dare_left(g), DARES[g->dare]);
}

static void dare_print(const void *st)
{
    const dare_t *g = st;
    app_out("DECK left=%d total=%d done=X%u,O%u", dare_left(g), DARE_COUNT, g->done[CELL_X], g->done[CELL_O]);
    if (g->dare != DARE_NONE) {
        print_dare(g);
    }
    app_out("TURN %c", game_mark_char(g->r.turn));
}

static void dare_report(const void *st)
{
    const dare_t *g = st;
    if (g->dare != DARE_NONE) {
        app_out("SHOWN %c idx=%d", game_mark_char(g->r.turn), g->dare);
    } else if (dare_spinning(g)) {
        app_out("WHEEL %c spin ms=%lu of=%lu clicks=%u", game_mark_char(g->r.turn), (unsigned long)g->spin_ms,
                (unsigned long)g->spin_len, g->clicks);
    } else {
        app_out("WHEEL %c rest", game_mark_char(g->r.turn));
    }
}

static game_event_t spin(dare_t *g)
{
    uint32_t len = DARE_SPIN_MIN_MS + game_random() % (DARE_SPIN_MAX_MS - DARE_SPIN_MIN_MS + 1);
    uint16_t segs = (uint16_t)(DARE_SPIN_MIN_WEDGES + game_random() % (2 * DARE_WEDGES));
    switch (dare_spin(g, len, segs, game_random())) {
    case DARE_OK:
        app_out("SPIN %c ms=%lu segs=%u", game_mark_char(g->r.turn), (unsigned long)len, segs);
        return GEV_CLICK;
    case DARE_ERR_SHOWN:
        app_out("ERR dare shown (pass first)");
        break;
    case DARE_ERR_SPINNING:
        app_out("ERR spinning");
        break;
    default:
        app_out("ERR game over");
        break;
    }
    return GEV_BAD;
}

static game_event_t pass(dare_t *g)
{
    uint8_t p = g->r.turn;
    switch (dare_pass(g)) {
    case DARE_OK:
        app_out("PASS %c", game_mark_char(p));
        app_out("TURN %c", game_mark_char(g->r.turn));
        return p == CELL_X ? GEV_MOVE_X : GEV_MOVE_O;
    case DARE_ERR_NONE:
        app_out("ERR no dare (spin first)");
        break;
    default:
        app_out("ERR game over");
        break;
    }
    return GEV_BAD;
}

/* a tap (or RIGHT) spins the wheel, or hands the board on once the dare is shown;
 * over the console "pass" only hands it on */
static game_event_t dare_move(void *st, int m, const char *src)
{
    dare_t *g = st;
    (void)m;
    if (strcmp(src, "uart") == 0 || g->dare != DARE_NONE || g->r.status != GAME_PLAYING) {
        return pass(g);
    }
    if (dare_spinning(g)) {
        return GEV_NONE; /* the wheel is turning: wait for it */
    }
    return spin(g);
}

/* dare get <idx> | dare count | dare (the state) | spin | pass */
static game_event_t dare_command(void *st, const char *arg)
{
    char word[8] = "";
    long idx = -1;
    int n = sscanf(arg, "%7s %ld", word, &idx);
    if (n <= 0) {
        dare_print(st);
        return GEV_NONE;
    }
    if (strcmp(word, "pass") == 0 && n == 1) {
        return dare_move(st, 0, "uart");
    }
    if (strcmp(word, "spin") == 0 && n == 1) {
        return spin(st);
    }
    if (strcmp(word, "count") == 0 && n == 1) {
        app_out("DARES n=%d", DARE_COUNT);
        return GEV_NONE;
    }
    if (strcmp(word, "get") == 0 && n == 2) {
        if (idx < 0 || idx >= DARE_COUNT) {
            app_out("ERR range 0-%d", DARE_COUNT - 1);
            return GEV_BAD;
        }
        app_out("DARE idx=%ld text=%s", idx, DARES[idx]);
        return GEV_NONE;
    }
    app_out("ERR dare (get <idx> | count)");
    return GEV_BAD;
}

/* the clock turns the wheel; shaking and tilting do nothing here */
static game_event_t dare_step(void *st, const motion_t *m)
{
    dare_t *g = st;
    int clicks;
    if (dare_spin_step(g, m->dt_ms, &clicks)) {
        print_dare(g);
        return GEV_DARE;
    }
    /* the clicks are only a sound: a simulated spin (whose steps stop at an event) stays silent */
    return clicks > 0 && !m->sim ? GEV_CLICK : GEV_NONE;
}

/* "sim cycle": draws a whole deck from a fresh one (on a copy) and counts the repeats */
static bool dare_sim_set(void *st, const char *key, long val)
{
    (void)val;
    if (strcmp(key, "cycle") != 0) {
        return false;
    }
    dare_t c = *(const dare_t *)st;
    c.used = 0;
    uint64_t seen = 0;
    int unique = 0;
    for (int i = 0; i < DARE_COUNT; i++) {
        int k = dare_pick(&c, game_random());
        if (!(seen >> k & 1)) {
            unique++;
        }
        seen |= 1ull << k;
    }
    app_out("CYCLE draws=%d unique=%d total=%d", DARE_COUNT, unique, DARE_COUNT);
    return true;
}

static int dare_hit(int x, int y)
{
    (void)x;
    (void)y;
    return 0; /* a tap anywhere spins or hands the board on */
}

static uint16_t mark_col(uint8_t mark) { return mark == CELL_X ? COL_X : COL_O; }

#define WRAP_MAX 6

/* greedy word wrap of s into lines of at most w characters: the line count, or -1 when
 * it needs more than WRAP_MAX lines or a word is longer than w */
static int wrap(const char *s, int w, int start[], int len[])
{
    int n = 0;
    const char *p = s;
    while (*p) {
        while (*p == ' ') {
            p++;
        }
        if (!*p) {
            break;
        }
        if (n == WRAP_MAX) {
            return -1;
        }
        const char *line = p, *end = p; /* end: after the last word that fits */
        while (*p) {
            const char *we = p;
            while (*we && *we != ' ') {
                we++;
            }
            if (we - line > w) {
                break;
            }
            end = we;
            p = we;
            while (*p == ' ') {
                p++;
            }
        }
        if (end == line) {
            return -1; /* one word wider than a line */
        }
        start[n] = (int)(line - s);
        len[n] = (int)(end - line);
        n++;
        p = end;
    }
    return n;
}

/* the dare, as large as fits, centred on cy */
static void big_text(int cy, const char *s, uint16_t c)
{
    static const int SCALES[] = {3, 2, 1};
    int start[WRAP_MAX], len[WRAP_MAX];
    for (int i = 0; i < 3; i++) {
        int sc = SCALES[i], w = (SCREEN_W - 12 + sc) / (6 * sc), max_lines = sc == 3 ? 4 : WRAP_MAX;
        int n = wrap(s, w, start, len);
        if (n < 0 || n > max_lines) {
            continue;
        }
        int lh = 7 * sc + 4 * sc, y = cy - (n * lh - 4 * sc) / 2;
        gfx_text_note(s); /* the whole dare, once, for the "text" console command */
        gfx_text_mute(true);
        for (int k = 0; k < n; k++, y += lh) {
            char buf[48];
            int l = len[k] < (int)sizeof(buf) - 1 ? len[k] : (int)sizeof(buf) - 1;
            memcpy(buf, s + start[k], l);
            buf[l] = '\0';
            gfx_text_mid(CX, y, buf, sc, c);
        }
        gfx_text_mute(false);
        return;
    }
    gfx_text_mid(CX, cy, s, 1, c);
}

static const uint16_t WEDGE_COLS[] = {
    RGB(235, 64, 64), RGB(255, 160, 0), RGB(240, 214, 40), RGB(56, 190, 90), RGB(64, 150, 255), RGB(160, 90, 235),
};

/* the wheel turned by pos thousandths of a wedge (clockwise), its pointer on top */
static void draw_wheel(float cx, float cy, float r, uint32_t pos, uint16_t hub)
{
    float per = 360.0f / DARE_WEDGES;
    float turn = (float)(pos % (DARE_WEDGES * 1000)) * per / 1000.0f;
    /* wedge k is centred under the pointer (-90 degrees) when the wheel stands at a whole wedge */
    gfx_wheel(cx, cy, r, DARE_WEDGES, -90 - per / 2 + turn, WEDGE_COLS,
              sizeof(WEDGE_COLS) / sizeof(WEDGE_COLS[0]));
    gfx_ring(cx, cy, r + 2, 4, COL_TEXT);
    /* a light on each border between two wedges */
    for (int k = 0; k < DARE_WEDGES; k++) {
        float a = (-90 - per / 2 + turn + k * per) * (float)M_PI / 180.0f;
        gfx_disc(cx + cosf(a) * (r - 6), cy + sinf(a) * (r - 6), 2.5f, COL_TEXT);
    }
    gfx_disc(cx, cy, r * 0.22f, COL_BG);
    gfx_ring(cx, cy, r * 0.22f, 2, hub);
    gfx_text_mid((int)cx, (int)(cy - 7), "?", 2, hub);
    /* the pointer: a triangle above the wheel, pointing down at the rim */
    float ty = cy - r - 12;
    for (int i = 0; i < 6; i++) {
        float w = 9 - i * 1.6f;
        gfx_line(cx - w, ty + i * 2.4f, cx + w, ty + i * 2.4f, 2.6f, COL_TEXT);
    }
}

static void dare_draw(const void *st, int preview)
{
    (void)preview;
    const dare_t *g = st;
    uint8_t p = g->r.turn;
    uint16_t pc = mark_col(p);
    char b[48];

    /* the corners count the dares done instead of the (unused) wins */
    gfx_rect(0, 0, 54, 36, COL_BG);
    gfx_rect(186, 0, 54, 36, COL_BG);
    snprintf(b, sizeof(b), "A %u", g->done[CELL_X]);
    gfx_text(8, 13, b, 2, COL_X);
    snprintf(b, sizeof(b), "B %u", g->done[CELL_O]);
    gfx_text_right(232, 13, b, 2, COL_O);

    if (g->dare == DARE_NONE) {
        draw_wheel(CX, WHEEL_CY, WHEEL_R, dare_wheel_pos(g), pc);
        if (dare_spinning(g)) {
            gfx_text_mid(CX, 216, "LA ROUE TOURNE...", 1, COL_DIM);
        } else {
            snprintf(b, sizeof(b), "JOUEUR %c : TOUCHE", game_player_char(p));
            gfx_text_mid(CX, 210, b, 1, pc);
            gfx_text_mid(CX, 224, "POUR FAIRE TOURNER LA ROUE", 1, COL_TEXT);
        }
    } else {
        snprintf(b, sizeof(b), "DEFI POUR JOUEUR %c :", game_player_char(p));
        gfx_text_mid(CX, 46, b, 1, pc);
        big_text(124, DARES[g->dare], COL_ACCENT);
        snprintf(b, sizeof(b), "TOUCHE : AU TOUR DE JOUEUR %c", game_player_char(game_other(p)));
        gfx_text_mid(CX, 222, b, 1, COL_DIM);
    }
}

static void dare_banner(const void *st, char *buf, int n, uint8_t *mark)
{
    const dare_t *g = st;
    *mark = g->r.turn;
    snprintf(buf, n, g->dare == DARE_NONE ? "TOUR DE %c" : "DEFI DE %c", game_player_char(g->r.turn));
}

static void dare_icon(int x, int y, int s)
{
    gfx_rrect(x, y, s, s, 6, COL_LINE);
    float cx = x + s / 2.0f, cy = y + s / 2.0f + s * 0.05f, r = s * 0.34f;
    gfx_wheel(cx, cy, r, 6, -120, WEDGE_COLS, sizeof(WEDGE_COLS) / sizeof(WEDGE_COLS[0]));
    gfx_disc(cx, cy, r * 0.25f, COL_TEXT);
    gfx_line(cx - s * 0.08f, cy - r - s * 0.1f, cx + s * 0.08f, cy - r - s * 0.1f, 2, COL_TEXT);
    gfx_line(cx, cy - r - s * 0.1f, cx, cy - r + s * 0.02f, 2, COL_TEXT);
}

static const char *const MORE_VERBS[] = {"pass", "spin", NULL};

static const char *const RULES[] = {
    "CHACUN SON TOUR : TOUCHE L'ECRAN",
    "POUR FAIRE TOURNER LA ROUE.",
    "ELLE RALENTIT ET S'ARRETE :",
    "UN DEFI S'AFFICHE.",
    "",
    "RELEVE LE DEFI, PUIS TOUCHE",
    "L'ECRAN OU DROITE : C'EST AU",
    "TOUR DE L'AUTRE JOUEUR.",
    "",
    "UN DEFI NE REVIENT PAS AVANT",
    "QUE TOUS SOIENT SORTIS.",
    NULL,
};
_Static_assert(DARE_SPIN_MIN_MS == 3000 && DARE_SPIN_MAX_MS == 5000 && DARE_WEDGES == 12, "update rules_uart");

const game_def_t ROUE_DES_DEFIS = {
    .id = "dare",
    .title = "ROUE DES DEFIS",
    .verb = "dare",
    .more_verbs = MORE_VERBS,
    .new_round = dare_new_round,
    .print = dare_print,
    .move = dare_move,
    .command = dare_command,
    .hit = dare_hit,
    .draw = dare_draw,
    .icon = dare_icon,
    .step = dare_step,
    .report = dare_report,
    .sim_set = dare_sim_set,
    .rules = RULES,
    .rules_uart = "spin=3-5s wedges=12 dare=random repeat=never-before-all pass=tap|right",
    .banner = dare_banner,
    .right_taps = true,
};
#endif
