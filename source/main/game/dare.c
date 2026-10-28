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

void dare_start(dare_t *g, uint8_t first, uint32_t need_ms, bool keep_deck)
{
    uint64_t used = keep_deck ? g->used : 0;
    memset(g, 0, sizeof(*g));
    game_round_start(&g->r, first);
    g->used = used;
    g->need_ms = need_ms;
    g->dare = DARE_NONE;
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

bool dare_shake(dare_t *g, uint32_t dt_ms, uint32_t rnd)
{
    if (g->r.status != GAME_PLAYING || g->dare != DARE_NONE) {
        return false;
    }
    g->shake_ms += dt_ms;
    g->anim_ms += dt_ms;
    if (g->shake_ms < g->need_ms) {
        return false;
    }
    g->dare = (int16_t)dare_pick(g, rnd);
    return true;
}

bool dare_pass(dare_t *g, uint32_t need_ms)
{
    if (g->r.status != GAME_PLAYING || g->dare == DARE_NONE) {
        return false;
    }
    g->done[g->r.turn]++;
    g->r.turn = game_other(g->r.turn);
    g->r.moves++;
    g->dare = DARE_NONE;
    g->shake_ms = 0;
    g->need_ms = need_ms;
    return true;
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

static uint32_t random_need(void)
{
    return DARE_NEED_MIN_MS + game_random() % (DARE_NEED_MAX_MS - DARE_NEED_MIN_MS + 1);
}

static void dare_new_round(void *st, uint8_t first) { dare_start(st, first, random_need(), true); }

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
    if (g->dare == DARE_NONE) {
        app_out("SHAKE %c ms=%lu need=%lu", game_mark_char(g->r.turn), (unsigned long)g->shake_ms,
                (unsigned long)g->need_ms);
    } else {
        app_out("SHOWN %c idx=%d", game_mark_char(g->r.turn), g->dare);
    }
}

/* the only move: a tap (or RIGHT) after the dare hands the board on */
static game_event_t dare_move(void *st, int m, const char *src)
{
    dare_t *g = st;
    (void)m;
    uint8_t p = g->r.turn;
    if (g->r.status != GAME_PLAYING) {
        app_out("ERR game over");
        return GEV_BAD;
    }
    if (!dare_pass(g, random_need())) {
        app_out("ERR no dare (shake first)");
        /* a stray tap while shaking the board is no mistake worth a buzz */
        return strcmp(src, "uart") == 0 ? GEV_BAD : GEV_NONE;
    }
    app_out("PASS %c", game_mark_char(p));
    app_out("TURN %c", game_mark_char(g->r.turn));
    return p == CELL_X ? GEV_MOVE_X : GEV_MOVE_O;
}

/* dare get <idx> | dare count | dare (the state) | pass */
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

static game_event_t dare_step(void *st, const motion_t *m)
{
    dare_t *g = st;
    if (g->r.status != GAME_PLAYING) {
        return GEV_NONE;
    }
    g->shaking = m->shaking && g->dare == DARE_NONE;
    if (g->dare != DARE_NONE) {
        if (m->sim && (m->shaking || m->shake)) {
            app_out("ERR dare shown (pass first)");
            return GEV_BAD;
        }
        return GEV_NONE;
    }
    if (m->shaking && dare_shake(g, m->dt_ms, game_random())) {
        print_dare(g);
        return GEV_DARE;
    }
    return GEV_NONE;
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
    return 0; /* a tap anywhere hands the board on */
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

/* the board being shaken: a small console that wobbles while it is shaken */
static void draw_console(float cx, float cy, float s, uint16_t c)
{
    gfx_rrect((int)(cx - s * 0.7f), (int)(cy - s), (int)(s * 1.4f), (int)(s * 2), (int)(s * 0.2f), c);
    gfx_rrect((int)(cx - s * 0.5f), (int)(cy - s * 0.75f), (int)s, (int)s, 3, COL_DIM);
    gfx_disc(cx, cy + s * 0.6f, s * 0.15f, c);
}

static void dare_draw(const void *st, int preview)
{
    (void)preview;
    const dare_t *g = st;
    uint8_t p = g->r.turn;
    uint16_t pc = mark_col(p);
    char b[40];

    /* the corners count the dares done instead of the (unused) wins */
    gfx_rect(0, 0, 54, 36, COL_BG);
    gfx_rect(186, 0, 54, 36, COL_BG);
    snprintf(b, sizeof(b), "X %u", g->done[CELL_X]);
    gfx_text(8, 13, b, 2, COL_X);
    snprintf(b, sizeof(b), "O %u", g->done[CELL_O]);
    gfx_text_right(232, 13, b, 2, COL_O);

    if (g->dare == DARE_NONE) {
        float t = g->anim_ms / 1000.0f;
        float dx = g->shaking ? sinf(t * 40.0f) * 10.0f : 0, dy = g->shaking ? cosf(t * 31.0f) * 5.0f : 0;
        draw_console(CX + dx, 112 + dy, 30, pc);
        if (g->shaking) {
            gfx_line(CX - 50, 92, CX - 42, 104, 2, COL_DIM);
            gfx_line(CX - 54, 116, CX - 44, 120, 2, COL_DIM);
            gfx_line(CX + 50, 92, CX + 42, 104, 2, COL_DIM);
            gfx_line(CX + 54, 116, CX + 44, 120, 2, COL_DIM);
        }
        snprintf(b, sizeof(b), "%c, SECOUE LA CONSOLE !", game_mark_char(p));
        gfx_text_mid(CX, 166, b, 1, pc);
        gfx_text_mid(CX, 190, "SECOUE !", 2, COL_TEXT);
        gfx_text_mid(CX, 222, "UN DEFI VA TOMBER...", 1, COL_DIM);
    } else {
        snprintf(b, sizeof(b), "DEFI POUR %c :", game_mark_char(p));
        gfx_text_mid(CX, 46, b, 1, pc);
        big_text(124, DARES[g->dare], COL_ACCENT);
        snprintf(b, sizeof(b), "TOUCHE OU DROITE : A %c", game_mark_char(game_other(p)));
        gfx_text_mid(CX, 222, b, 1, COL_DIM);
    }
}

static void dare_banner(const void *st, char *buf, int n, uint8_t *mark)
{
    const dare_t *g = st;
    *mark = g->r.turn;
    snprintf(buf, n, g->dare == DARE_NONE ? "TOUR DE %c" : "DEFI DE %c", game_mark_char(g->r.turn));
}

static void dare_icon(int x, int y, int s)
{
    gfx_rrect(x, y, s, s, 6, COL_LINE);
    float cx = x + s / 2.0f, cy = y + s / 2.0f;
    gfx_rrect((int)(cx - s * 0.16f), (int)(cy - s * 0.26f), (int)(s * 0.32f), (int)(s * 0.52f), 3, COL_ACCENT);
    gfx_line(cx - s * 0.36f, cy - s * 0.18f, cx - s * 0.26f, cy - s * 0.04f, 1.5f, COL_DIM);
    gfx_line(cx - s * 0.38f, cy + s * 0.06f, cx - s * 0.26f, cy + s * 0.1f, 1.5f, COL_DIM);
    gfx_line(cx + s * 0.36f, cy - s * 0.18f, cx + s * 0.26f, cy - s * 0.04f, 1.5f, COL_DIM);
    gfx_line(cx + s * 0.38f, cy + s * 0.06f, cx + s * 0.26f, cy + s * 0.1f, 1.5f, COL_DIM);
    gfx_text_mid((int)cx, (int)(cy - s * 0.14f), "!", 2, COL_TEXT);
}

static const char *const MORE_VERBS[] = {"pass", NULL};

static const char *const RULES[] = {
    "CHACUN SON TOUR, SECOUE LA CONSOLE.",
    "",
    "APRES QUELQUES SECONDES DE",
    "SECOUSSE, UN SON RETENTIT ET",
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
_Static_assert(DARE_NEED_MIN_MS == 1500 && DARE_NEED_MAX_MS == 4000, "update rules_uart");

const game_def_t DEFI_SECOUSSE = {
    .id = "dare",
    .title = "DEFI SECOUSSE",
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
    .rules_uart = "shake=1.5-4s dare=random repeat=never-before-all pass=tap|right",
    .banner = dare_banner,
    .right_taps = true,
};
#endif
