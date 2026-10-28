#include "drawguess.h"
#include <string.h>

/* French nouns of objects, animals and things that are quick to draw with a finger. One
 * word each, accents dropped and lower case a-z only (the UART lines; the screen font
 * shows them in capitals, where French usually omits the accents anyway). */
const char *const DG_WORDS[] = {
    /* animaux */
    "chat", "chien", "poisson", "oiseau", "cheval", "vache", "cochon", "mouton", "lapin", "souris",
    "canard", "poule", "serpent", "tortue", "escargot", "papillon", "abeille", "araignee", "fourmi",
    "coccinelle", "elephant", "girafe", "lion", "tigre", "singe", "ours", "loup", "renard", "zebre",
    "crocodile", "requin", "baleine", "dauphin", "pieuvre", "crabe", "meduse", "grenouille", "hibou",
    "pingouin", "chameau", "kangourou", "panda", "hippopotame", "ecureuil", "herisson", "dinosaure",
    "dragon", "licorne", "ver", "hamster",
    /* nature */
    "soleil", "lune", "etoile", "nuage", "pluie", "arbre", "fleur", "feuille", "montagne", "volcan",
    "ile", "plage", "vague", "riviere", "cactus", "champignon", "palmier", "sapin", "rose", "tulipe",
    "eclair", "flocon", "feu", "caillou", "tournesol", "glacon", "planete", "comete",
    /* a manger */
    "pomme", "poire", "banane", "cerise", "fraise", "raisin", "citron", "orange", "ananas", "carotte",
    "tomate", "pasteque", "gateau", "pizza", "pain", "fromage", "oeuf", "glace", "bonbon", "sucette",
    "croissant", "baguette", "hamburger", "frites", "saucisse", "biscuit", "chocolat", "sandwich",
    "poivron", "salade", "noix", "brocoli", "citrouille", "crepe", "beignet",
    /* la maison */
    "maison", "porte", "fenetre", "chaise", "table", "lit", "lampe", "horloge", "tasse", "verre",
    "assiette", "fourchette", "couteau", "cuillere", "bouteille", "casserole", "bougie", "miroir",
    "canape", "television", "telephone", "ordinateur", "clavier", "livre", "crayon", "stylo",
    "ciseaux", "regle", "gomme", "cartable", "cle", "cadenas", "cloche", "balai", "seau", "parapluie",
    "valise", "cadeau", "ballon", "baignoire", "douche", "robinet", "frigo", "armoire", "escalier",
    "toit", "cheminee", "radiateur", "ventilateur", "aspirateur", "poubelle", "tableau", "coussin",
    /* vetements et bijoux */
    "chapeau", "chaussure", "botte", "chaussette", "pantalon", "robe", "jupe", "chemise", "cravate",
    "lunettes", "gant", "echarpe", "bonnet", "casquette", "couronne", "bague", "collier", "montre",
    "sac", "ceinture", "pyjama", "tablier",
    /* transports */
    "voiture", "bateau", "avion", "train", "velo", "moto", "camion", "bus", "fusee", "helicoptere",
    "tracteur", "trottinette", "ambulance", "voilier", "montgolfiere", "roue", "ancre", "metro",
    "parachute", "taxi",
    /* objets et lieux */
    "guitare", "piano", "tambour", "trompette", "violon", "marteau", "tournevis", "scie", "echelle",
    "pelle", "ampoule", "aimant", "loupe", "boussole", "enveloppe", "timbre", "drapeau", "tente",
    "coeur", "fantome", "robot", "squelette", "sorciere", "chateau", "pont", "phare", "moulin",
    "eglise", "pyramide", "igloo", "cabane", "puits", "fontaine", "banc", "epee", "bouclier",
    "fleche", "cible", "toupie", "poupee", "trophee", "medaille", "sifflet", "micro", "radio",
    "pile", "aiguille", "bouton", "hamac", "brosse", "peigne", "savon", "raquette", "panier",
    "balancoire", "toboggan", "sablier", "lanterne", "tresor", "pirate", "clown", "ange", "sirene",
    /* le corps */
    "main", "pied", "oeil", "nez", "bouche", "oreille", "dent", "os",
};
const int DG_WORD_COUNT = sizeof(DG_WORDS) / sizeof(DG_WORDS[0]);

uint8_t dg_pts[DG_MAX_PTS][2];
uint16_t dg_stroke_start[DG_MAX_STROKES];

/* finger travel below this (px) adds no point: a resting finger's jitter draws nothing */
#define MIN_STEP 3

void dg_start(dg_t *g, uint8_t first, int word)
{
    memset(g, 0, sizeof(*g));
    game_round_start(&g->r, first);
    g->phase = DG_WORD;
    g->word = (uint16_t)(word >= 0 && word < DG_WORD_COUNT ? word : 0);
    g->force_correct = -1;
    g->picked = -1;
}

dg_result_t dg_begin(dg_t *g)
{
    if (g->phase != DG_WORD) {
        return DG_ERR_PHASE;
    }
    g->phase = DG_DRAW;
    g->r.moves = 1; /* a round in progress: leaving it asks first */
    return DG_OK;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void add_point(dg_t *g, int x, int y)
{
    dg_pts[g->n_pts][0] = (uint8_t)clampi(x - DG_CANVAS_X, 0, DG_CANVAS_W - 1);
    dg_pts[g->n_pts][1] = (uint8_t)clampi(y - DG_CANVAS_Y, 0, DG_CANVAS_H - 1);
    g->n_pts++;
}

dg_result_t dg_pen(dg_t *g, int phase, int x, int y)
{
    if (g->phase != DG_DRAW) {
        g->pen_down = false;
        return DG_ERR_PHASE;
    }
    if (phase == DG_PEN_UP) {
        g->pen_down = false;
        return DG_OK;
    }
    if (phase == DG_PEN_DOWN) {
        if (g->n_strokes >= DG_MAX_STROKES || g->n_pts >= DG_MAX_PTS) {
            g->pen_down = false;
            return DG_ERR_FULL;
        }
        dg_stroke_start[g->n_strokes++] = g->n_pts;
        add_point(g, x, y);
        g->pen_down = true;
        return DG_OK;
    }
    if (!g->pen_down) {
        return DG_ERR_PHASE;
    }
    if (g->n_pts >= DG_MAX_PTS) {
        g->pen_down = false;
        return DG_ERR_FULL;
    }
    const uint8_t *last = dg_pts[g->n_pts - 1];
    int dx = clampi(x - DG_CANVAS_X, 0, DG_CANVAS_W - 1) - last[0];
    int dy = clampi(y - DG_CANVAS_Y, 0, DG_CANVAS_H - 1) - last[1];
    if (dx * dx + dy * dy < MIN_STEP * MIN_STEP) {
        return DG_ERR_RANGE; /* not far enough: nothing changed */
    }
    add_point(g, x, y);
    return DG_OK;
}

dg_result_t dg_undo(dg_t *g)
{
    if (g->phase != DG_DRAW) {
        return DG_ERR_PHASE;
    }
    if (g->n_strokes == 0) {
        return DG_ERR_EMPTY;
    }
    g->n_strokes--;
    g->n_pts = dg_stroke_start[g->n_strokes];
    g->pen_down = false;
    return DG_OK;
}

dg_result_t dg_clear(dg_t *g)
{
    if (g->phase != DG_DRAW) {
        return DG_ERR_PHASE;
    }
    g->n_strokes = 0;
    g->n_pts = 0;
    g->pen_down = false;
    return DG_OK;
}

/* a random word index other than the secret word and the decoys already picked */
static uint16_t pick_decoy(const dg_t *g, int n)
{
    for (;;) {
        uint16_t w = (uint16_t)(game_random() % DG_WORD_COUNT);
        bool used = w == g->word;
        for (int i = 0; i < n; i++) {
            used = used || g->choices[i] == w;
        }
        if (!used) {
            return w;
        }
    }
}

dg_result_t dg_done(dg_t *g, bool force)
{
    if ((g->phase == DG_WORD || g->phase == DG_DRAW) && g->n_pts == 0 && !force) {
        return DG_ERR_EMPTY;
    }
    if (g->phase != DG_DRAW) {
        return DG_ERR_PHASE;
    }
    g->correct = g->force_correct >= 0 ? (uint8_t)g->force_correct : (uint8_t)(game_random() % DG_CHOICES);
    /* decoys first (distinct from the word and each other), then the word in its place */
    uint16_t decoys[DG_CHOICES - 1];
    for (int i = 0; i < DG_CHOICES - 1; i++) {
        decoys[i] = pick_decoy(g, i);
        g->choices[i] = decoys[i];
    }
    for (int i = 0, d = 0; i < DG_CHOICES; i++) {
        g->choices[i] = i == g->correct ? g->word : decoys[d++];
    }
    g->phase = DG_HANDOVER;
    g->pen_down = false;
    g->r.turn = game_other(g->r.starter); /* the board goes to the guesser */
    return DG_OK;
}

dg_result_t dg_reveal(dg_t *g)
{
    if (g->phase != DG_HANDOVER) {
        return DG_ERR_PHASE;
    }
    g->phase = DG_GUESS;
    return DG_OK;
}

dg_result_t dg_guess(dg_t *g, int i)
{
    if (g->phase != DG_GUESS) {
        return DG_ERR_PHASE;
    }
    if (i < 0 || i >= DG_CHOICES) {
        return DG_ERR_RANGE;
    }
    g->picked = (int8_t)i;
    g->phase = DG_RESULT;
    if (i == g->correct) {
        g->r.status = GAME_WIN;
        g->r.winner = g->r.turn;
    } else {
        g->r.status = GAME_DRAW;
    }
    return DG_OK;
}

bool dg_tick(dg_t *g, uint32_t dt_ms)
{
    if (g->phase != DG_DRAW) {
        return false;
    }
    g->drawn_ms += dt_ms;
    if (g->drawn_ms < DG_LIMIT_MS) {
        return false;
    }
    g->drawn_ms = DG_LIMIT_MS;
    dg_done(g, true);
    return true;
}

#ifndef GAME_RULES_ONLY
/* ---- the game module: console, touch and drawing ---- */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "app.h"
#include "display.h"

_Static_assert(sizeof(dg_t) <= GAME_STATE_MAX, "dg_t does not fit in a game slot");

#define CX (SCREEN_W / 2)

/* moves (taps): what dg_hit() returns and dg_move() applies */
enum { MV_START, MV_UNDO, MV_CLEAR, MV_DONE, MV_REVEAL, MV_GUESS0 };

typedef struct {
    int x, y, w, h;
} box_t;

static bool in_box(box_t b, int x, int y) { return x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h; }

/* drawing screen: the canvas, then UNDO, CLEAR, DONE */
static const box_t BTN_UNDO = {4, 190, 74, 46};
static const box_t BTN_CLEAR = {83, 190, 74, 46};
static const box_t BTN_DONE = {162, 190, 74, 46};
/* guessing screen: the drawing scaled into the top, three word buttons below */
#define GUESS_Y0 38
#define GUESS_H 88
#define CHOICE_Y0 130
#define CHOICE_H 34
#define CHOICE_GAP 3
static box_t choice_box(int i) { return (box_t){8, CHOICE_Y0 + i * (CHOICE_H + CHOICE_GAP), 224, CHOICE_H}; }

static uint16_t mark_col(uint8_t m) { return m == CELL_X ? COL_X : COL_O; }

static uint8_t guesser(const dg_t *g) { return game_other(g->r.starter); }

static const char *word_of(int i) { return DG_WORDS[i]; }

static void dg_new_round(void *st, uint8_t first) { dg_start(st, first, (int)(game_random() % DG_WORD_COUNT)); }

static unsigned left_s(const dg_t *g) { return (DG_LIMIT_MS - g->drawn_ms + 999) / 1000; }

static const char *const PHASE_NAMES[] = {"word", "draw", "handover", "guess", "result"};

static void dg_print(const void *st)
{
    const dg_t *g = st;
    app_out("DG phase=%s drawer=%c strokes=%u pts=%u left=%us", PHASE_NAMES[g->phase], game_mark_char(g->r.starter),
            g->n_strokes, g->n_pts, left_s(g));
}

static void dg_report(const void *st) { dg_print(st); }

static void print_choices(const dg_t *g)
{
    app_out("CHOICES %s,%s,%s correct=%u word=%s", word_of(g->choices[0]), word_of(g->choices[1]),
            word_of(g->choices[2]), g->correct, word_of(g->word));
}

static game_event_t err(dg_result_t res)
{
    switch (res) {
    case DG_ERR_EMPTY:
        app_out("ERR empty (draw something first)");
        break;
    case DG_ERR_RANGE:
        app_out("ERR range (guess 0-2)");
        break;
    case DG_ERR_FULL:
        app_out("ERR canvas full");
        break;
    default:
        app_out("ERR wrong step");
        break;
    }
    return GEV_BAD;
}

static game_event_t moved(const dg_t *g) { return g->r.turn == CELL_X ? GEV_MOVE_X : GEV_MOVE_O; }

static game_event_t dg_move(void *st, int m, const char *src)
{
    dg_t *g = st;
    dg_result_t res;
    if (g->r.status != GAME_PLAYING) {
        app_out("ERR game over");
        return GEV_BAD;
    }
    switch (m) {
    case MV_START:
        if ((res = dg_begin(g)) != DG_OK) {
            return err(res);
        }
        app_out("START drawer=%c src=%s", game_mark_char(g->r.starter), src);
        return moved(g);
    case MV_UNDO:
        if ((res = dg_undo(g)) != DG_OK) {
            return err(res);
        }
        app_out("UNDO strokes=%u pts=%u", g->n_strokes, g->n_pts);
        return moved(g);
    case MV_CLEAR:
        if ((res = dg_clear(g)) != DG_OK) {
            return err(res);
        }
        app_out("CLEAR");
        return moved(g);
    case MV_DONE:
        if ((res = dg_done(g, false)) != DG_OK) {
            return err(res);
        }
        app_out("DONE drawer=%c strokes=%u pts=%u ms=%lu", game_mark_char(g->r.starter), g->n_strokes, g->n_pts,
                (unsigned long)g->drawn_ms);
        app_out("SCREEN handover to=%c", game_mark_char(guesser(g)));
        return moved(g);
    case MV_REVEAL:
        if ((res = dg_reveal(g)) != DG_OK) {
            return err(res);
        }
        app_out("SCREEN guess guesser=%c", game_mark_char(guesser(g)));
        print_choices(g);
        return moved(g);
    default:
        if (g->phase != DG_GUESS) {
            app_out("ERR not guessing");
            return GEV_BAD;
        }
        if ((res = dg_guess(g, m - MV_GUESS0)) != DG_OK) {
            return err(res);
        }
        app_out("GUESS %c pick=%d word=%s", game_mark_char(guesser(g)), g->picked, word_of(g->choices[g->picked]));
        bool right = g->r.status == GAME_WIN;
        app_out("RESULT %s word=%s", right ? "correct" : "wrong", word_of(g->word));
        return right ? GEV_WIN : GEV_DRAW;
    }
}

/* done | reveal | guess <0-2> | start | undo | clear */
static game_event_t dg_command(void *st, const char *text)
{
    char sub[12] = "";
    int n = -1;
    int k = sscanf(text, "%11s %d", sub, &n);
    if (strcmp(sub, "guess") == 0) {
        dg_t *g = st;
        if (g->r.status == GAME_PLAYING && g->phase != DG_GUESS) {
            app_out("ERR not guessing");
            return GEV_BAD;
        }
        if (k != 2 || n < 0 || n >= DG_CHOICES) {
            app_out("ERR range (guess 0-2)");
            return GEV_BAD;
        }
        return dg_move(st, MV_GUESS0 + n, "uart");
    }
    static const char *const SUBS[] = {"start", "undo", "clear", "done", "reveal"};
    for (int i = 0; i < 5; i++) {
        if (strcmp(sub, SUBS[i]) == 0) {
            return dg_move(st, i, "uart");
        }
    }
    app_out("ERR draw (start | undo | clear | done | reveal | guess <0-2>)");
    return GEV_BAD;
}

static int dg_hit(const void *st, int x, int y)
{
    const dg_t *g = st;
    switch (g->phase) {
    case DG_WORD:
        return MV_START;
    case DG_DRAW:
        if (in_box(BTN_UNDO, x, y)) {
            return MV_UNDO;
        }
        if (in_box(BTN_CLEAR, x, y)) {
            return MV_CLEAR;
        }
        if (in_box(BTN_DONE, x, y)) {
            return MV_DONE;
        }
        if (y >= DG_CANVAS_Y && y < DG_CANVAS_Y + DG_CANVAS_H) {
            return GAME_HIT_PEN;
        }
        return -1;
    case DG_HANDOVER:
        return MV_REVEAL;
    case DG_GUESS:
        for (int i = 0; i < DG_CHOICES; i++) {
            if (in_box(choice_box(i), x, y)) {
                return MV_GUESS0 + i;
            }
        }
        return -1;
    default:
        return -1;
    }
}

/* true when the canvas changed (redraw) */
static bool dg_pen_hook(void *st, int phase, int x, int y) { return dg_pen(st, phase, x, y) == DG_OK; }

static game_event_t dg_step(void *st, const motion_t *m)
{
    dg_t *g = st;
    if (g->r.status != GAME_PLAYING || !dg_tick(g, m->dt_ms)) {
        return GEV_NONE;
    }
    app_out("TIME up drawer=%c strokes=%u pts=%u", game_mark_char(g->r.starter), g->n_strokes, g->n_pts);
    app_out("SCREEN handover to=%c", game_mark_char(guesser(g)));
    return moved(g);
}

/* the test stroke: a closed loop of points around the middle of the canvas */
static void sim_stroke(dg_t *g)
{
    int cx = DG_CANVAS_X + DG_CANVAS_W / 2, cy = DG_CANVAS_Y + DG_CANVAS_H / 2;
    if (dg_pen(g, DG_PEN_DOWN, cx + 40, cy) != DG_OK) {
        app_out("ERR canvas full");
        return;
    }
    for (int i = 1; i <= 24; i++) {
        float a = i * (2 * (float)M_PI / 24);
        dg_pen(g, DG_PEN_MOVE, cx + (int)lroundf(40 * cosf(a)), cy + (int)lroundf(30 * sinf(a)));
    }
    dg_pen(g, DG_PEN_UP, 0, 0);
    app_out("STROKE n=%u pts=%u", g->n_strokes, g->n_pts);
}

/* sim word <idx> | sim correct <0-2> | sim stroke */
static bool dg_sim_set(void *st, const char *key, long val)
{
    dg_t *g = st;
    bool early = g->r.status == GAME_PLAYING && (g->phase == DG_WORD || g->phase == DG_DRAW);
    if (strcmp(key, "word") == 0) {
        if (g->r.status != GAME_PLAYING || g->phase != DG_WORD) {
            app_out("ERR word (only before the drawing starts)");
        } else if (val < 0 || val >= DG_WORD_COUNT) {
            app_out("ERR word 0-%d", DG_WORD_COUNT - 1);
        } else {
            g->word = (uint16_t)val;
            app_out("WORD idx=%ld word=%s", val, word_of(g->word));
        }
        return true;
    }
    if (strcmp(key, "correct") == 0) {
        if (!early || val < 0 || val >= DG_CHOICES) {
            app_out("ERR correct 0-2 (before DONE)");
        } else {
            g->force_correct = (int8_t)val;
            app_out("SIM correct=%ld", val);
        }
        return true;
    }
    if (strcmp(key, "stroke") == 0 && val < 0) {
        if (!early) {
            app_out("ERR not drawing");
            return true;
        }
        if (g->phase == DG_WORD) {
            dg_begin(g); /* the drawer's tap to start */
            app_out("START drawer=%c src=sim", game_mark_char(g->r.starter));
        }
        sim_stroke(g);
        return true;
    }
    return false;
}

static void dg_banner(const void *st, char *buf, int n, uint8_t *mark)
{
    const dg_t *g = st;
    *mark = g->r.turn;
    switch (g->phase) {
    case DG_WORD:
        snprintf(buf, n, "%c DESSINE", game_mark_char(g->r.starter));
        break;
    case DG_DRAW:
        snprintf(buf, n, "%c: %u S", game_mark_char(g->r.starter), left_s(g));
        break;
    case DG_HANDOVER:
        snprintf(buf, n, "PASSE A %c", game_mark_char(guesser(g)));
        break;
    case DG_GUESS:
        snprintf(buf, n, "%c DEVINE", game_mark_char(guesser(g)));
        break;
    default:
        if (g->r.status == GAME_WIN) {
            snprintf(buf, n, "%c TROUVE", game_mark_char(g->r.winner));
            *mark = g->r.winner;
        } else {
            snprintf(buf, n, "RATE");
            *mark = CELL_EMPTY;
        }
        break;
    }
}

/* the round is scored: the score and the next drawer (the app swaps the roles) */
static void dg_finished(const void *st, int wins_x, int wins_o, uint8_t next)
{
    (void)st;
    app_out("SCORE X=%d O=%d", wins_x, wins_o);
    app_out("TURN drawer=%c", game_mark_char(next));
}

/* the strokes of g, canvas pixel (0,0) at ox,oy, scaled by k */
static void draw_strokes(const dg_t *g, float ox, float oy, float k, float width, uint16_t c)
{
    int n_pts = g->n_pts > DG_MAX_PTS ? DG_MAX_PTS : g->n_pts;
    int n_strokes = g->n_strokes > DG_MAX_STROKES ? DG_MAX_STROKES : g->n_strokes;
    for (int s = 0; s < n_strokes; s++) {
        int a = dg_stroke_start[s], b = s + 1 < n_strokes ? dg_stroke_start[s + 1] : n_pts;
        if (b > n_pts) {
            b = n_pts;
        }
        if (a >= b) {
            continue;
        }
        if (b - a == 1) {
            gfx_disc(ox + (dg_pts[a][0] + 0.5f) * k, oy + (dg_pts[a][1] + 0.5f) * k, width / 2, c);
            continue;
        }
        for (int i = a + 1; i < b; i++) {
            gfx_line(ox + (dg_pts[i - 1][0] + 0.5f) * k, oy + (dg_pts[i - 1][1] + 0.5f) * k,
                     ox + (dg_pts[i][0] + 0.5f) * k, oy + (dg_pts[i][1] + 0.5f) * k, width, c);
        }
    }
}

static void btn(box_t b, const char *label, int scale, uint16_t c)
{
    gfx_rrect(b.x, b.y, b.w, b.h, 8, c);
    gfx_text_mid(b.x + b.w / 2, b.y + (b.h - 7 * scale) / 2, label, scale, c);
}

/* a word as large as fits the screen width */
static void big_word(int y, const char *w, uint16_t c)
{
    int scale = gfx_text_w(w, 3) <= SCREEN_W - 16 ? 3 : 2;
    gfx_text_mid(CX, y, w, scale, c);
}

static void dg_draw(const void *st, int preview)
{
    (void)preview;
    const dg_t *g = st;
    char b[40];
    uint8_t drawer = g->r.starter, other = guesser(g);
    switch (g->phase) {
    case DG_WORD:
        snprintf(b, sizeof(b), "%c, TON MOT SECRET :", game_mark_char(drawer));
        gfx_text_mid(CX, 54, b, 1, mark_col(drawer));
        big_word(76, word_of(g->word), COL_ACCENT);
        snprintf(b, sizeof(b), "%c NE DOIT PAS LE VOIR !", game_mark_char(other));
        gfx_text_mid(CX, 120, b, 1, COL_DIM);
        snprintf(b, sizeof(b), "DESSINE-LE EN %d S", DG_LIMIT_MS / 1000);
        gfx_text_mid(CX, 138, b, 1, COL_DIM);
        btn((box_t){40, 166, 160, 48}, "COMMENCER", 2, COL_TEXT);
        break;
    case DG_DRAW: {
        gfx_rrect(DG_CANVAS_X, DG_CANVAS_Y, DG_CANVAS_W, DG_CANVAS_H, 6, COL_LINE);
        gfx_text(6, DG_CANVAS_Y + 5, word_of(g->word), 1, COL_DIM); /* a reminder for the drawer */
        draw_strokes(g, DG_CANVAS_X, DG_CANVAS_Y, 1.0f, 3.2f, COL_TEXT);
        uint16_t edit = g->n_strokes ? COL_TEXT : COL_DIM;
        btn(BTN_UNDO, "OUPS", 2, edit);
        btn(BTN_CLEAR, "EFFACE", 2, edit);
        btn(BTN_DONE, "FINI", 2, g->n_pts ? COL_ACCENT : COL_DIM);
        break;
    }
    case DG_HANDOVER:
        gfx_text_mid(CX, 62, "PASSE LA CONSOLE", 2, COL_TEXT);
        snprintf(b, sizeof(b), "A %c", game_mark_char(other));
        gfx_text_mid(CX, 88, b, 3, mark_col(other));
        snprintf(b, sizeof(b), "%c : TOUCHE POUR", game_mark_char(other));
        gfx_text_mid(CX, 150, b, 2, COL_TEXT);
        gfx_text_mid(CX, 172, "VOIR LE DESSIN", 2, COL_TEXT);
        break;
    case DG_GUESS: {
        float k = (float)GUESS_H / DG_CANVAS_H;
        float w = DG_CANVAS_W * k, ox = (SCREEN_W - w) / 2;
        gfx_rrect((int)ox - 2, GUESS_Y0, (int)w + 4, GUESS_H, 6, COL_LINE);
        draw_strokes(g, ox, GUESS_Y0, k, 2.2f, COL_TEXT);
        for (int i = 0; i < DG_CHOICES; i++) {
            btn(choice_box(i), word_of(g->choices[i]), 2, COL_TEXT);
        }
        break;
    }
    default: {
        bool right = g->r.status == GAME_WIN;
        gfx_text_mid(CX, 50, right ? "BRAVO !" : "RATE", 3, right ? COL_ACCENT : COL_O);
        gfx_text_mid(CX, 84, "LE MOT ETAIT", 1, COL_DIM);
        big_word(98, word_of(g->word), COL_TEXT);
        if (!right && g->picked >= 0) {
            snprintf(b, sizeof(b), "%c A CHOISI %s", game_mark_char(other), word_of(g->choices[g->picked]));
            gfx_text_mid(CX, 134, b, 1, COL_DIM);
        }
        if (right) {
            snprintf(b, sizeof(b), "+1 POUR %c", game_mark_char(other));
            gfx_text_mid(CX, 156, b, 2, mark_col(other));
        } else {
            gfx_text_mid(CX, 156, "PAS DE POINT", 2, COL_DIM);
        }
        snprintf(b, sizeof(b), "TOUCHE : %c DESSINE ENSUITE", game_mark_char(other));
        gfx_text_mid(CX, 220, b, 1, COL_DIM);
        break;
    }
    }
}

/* a framed canvas with a squiggle and a pencil */
static void dg_icon(int x, int y, int s)
{
    gfx_rrect(x, y, s, s, 6, COL_LINE);
    float u = s / 48.0f;
    gfx_line(x + 8 * u, y + 30 * u, x + 16 * u, y + 20 * u, 2, COL_TEXT);
    gfx_line(x + 16 * u, y + 20 * u, x + 22 * u, y + 32 * u, 2, COL_TEXT);
    gfx_line(x + 22 * u, y + 32 * u, x + 28 * u, y + 24 * u, 2, COL_TEXT);
    gfx_line(x + 30 * u, y + 34 * u, x + 40 * u, y + 10 * u, 5 * u, COL_ACCENT);
    gfx_disc(x + 29 * u, y + 36 * u, 1.8f * u, COL_ACCENT);
}

static const char *const MORE_VERBS[] = {"done", "reveal", "guess", "start", "undo", "clear", NULL};

const game_def_t DRAW_GUESS = {
    .id = "draw",
    .title = "DESSINE ET DEVINE",
    .verb = "dg",
    .more_verbs = MORE_VERBS,
    .starter_key = "drawer",
    .new_round = dg_new_round,
    .print = dg_print,
    .move = dg_move,
    .command = dg_command,
    .hit_state = dg_hit,
    .pen = dg_pen_hook,
    .draw = dg_draw,
    .icon = dg_icon,
    .step = dg_step,
    .report = dg_report,
    .sim_set = dg_sim_set,
    .timed = true,
    .banner = dg_banner,
    .finished = dg_finished,
};
#endif
