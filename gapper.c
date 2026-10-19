#include <SDL.h>

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SCREEN_W 320
#define SCREEN_H 200
#define MAX_COLS 8
#define MAX_ROWS 7
#define MAX_SCORES 10
#define SCORE_FILE "GAPPER.HS"

typedef enum { DIR_NONE, DIR_UP, DIR_LEFT, DIR_DOWN, DIR_RIGHT } Direction;
typedef enum { ST_NAME, ST_MENU, ST_HELP1, ST_HELP2, ST_READY, ST_PLAY,
               ST_DEAD, ST_LEVEL_DONE, ST_GAME_OVER } ScreenState;

typedef struct {
    int exists;
    int painted;
    double paint_low;
    double paint_high;
    double blocked_until;
    double gap_at;
} Edge;

typedef struct {
    int gx, gy;
    int tx, ty;
    int from_x, from_y;
    Direction dir;
    Direction queued;
    double progress;
    int waiting_at_gap;
} Actor;

typedef struct {
    char name[4];
    int score;
    int level;
} ScoreEntry;

typedef struct {
    int cols, rows;
    int cell[MAX_ROWS][MAX_COLS];
    int complete[MAX_ROWS][MAX_COLS];
    Edge h[MAX_ROWS + 1][MAX_COLS];
    Edge v[MAX_ROWS][MAX_COLS + 1];
    int completed_cells;
    int total_cells;
} Board;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_AudioDeviceID audio;
    Board board;
    Actor player, seeker;
    ScreenState state;
    ScoreEntry scores[MAX_SCORES];
    char initials[4];
    int initials_len;
    int score;
    int level;
    int lives;
    int next_life;
    int bonus_x, bonus_y;
    int bonus_on;
    int gap_x1, gap_y1, gap_x2, gap_y2;
    double now;
    double state_until;
    double bonus_until;
    double speed_scale;
    uint32_t rng;
    int running;
} Game;

typedef struct { char c; uint8_t row[7]; } Glyph;
typedef struct { char c; uint8_t row[8]; } PcGlyph;

static const Glyph font[] = {
    {' ',{0,0,0,0,0,0,0}}, {'A',{14,17,17,31,17,17,17}},
    {'B',{30,17,17,30,17,17,30}}, {'C',{14,17,16,16,16,17,14}},
    {'D',{30,17,17,17,17,17,30}}, {'E',{31,16,16,30,16,16,31}},
    {'F',{31,16,16,30,16,16,16}}, {'G',{14,17,16,23,17,17,15}},
    {'H',{17,17,17,31,17,17,17}}, {'I',{14,4,4,4,4,4,14}},
    {'J',{7,2,2,2,18,18,12}}, {'K',{17,18,20,24,20,18,17}},
    {'L',{16,16,16,16,16,16,31}}, {'M',{17,27,21,21,17,17,17}},
    {'N',{17,25,21,19,17,17,17}}, {'O',{14,17,17,17,17,17,14}},
    {'P',{30,17,17,30,16,16,16}}, {'Q',{14,17,17,17,21,18,13}},
    {'R',{30,17,17,30,20,18,17}}, {'S',{15,16,16,14,1,1,30}},
    {'T',{31,4,4,4,4,4,4}}, {'U',{17,17,17,17,17,17,14}},
    {'V',{17,17,17,17,17,10,4}}, {'W',{17,17,17,21,21,21,10}},
    {'X',{17,17,10,4,10,17,17}}, {'Y',{17,17,10,4,4,4,4}},
    {'Z',{31,1,2,4,8,16,31}}, {'0',{14,17,19,21,25,17,14}},
    {'1',{4,12,4,4,4,4,14}}, {'2',{14,17,1,2,4,8,31}},
    {'3',{30,1,1,14,1,1,30}}, {'4',{2,6,10,18,31,2,2}},
    {'5',{31,16,16,30,1,1,30}}, {'6',{14,16,16,30,17,17,14}},
    {'7',{31,1,2,4,8,8,8}}, {'8',{14,17,17,14,17,17,14}},
    {'9',{14,17,17,15,1,1,14}}, {'.',{0,0,0,0,0,12,12}},
    {',',{0,0,0,0,0,12,8}}, {':',{0,12,12,0,12,12,0}},
    {'!',{4,4,4,4,4,0,4}}, {'?',{14,17,1,2,4,0,4}},
    {'-',{0,0,0,31,0,0,0}}, {'+',{0,4,4,31,4,4,0}},
    {'[',{14,8,8,8,8,8,14}}, {']',{14,2,2,2,2,2,14}},
    {'(',{2,4,8,8,8,4,2}}, {')',{8,4,2,2,2,4,8}},
    {'/',{1,2,2,4,8,8,16}}, {'\'',{4,4,0,0,0,0,0}},
    {'$',{4,15,20,14,5,30,4}}, {'#',{10,31,10,10,31,10,0}},
    {'=',{0,0,31,0,31,0,0}}
};

/* IBM PC ROM-style 8x8 glyphs used by the original SCREEN 1 title and HUD. */
static const PcGlyph pc_font[] = {
    {' ',{0,0,0,0,0,0,0,0}},
    {'0',{0x3e,0x63,0x73,0x7b,0x6f,0x67,0x3e,0}},
    {'1',{0x0c,0x0e,0x0c,0x0c,0x0c,0x0c,0x3f,0}},
    {'2',{0x1e,0x33,0x30,0x1c,0x06,0x33,0x3f,0}},
    {'3',{0x1e,0x33,0x30,0x1c,0x30,0x33,0x1e,0}},
    {'4',{0x38,0x3c,0x36,0x33,0x7f,0x30,0x78,0}},
    {'5',{0x3f,0x03,0x1f,0x30,0x30,0x33,0x1e,0}},
    {'6',{0x1c,0x06,0x03,0x1f,0x33,0x33,0x1e,0}},
    {'7',{0x3f,0x33,0x30,0x18,0x0c,0x0c,0x0c,0}},
    {'8',{0x1e,0x33,0x33,0x1e,0x33,0x33,0x1e,0}},
    {'9',{0x1e,0x33,0x33,0x3e,0x30,0x18,0x0e,0}},
    {':',{0,0x0c,0x0c,0,0,0x0c,0x0c,0}},
    {'.',{0,0,0,0,0,0x0c,0x0c,0}},
    {'-',{0,0,0,0x3f,0,0,0,0}},
    {'_',{0,0,0,0,0,0,0,0xff}},
    {'[',{0x1e,0x06,0x06,0x06,0x06,0x06,0x1e,0}},
    {']',{0x1e,0x18,0x18,0x18,0x18,0x18,0x1e,0}},
    {'A',{0x0c,0x1e,0x33,0x33,0x3f,0x33,0x33,0}},
    {'B',{0x3f,0x66,0x66,0x3e,0x66,0x66,0x3f,0}},
    {'C',{0x3c,0x66,0x03,0x03,0x03,0x66,0x3c,0}},
    {'D',{0x1f,0x36,0x66,0x66,0x66,0x36,0x1f,0}},
    {'E',{0x7f,0x46,0x16,0x1e,0x16,0x46,0x7f,0}},
    {'F',{0x7f,0x46,0x16,0x1e,0x16,0x06,0x0f,0}},
    {'G',{0x3c,0x66,0x03,0x03,0x73,0x66,0x7c,0}},
    {'H',{0x33,0x33,0x33,0x3f,0x33,0x33,0x33,0}},
    {'I',{0x1e,0x0c,0x0c,0x0c,0x0c,0x0c,0x1e,0}},
    {'J',{0x78,0x30,0x30,0x30,0x33,0x33,0x1e,0}},
    {'K',{0x67,0x66,0x36,0x1e,0x36,0x66,0x67,0}},
    {'L',{0x0f,0x06,0x06,0x06,0x46,0x66,0x7f,0}},
    {'M',{0x63,0x77,0x7f,0x7f,0x6b,0x63,0x63,0}},
    {'N',{0x63,0x67,0x6f,0x7b,0x73,0x63,0x63,0}},
    {'O',{0x1c,0x36,0x63,0x63,0x63,0x36,0x1c,0}},
    {'P',{0x3f,0x66,0x66,0x3e,0x06,0x06,0x0f,0}},
    {'Q',{0x1e,0x33,0x33,0x33,0x3b,0x1e,0x38,0}},
    {'R',{0x3f,0x66,0x66,0x3e,0x36,0x66,0x67,0}},
    {'S',{0x1e,0x33,0x07,0x0e,0x38,0x33,0x1e,0}},
    {'T',{0x3f,0x2d,0x0c,0x0c,0x0c,0x0c,0x1e,0}},
    {'U',{0x33,0x33,0x33,0x33,0x33,0x33,0x3f,0}},
    {'V',{0x33,0x33,0x33,0x33,0x33,0x1e,0x0c,0}},
    {'W',{0x63,0x63,0x63,0x6b,0x7f,0x77,0x63,0}},
    {'X',{0x63,0x63,0x36,0x1c,0x36,0x63,0x63,0}},
    {'Y',{0x33,0x33,0x33,0x1e,0x0c,0x0c,0x1e,0}},
    {'Z',{0x7f,0x63,0x31,0x18,0x4c,0x66,0x7f,0}},
    {'a',{0,0,0x1e,0x30,0x3e,0x33,0x6e,0}},
    {'b',{0x07,0x06,0x06,0x3e,0x66,0x66,0x3b,0}},
    {'c',{0,0,0x1e,0x03,0x03,0x33,0x1e,0}},
    {'d',{0x38,0x30,0x30,0x3e,0x33,0x33,0x6e,0}},
    {'e',{0,0,0x1e,0x33,0x3f,0x03,0x1e,0}},
    {'f',{0x1c,0x36,0x06,0x0f,0x06,0x06,0x0f,0}},
    {'g',{0,0,0x6e,0x33,0x33,0x3e,0x30,0x1f}},
    {'h',{0x07,0x06,0x36,0x6e,0x66,0x66,0x67,0}},
    {'i',{0x0c,0,0x0e,0x0c,0x0c,0x0c,0x1e,0}},
    {'j',{0x30,0,0x38,0x30,0x30,0x33,0x33,0x1e}},
    {'k',{0x07,0x06,0x66,0x36,0x1e,0x36,0x67,0}},
    {'l',{0x0e,0x0c,0x0c,0x0c,0x0c,0x0c,0x1e,0}},
    {'m',{0,0,0x33,0x7f,0x7f,0x6b,0x63,0}},
    {'n',{0,0,0x1f,0x33,0x33,0x33,0x33,0}},
    {'o',{0,0,0x1e,0x33,0x33,0x33,0x1e,0}},
    {'p',{0,0,0x3b,0x66,0x66,0x3e,0x06,0x0f}},
    {'q',{0,0,0x6e,0x33,0x33,0x3e,0x30,0x78}},
    {'r',{0,0,0x1b,0x37,0x33,0x03,0x07,0}},
    {'s',{0,0,0x3e,0x03,0x1e,0x30,0x1f,0}},
    {'t',{0x08,0x0c,0x3e,0x0c,0x0c,0x2c,0x18,0}},
    {'u',{0,0,0x33,0x33,0x33,0x33,0x6e,0}},
    {'v',{0,0,0x33,0x33,0x33,0x1e,0x0c,0}},
    {'w',{0,0,0x63,0x6b,0x7f,0x7f,0x36,0}},
    {'x',{0,0,0x63,0x36,0x1c,0x36,0x63,0}},
    {'y',{0,0,0x33,0x33,0x33,0x3e,0x30,0x1f}},
    {'z',{0,0,0x3f,0x19,0x0c,0x26,0x3f,0}}
};

static void color(SDL_Renderer *r, uint8_t red, uint8_t green, uint8_t blue) {
    SDL_SetRenderDrawColor(r, red, green, blue, 255);
}

static const uint8_t *glyph_for(char c) {
    size_t i;
    c = (char)toupper((unsigned char)c);
    for (i = 0; i < sizeof(font) / sizeof(font[0]); ++i)
        if (font[i].c == c) return font[i].row;
    return font[0].row;
}

static const uint8_t *pc_glyph_for(char c) {
    size_t i;
    for (i = 0; i < sizeof(pc_font) / sizeof(pc_font[0]); ++i)
        if (pc_font[i].c == c) return pc_font[i].row;
    return pc_font[0].row;
}

static void draw_pc_text(Game *g, int x, int y, const char *s,
                         uint8_t red, uint8_t green, uint8_t blue) {
    color(g->renderer, red, green, blue);
    for (; *s; ++s, x += 8) {
        const uint8_t *rows = pc_glyph_for(*s);
        for (int yy = 0; yy < 8; ++yy)
            for (int xx = 0; xx < 8; ++xx)
                if (rows[yy] & (1u << xx))
                    SDL_RenderDrawPoint(g->renderer, x + xx, y + yy);
    }
}

static void draw_text(Game *g, int x, int y, int scale, const char *s,
                      uint8_t red, uint8_t green, uint8_t blue) {
    SDL_Rect p = {0, 0, scale, scale};
    color(g->renderer, red, green, blue);
    for (; *s; ++s, x += 6 * scale) {
        const uint8_t *rows = glyph_for(*s);
        for (int yy = 0; yy < 7; ++yy)
            for (int xx = 0; xx < 5; ++xx)
                if (rows[yy] & (1u << (4 - xx))) {
                    p.x = x + xx * scale;
                    p.y = y + yy * scale;
                    SDL_RenderFillRect(g->renderer, &p);
                }
    }
}

static int text_width(const char *s, int scale) {
    return (int)strlen(s) * 6 * scale - scale;
}

static void center_text(Game *g, int y, int scale, const char *s,
                        uint8_t red, uint8_t green, uint8_t blue) {
    draw_text(g, (SCREEN_W - text_width(s, scale)) / 2, y, scale, s,
              red, green, blue);
}

static uint32_t random_u32(Game *g) {
    uint32_t x = g->rng ? g->rng : 0x6d2b79f5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return g->rng = x;
}

static int random_n(Game *g, int n) {
    return n > 0 ? (int)(random_u32(g) % (uint32_t)n) : 0;
}

static void beep(Game *g, float hz, int milliseconds) {
    if (!g->audio) return;
    const int rate = 22050;
    int count = rate * milliseconds / 1000;
    float *samples = malloc((size_t)count * sizeof(*samples));
    if (!samples) return;
    for (int i = 0; i < count; ++i) {
        float fade = 1.0f - (float)i / (float)count;
        samples[i] = sinf(6.2831853f * hz * (float)i / (float)rate) * .11f * fade;
    }
    if (SDL_GetQueuedAudioSize(g->audio) < (uint32_t)(rate * sizeof(float)))
        SDL_QueueAudio(g->audio, samples, (uint32_t)count * sizeof(*samples));
    free(samples);
}

static void default_scores(Game *g) {
    memset(g->scores, 0, sizeof(g->scores));
    for (int i = 0; i < MAX_SCORES; ++i) g->scores[i].score = -1;
}

static void load_scores(Game *g) {
    char line[128];
    FILE *fp;
    default_scores(g);
    fp = fopen(SCORE_FILE, "rb");
    if (!fp) return;
    for (int i = 0; i < MAX_SCORES && fgets(line, sizeof(line), fp); ++i) {
        char name[4] = "";
        int score, level;
        if (sscanf(line, "\"%3[^\"]\",%d,%d", name, &score, &level) == 3) {
            memcpy(g->scores[i].name, name, sizeof(name));
            g->scores[i].name[3] = '\0';
            g->scores[i].score = score;
            g->scores[i].level = level;
        }
    }
    fclose(fp);
}

static void save_scores(const Game *g) {
    FILE *fp = fopen(SCORE_FILE, "wb");
    if (!fp) {
        fprintf(stderr, "gapper: cannot write %s\n", SCORE_FILE);
        return;
    }
    for (int i = 0; i < MAX_SCORES; ++i)
        fprintf(fp, "\"%s\",%d,%d\r\n", g->scores[i].name,
                g->scores[i].score, g->scores[i].level);
    fputc(0x1a, fp);
    fclose(fp);
}

static void record_score(Game *g) {
    ScoreEntry entry;
    memcpy(entry.name, g->initials, sizeof(entry.name));
    entry.score = g->score;
    entry.level = g->level;
    for (int i = 0; i < MAX_SCORES; ++i) {
        if (entry.score > g->scores[i].score) {
            ScoreEntry displaced = g->scores[i];
            g->scores[i] = entry;
            entry = displaced;
        }
    }
    save_scores(g);
}

static void board_edges(Board *b) {
    memset(b->h, 0, sizeof(b->h));
    memset(b->v, 0, sizeof(b->v));
    b->total_cells = 0;
    b->completed_cells = 0;
    memset(b->complete, 0, sizeof(b->complete));
    for (int y = 0; y < b->rows; ++y)
        for (int x = 0; x < b->cols; ++x)
            if (b->cell[y][x]) {
                ++b->total_cells;
                b->h[y][x].exists = b->h[y + 1][x].exists = 1;
                b->v[y][x].exists = b->v[y][x + 1].exists = 1;
            }
    for (int y = 0; y <= b->rows; ++y)
        for (int x = 0; x < b->cols; ++x)
            b->h[y][x].paint_high = 1.0;
    for (int y = 0; y < b->rows; ++y)
        for (int x = 0; x <= b->cols; ++x)
            b->v[y][x].paint_high = 1.0;
}

static int cells_connected(const Board *b) {
    int seen[MAX_ROWS][MAX_COLS] = {{0}};
    int qx[MAX_ROWS * MAX_COLS], qy[MAX_ROWS * MAX_COLS];
    int head = 0, tail = 0, active = 0, reached = 0;
    static const int ox[4] = {0, -1, 0, 1};
    static const int oy[4] = {-1, 0, 1, 0};

    for (int y = 0; y < b->rows; ++y)
        for (int x = 0; x < b->cols; ++x)
            if (b->cell[y][x]) {
                ++active;
                if (!tail) {
                    qx[tail] = x;
                    qy[tail++] = y;
                    seen[y][x] = 1;
                }
            }
    while (head < tail) {
        int x = qx[head], y = qy[head++];
        ++reached;
        for (int i = 0; i < 4; ++i) {
            int nx = x + ox[i], ny = y + oy[i];
            if (nx >= 0 && nx < b->cols && ny >= 0 && ny < b->rows &&
                b->cell[ny][nx] && !seen[ny][nx]) {
                seen[ny][nx] = 1;
                qx[tail] = nx;
                qy[tail++] = ny;
            }
        }
    }
    return active > 0 && reached == active;
}

static void make_level(Game *g) {
    Board *b = &g->board;
    memset(b, 0, sizeof(*b));
    if (g->level == 1) { b->cols = 3; b->rows = 3; }
    else if (g->level == 2) { b->cols = 4; b->rows = 4; }
    else if (g->level == 3) { b->cols = 5; b->rows = 5; }
    else if (g->level == 4) { b->cols = 6; b->rows = 7; }
    else { b->cols = 8; b->rows = 7; }
    for (int y = 0; y < b->rows; ++y)
        for (int x = 0; x < b->cols; ++x) b->cell[y][x] = 1;

    /* Levels five and above use the original T-shaped 8x7 mask: four full
       rows followed by three centered rows of four cells. */
    if (g->level >= 5)
        for (int y = 4; y < 7; ++y)
            for (int x = 0; x < 8; ++x)
                if (x < 2 || x > 5) b->cell[y][x] = 0;
    board_edges(b);
}

static Edge *edge_between(Board *b, int x1, int y1, int x2, int y2) {
    if (x1 == x2 && abs(y2 - y1) == 1) {
        int y = y1 < y2 ? y1 : y2;
        if (x1 >= 0 && x1 <= b->cols && y >= 0 && y < b->rows)
            return &b->v[y][x1];
    } else if (y1 == y2 && abs(x2 - x1) == 1) {
        int x = x1 < x2 ? x1 : x2;
        if (x >= 0 && x < b->cols && y1 >= 0 && y1 <= b->rows)
            return &b->h[y1][x];
    }
    return NULL;
}

static void dir_delta(Direction d, int *dx, int *dy) {
    *dx = *dy = 0;
    if (d == DIR_UP) --*dy;
    else if (d == DIR_LEFT) --*dx;
    else if (d == DIR_DOWN) ++*dy;
    else if (d == DIR_RIGHT) ++*dx;
}

static Direction opposite(Direction d) {
    if (d == DIR_UP) return DIR_DOWN;
    if (d == DIR_DOWN) return DIR_UP;
    if (d == DIR_LEFT) return DIR_RIGHT;
    if (d == DIR_RIGHT) return DIR_LEFT;
    return DIR_NONE;
}

static void layout(const Board *b, int *x0, int *y0, int *dx, int *dy);
static void grid_to_screen(const Board *b, double gx, double gy, int *x, int *y);

static int can_move(Game *g, int x, int y, Direction d) {
    int dx, dy;
    Edge *e;
    dir_delta(d, &dx, &dy);
    e = edge_between(&g->board, x, y, x + dx, y + dy);
    if (!e || !e->exists) return 0;
    if (e->blocked_until > g->now) {
        int from_low = dx > 0 || dy > 0;
        double start = from_low ? 0.0 : 1.0;
        if (fabs(e->gap_at - start) < .0001) return 0;
    }
    return 1;
}

static int actor_start(Game *g, Actor *a, Direction d) {
    int dx, dy;
    if (a->dir != DIR_NONE || !can_move(g, a->gx, a->gy, d)) return 0;
    dir_delta(d, &dx, &dy);
    a->from_x = a->gx;
    a->from_y = a->gy;
    a->tx = a->gx + dx;
    a->ty = a->gy + dy;
    a->dir = d;
    a->progress = 0.0;
    a->waiting_at_gap = 0;
    return 1;
}

static void find_start_vertices(Board *b, Actor *player, Actor *seeker) {
    int found = 0;
    memset(player, 0, sizeof(*player));
    memset(seeker, 0, sizeof(*seeker));
    player->dir = seeker->dir = DIR_NONE;
    for (int y = 0; y <= b->rows; ++y)
        for (int x = 0; x <= b->cols; ++x) {
            int degree = 0;
            if (x < b->cols && b->h[y][x].exists) ++degree;
            if (x > 0 && b->h[y][x - 1].exists) ++degree;
            if (y < b->rows && b->v[y][x].exists) ++degree;
            if (y > 0 && b->v[y - 1][x].exists) ++degree;
            if (degree && !found) {
                player->gx = player->tx = x;
                player->gy = player->ty = y;
                found = 1;
            }
            if (degree) {
                seeker->gx = seeker->tx = x;
                seeker->gy = seeker->ty = y;
            }
        }
}

static void pick_bonus(Game *g) {
    Board *b = &g->board;
    int choices[MAX_ROWS * MAX_COLS][2], count = 0;
    for (int y = 0; y < b->rows; ++y)
        for (int x = 0; x < b->cols; ++x)
            if (b->cell[y][x] && !b->complete[y][x]) {
                choices[count][0] = x;
                choices[count++][1] = y;
            }
    if (count) {
        int n = random_n(g, count);
        g->bonus_x = choices[n][0];
        g->bonus_y = choices[n][1];
        g->bonus_on = 1;
        g->bonus_until = g->now + 3.5;
    } else g->bonus_on = 0;
}

static void start_level(Game *g) {
    make_level(g);
    find_start_vertices(&g->board, &g->player, &g->seeker);
    g->gap_x1 = g->gap_y1 = g->gap_x2 = g->gap_y2 = -1;
    g->bonus_on = 0;
    g->bonus_until = g->now + 1.5;
    g->state = ST_READY;
}

static void new_game(Game *g) {
    g->score = 0;
    g->level = 1;
    g->lives = 2;
    g->next_life = 5000;
    start_level(g);
}

static void check_cells(Game *g) {
    Board *b = &g->board;
    for (int y = 0; y < b->rows; ++y)
        for (int x = 0; x < b->cols; ++x)
            if (b->cell[y][x] && !b->complete[y][x] &&
                b->h[y][x].painted && b->h[y + 1][x].painted &&
                b->v[y][x].painted && b->v[y][x + 1].painted) {
                b->complete[y][x] = 1;
                ++b->completed_cells;
                if (g->bonus_on && x == g->bonus_x && y == g->bonus_y) {
                    g->score += 100;
                    g->bonus_on = 0;
                    g->bonus_until = g->now + 1.0;
                    beep(g, 880.0f, 100);
                } else {
                    g->score += 50;
                    beep(g, 520.0f, 55);
                }
                while (g->score >= g->next_life) {
                    ++g->lives;
                    g->next_life += 5000;
                    beep(g, 1040.0f, 180);
                }
            }
    if (b->completed_cells == b->total_cells && g->state == ST_PLAY) {
        g->state = ST_LEVEL_DONE;
        g->state_until = g->now + 1.4;
        beep(g, 660.0f, 120);
    }
}

static Direction seeker_direction(Game *g) {
    int sx = g->seeker.gx, sy = g->seeker.gy;
    int target_x = g->player.dir == DIR_NONE ? g->player.gx : g->player.tx;
    int target_y = g->player.dir == DIR_NONE ? g->player.gy : g->player.ty;
    int dx = target_x - sx, dy = target_y - sy;
    Direction horizontal = dx < 0 ? DIR_LEFT : dx > 0 ? DIR_RIGHT : DIR_NONE;
    Direction vertical = dy < 0 ? DIR_UP : dy > 0 ? DIR_DOWN : DIR_NONE;

    /* The BASIC original does not perform a maze search. At each decision it
       selects the dominant coordinate toward Gapper and attempts that move.
       If a gap cuts that line the Seeker waits instead of routing around it. */
    if (abs(dx) >= abs(dy) && horizontal != DIR_NONE) return horizontal;
    if (vertical != DIR_NONE) return vertical;
    return horizontal;
}

static void actor_position(const Actor *a, double *x, double *y) {
    *x = (double)a->from_x + ((double)a->tx - a->from_x) * a->progress;
    *y = (double)a->from_y + ((double)a->ty - a->from_y) * a->progress;
    if (a->dir == DIR_NONE) { *x = a->gx; *y = a->gy; }
}

static void paint_actor_edge(Game *g, const Actor *a, double old_progress) {
    Edge *e = edge_between(&g->board, a->from_x, a->from_y, a->tx, a->ty);
    double p = a->progress > 1.0 ? 1.0 : a->progress;
    double t0, t1, low, high;
    int from_low;
    if (!e) return;
    from_low = a->from_x != a->tx ? a->from_x < a->tx : a->from_y < a->ty;
    t0 = from_low ? old_progress : 1.0 - old_progress;
    t1 = from_low ? p : 1.0 - p;
    low = t0 < t1 ? t0 : t1;
    high = t0 > t1 ? t0 : t1;
    if (low <= e->paint_low + .0001 && high > e->paint_low) e->paint_low = high;
    if (high >= e->paint_high - .0001 && low < e->paint_high) e->paint_high = low;
    if (e->paint_low >= e->paint_high - .0001) {
        e->painted = 1;
        e->paint_low = 1.0;
        e->paint_high = 0.0;
    }
}

static void advance_actor(Game *g, Actor *a, double dt, double pixels_per_second,
                          int is_player) {
    int x0, y0, dx, dy;
    double edge_length, old_progress;
    if (a->dir == DIR_NONE) {
        Direction next = is_player ? a->queued : seeker_direction(g);
        if (next != DIR_NONE && actor_start(g, a, next) && is_player)
            a->queued = DIR_NONE;
        else if (is_player && a->queued != DIR_NONE &&
                 actor_start(g, a, a->dir)) a->queued = DIR_NONE;
        return;
    }
    layout(&g->board, &x0, &y0, &dx, &dy);
    (void)x0; (void)y0;
    edge_length = a->from_y == a->ty ? dx : dy;
    Edge *e = edge_between(&g->board, a->from_x, a->from_y, a->tx, a->ty);
    if (a->waiting_at_gap) {
        if (e && e->blocked_until > g->now) return;
        a->waiting_at_gap = 0;
    }
    old_progress = a->progress;
    a->progress += dt * pixels_per_second / edge_length;
    if (e && e->blocked_until > g->now) {
        int from_low = a->from_x != a->tx ? a->from_x < a->tx : a->from_y < a->ty;
        double old_canonical = from_low ? old_progress : 1.0 - old_progress;
        double new_canonical = from_low ? a->progress : 1.0 - a->progress;
        int hits_gap = new_canonical > old_canonical ?
            e->gap_at > old_canonical + .0001 && e->gap_at <= new_canonical + .0001 :
            e->gap_at < old_canonical - .0001 && e->gap_at >= new_canonical - .0001;
        if (hits_gap) {
            a->progress = from_low ? e->gap_at : 1.0 - e->gap_at;
            a->waiting_at_gap = 1;
        }
    }
    if (is_player) paint_actor_edge(g, a, old_progress);
    if (a->waiting_at_gap) return;
    if (a->progress < 1.0) return;
    if (is_player && e) {
        g->gap_x1 = a->from_x; g->gap_y1 = a->from_y;
        g->gap_x2 = a->tx; g->gap_y2 = a->ty;
    }
    a->gx = a->tx; a->gy = a->ty; a->progress = 0.0;
    Direction old = a->dir;
    a->dir = DIR_NONE;
    if (is_player) {
        check_cells(g);
        if (a->queued != DIR_NONE && actor_start(g, a, a->queued)) a->queued = DIR_NONE;
        else actor_start(g, a, old);
    } else {
        Direction next = seeker_direction(g);
        if (next == opposite(old)) {
            int dx = (g->player.dir == DIR_NONE ? g->player.gx : g->player.tx) - a->gx;
            int dy = (g->player.dir == DIR_NONE ? g->player.gy : g->player.ty) - a->gy;
            Direction alternate = abs(dx) >= abs(dy) ?
                (dy < 0 ? DIR_UP : dy > 0 ? DIR_DOWN : DIR_NONE) :
                (dx < 0 ? DIR_LEFT : dx > 0 ? DIR_RIGHT : DIR_NONE);
            if (alternate != DIR_NONE && alternate != opposite(old)) next = alternate;
        }
        actor_start(g, a, next);
    }
}

static void reverse_actor(Actor *a, Direction d) {
    int old_from_x = a->from_x, old_from_y = a->from_y;
    a->from_x = a->tx; a->from_y = a->ty;
    a->tx = old_from_x; a->ty = old_from_y;
    a->gx = a->from_x; a->gy = a->from_y;
    a->progress = 1.0 - a->progress;
    a->dir = d;
    a->queued = DIR_NONE;
    a->waiting_at_gap = 0;
}

static void kill_player(Game *g) {
    if (g->state != ST_PLAY) return;
    g->state = ST_DEAD;
    g->state_until = g->now + 1.25;
    beep(g, 110.0f, 350);
}

static void update_game(Game *g, double dt) {
    if (g->state == ST_PLAY) {
        if (g->bonus_on && g->now >= g->bonus_until) {
            g->bonus_on = 0;
            g->bonus_until = g->now + 1.4;
        } else if (!g->bonus_on && g->now >= g->bonus_until) pick_bonus(g);
        while (dt > 0.0 && g->state == ST_PLAY) {
            double step = dt > 1.0 / 120.0 ? 1.0 / 120.0 : dt;
            double pgx, pgy, sgx, sgy;
            int px, py, sx, sy;
            double seeker_speed = 105.0 + 12.0 * g->level;
            if (seeker_speed > 220.0) seeker_speed = 220.0;
            advance_actor(g, &g->player, step, 190.0 * g->speed_scale, 1);
            advance_actor(g, &g->seeker, step, seeker_speed * g->speed_scale, 0);
            actor_position(&g->player, &pgx, &pgy);
            actor_position(&g->seeker, &sgx, &sgy);
            grid_to_screen(&g->board, pgx, pgy, &px, &py);
            grid_to_screen(&g->board, sgx, sgy, &sx, &sy);
            if (abs(px - sx) <= 6 && abs(py - sy) <= 6) kill_player(g);
            dt -= step;
        }
    } else if (g->state == ST_DEAD && g->now >= g->state_until) {
        if (g->lives-- > 0) {
            find_start_vertices(&g->board, &g->player, &g->seeker);
            g->state = ST_READY;
        } else {
            record_score(g);
            g->state = ST_GAME_OVER;
            g->state_until = g->now + 1.5;
        }
    } else if (g->state == ST_LEVEL_DONE && g->now >= g->state_until) {
        ++g->level;
        start_level(g);
    }
}

static void layout(const Board *b, int *x0, int *y0, int *dx, int *dy) {
    if (b->cols == 3) { *x0 = 4; *y0 = 10; *dx = 104; *dy = 60; }
    else if (b->cols == 4) { *x0 = 8; *y0 = 12; *dx = 76; *dy = 44; }
    else if (b->cols == 5) { *x0 = 10; *y0 = 10; *dx = 60; *dy = 36; }
    else if (b->cols == 6) { *x0 = 16; *y0 = 16; *dx = 48; *dy = 24; }
    else { *x0 = 16; *y0 = 16; *dx = 36; *dy = 24; }
}

static void grid_to_screen(const Board *b, double gx, double gy, int *x, int *y) {
    int x0, y0, dx, dy;
    layout(b, &x0, &y0, &dx, &dy);
    *x = x0 + (int)lround(gx * dx);
    *y = y0 + (int)lround(gy * dy);
}

static void draw_edge(Game *g, int x1, int y1, int x2, int y2, const Edge *e) {
    int sx1, sy1, sx2, sy2, lx, ly, hx, hy;
    grid_to_screen(&g->board, x1, y1, &sx1, &sy1);
    grid_to_screen(&g->board, x2, y2, &sx2, &sy2);
    color(g->renderer, 0, 235, 235);
    SDL_RenderDrawLine(g->renderer, sx1, sy1, sx2, sy2);
    color(g->renderer, 255, 70, 235);
    lx = sx1 + (int)lround((sx2 - sx1) * e->paint_low);
    ly = sy1 + (int)lround((sy2 - sy1) * e->paint_low);
    hx = sx1 + (int)lround((sx2 - sx1) * e->paint_high);
    hy = sy1 + (int)lround((sy2 - sy1) * e->paint_high);
    if (e->paint_low > 0.0) SDL_RenderDrawLine(g->renderer, sx1, sy1, lx, ly);
    if (e->paint_high < 1.0) SDL_RenderDrawLine(g->renderer, hx, hy, sx2, sy2);
    if (e->blocked_until > g->now) {
        color(g->renderer, 0, 0, 0);
        int mx = sx1 + (int)lround((sx2 - sx1) * e->gap_at);
        int my = sy1 + (int)lround((sy2 - sy1) * e->gap_at);
        if (sx1 == sx2) SDL_RenderDrawLine(g->renderer, mx, my - 3, mx, my + 3);
        else SDL_RenderDrawLine(g->renderer, mx - 4, my, mx + 4, my);
    }
}

static void draw_actor(Game *g, const Actor *a, int seeker) {
    double gx, gy;
    int x, y;
    actor_position(a, &gx, &gy);
    grid_to_screen(&g->board, gx, gy, &x, &y);
    if (seeker) {
        color(g->renderer, 255, 235, 0);
        SDL_RenderDrawLine(g->renderer, x - 3, y - 3, x + 3, y + 3);
        SDL_RenderDrawLine(g->renderer, x + 3, y - 3, x - 3, y + 3);
        SDL_RenderDrawPoint(g->renderer, x, y);
    } else {
        color(g->renderer, 255, 70, 235);
        SDL_RenderDrawLine(g->renderer, x, y - 4, x, y + 3);
        SDL_RenderDrawLine(g->renderer, x - 2, y - 2, x + 2, y - 2);
        SDL_RenderDrawLine(g->renderer, x - 3, y + 4, x, y + 1);
        SDL_RenderDrawLine(g->renderer, x + 3, y + 4, x, y + 1);
        color(g->renderer, 0, 235, 235);
        SDL_RenderDrawPoint(g->renderer, x - 1, y - 3);
        SDL_RenderDrawPoint(g->renderer, x + 1, y - 3);
    }
}

static void draw_board(Game *g) {
    Board *b = &g->board;
    int x0, y0, dx, dy;
    layout(b, &x0, &y0, &dx, &dy);
    for (int y = 0; y < b->rows; ++y)
        for (int x = 0; x < b->cols; ++x) {
            if (b->complete[y][x] ||
                (g->bonus_on && x == g->bonus_x && y == g->bonus_y)) {
                SDL_Rect r = {x0 + x * dx + 2, y0 + y * dy + 2, dx - 3, dy - 3};
                color(g->renderer, 255, 255, 255);
                SDL_RenderFillRect(g->renderer, &r);
                const char *points = (g->bonus_on && x == g->bonus_x &&
                                      y == g->bonus_y) ? "100" : "50";
                draw_text(g, x0 + x * dx + (dx - text_width(points, 1)) / 2,
                          y0 + y * dy + (dy - 7) / 2, 1, points, 0, 0, 0);
            }
        }
    for (int y = 0; y <= b->rows; ++y)
        for (int x = 0; x < b->cols; ++x)
            if (b->h[y][x].exists) draw_edge(g, x, y, x + 1, y, &b->h[y][x]);
    for (int y = 0; y < b->rows; ++y)
        for (int x = 0; x <= b->cols; ++x)
            if (b->v[y][x].exists) draw_edge(g, x, y, x, y + 1, &b->v[y][x]);
    draw_actor(g, &g->player, 0);
    draw_actor(g, &g->seeker, 1);
    char status[32];
    snprintf(status, sizeof(status), "Score[%6d]", g->score);
    draw_pc_text(g, 7, 192, status, 255, 255, 255);
    snprintf(status, sizeof(status), "Lives[%d]", g->lives);
    draw_pc_text(g, 144, 192, status, 255, 255, 255);
    snprintf(status, sizeof(status), "Level[%2d]", g->level);
    draw_pc_text(g, 240, 192, status, 255, 255, 255);
    if (g->gap_x1 >= 0) {
        Edge *e = edge_between(b, g->gap_x1, g->gap_y1, g->gap_x2, g->gap_y2);
        if (e && e->blocked_until > g->now) draw_text(g, 145, 177, 1, "GAP ON", 255, 235, 0);
    }
    if (g->state == ST_READY) {
        SDL_Rect banner = {84, 0, 152, 8};
        color(g->renderer, 255, 85, 255); SDL_RenderFillRect(g->renderer, &banner);
        draw_pc_text(g, 136, 0, "GAPPER", 85, 255, 255);
    } else if (g->state == ST_DEAD) {
        center_text(g, 91, 2, "CAUGHT!", 255, 235, 0);
    } else if (g->state == ST_LEVEL_DONE) {
        center_text(g, 91, 2, "LEVEL CLEAR", 255, 235, 0);
    }
}

static void draw_title(Game *g) {
    char line[80];
    SDL_Rect bar = {0, 0, 288, 8};
    color(g->renderer, 170, 85, 0); SDL_RenderFillRect(g->renderer, &bar);
    draw_pc_text(g, 120, 0, "GAPPER", 85, 255, 255);
    draw_pc_text(g, 0, 16, "High Scores:", 170, 170, 170);
    for (int i = 0; i < 5; ++i) {
        if (g->scores[i].score >= 0)
            snprintf(line, sizeof(line), "%3s    %6d    %2d",
                     g->scores[i].name, g->scores[i].score, g->scores[i].level);
        else snprintf(line, sizeof(line), "             0     0");
        draw_pc_text(g, 72, 32 + i * 8, line, 255, 255, 85);
    }
    snprintf(line, sizeof(line), "Score: %6d.             Level: %2d", g->score, g->level);
    draw_pc_text(g, 0, 128, line, 170, 85, 0);
    draw_pc_text(g, 80, 144, "P: Play a game.", 255, 255, 85);
    draw_pc_text(g, 80, 152, "Q: Quit", 255, 255, 85);
    draw_pc_text(g, 80, 160, "N: New Player", 255, 255, 85);
    draw_pc_text(g, 80, 168, "I: Instructions", 255, 255, 85);
    if (g->state == ST_NAME) {
        snprintf(line, sizeof(line), "Enter your initials: %s", g->initials);
        draw_pc_text(g, 0, 192, line, 255, 85, 255);
        if (((int)(g->now * 2.0) & 1) == 0)
            draw_pc_text(g, (int)strlen(line) * 8, 192, "_", 170, 170, 170);
    } else if (g->state == ST_GAME_OVER) {
        draw_pc_text(g, 56, 192, "GAME OVER - PRESS A KEY", 255, 85, 255);
    } else {
        const char *prompt = "Please enter selection:";
        draw_pc_text(g, 0, 192, prompt, 255, 85, 255);
        if (((int)(g->now * 2.0) & 1) == 0)
            draw_pc_text(g, (int)strlen(prompt) * 8, 192, "_", 170, 170, 170);
    }
}

static void draw_help(Game *g) {
    if (g->state == ST_HELP1) {
        center_text(g, 6, 1, "OBJECT", 255, 235, 0);
        center_text(g, 18, 1, "COLOR ALL BLUE LINES RED BY", 0, 235, 235);
        center_text(g, 27, 1, "MOVE WITH ARROWS OR WASD.", 0, 235, 235);
        center_text(g, 36, 1, "AVOID THE SEEKER.", 0, 235, 235);
        center_text(g, 54, 1, "POINTS", 255, 235, 0);
        center_text(g, 66, 1, "50 FOR EACH SURROUNDED BOX.", 0, 235, 235);
        center_text(g, 75, 1, "100 FOR THE LIT POINT BOX.", 0, 235, 235);
        center_text(g, 84, 1, "EACH GAP COSTS 5 POINTS.", 0, 235, 235);
        center_text(g, 105, 1, "GAPPING", 255, 235, 0);
        center_text(g, 117, 1, "SPACE OR RETURN LEAVES A", 0, 235, 235);
        center_text(g, 126, 1, "TEMPORARY GAP. NOBODY CAN", 0, 235, 235);
        center_text(g, 135, 1, "CROSS IT WHILE GAP ON SHOWS.", 0, 235, 235);
        center_text(g, 181, 1, "PRESS A KEY...", 255, 70, 235);
    } else {
        center_text(g, 11, 1, "LEVELS", 255, 235, 0);
        center_text(g, 25, 1, "EACH SCREEN IS FASTER, WITH", 0, 235, 235);
        center_text(g, 34, 1, "MORE BOXES AND SHORTER GAPS.", 0, 235, 235);
        center_text(g, 59, 1, "LIVES", 255, 235, 0);
        center_text(g, 73, 1, "YOU START WITH 2 EXTRA MEN.", 0, 235, 235);
        center_text(g, 82, 1, "GAIN ONE EACH 5000 POINTS.", 0, 235, 235);
        center_text(g, 107, 1, "START", 255, 235, 0);
        center_text(g, 121, 1, "GAPPER STARTS AT UPPER LEFT,", 0, 235, 235);
        center_text(g, 130, 1, "THE SEEKER AT LOWER RIGHT.", 0, 235, 235);
        center_text(g, 139, 1, "PRESS A KEY FOR EACH SCREEN.", 0, 235, 235);
        center_text(g, 181, 1, "PRESS A KEY...", 255, 70, 235);
    }
}

static void render(Game *g) {
    color(g->renderer, 0, 0, 0);
    SDL_RenderClear(g->renderer);
    if (g->state == ST_HELP1 || g->state == ST_HELP2) draw_help(g);
    else if (g->state == ST_READY || g->state == ST_PLAY ||
             g->state == ST_DEAD || g->state == ST_LEVEL_DONE) draw_board(g);
    else draw_title(g);
    SDL_RenderPresent(g->renderer);
}

static Direction key_direction(SDL_Keycode key) {
    if (key == SDLK_UP || key == SDLK_w) return DIR_UP;
    if (key == SDLK_LEFT || key == SDLK_a) return DIR_LEFT;
    if (key == SDLK_DOWN || key == SDLK_s) return DIR_DOWN;
    if (key == SDLK_RIGHT || key == SDLK_d) return DIR_RIGHT;
    return DIR_NONE;
}

static void create_gap(Game *g) {
    Edge *e;
    double at = .5;
    int x0, y0, dx, dy;
    if (g->gap_x1 >= 0) {
        Edge *active = edge_between(&g->board, g->gap_x1, g->gap_y1,
                                    g->gap_x2, g->gap_y2);
        if (active && active->blocked_until > g->now) return;
    }
    layout(&g->board, &x0, &y0, &dx, &dy);
    (void)x0; (void)y0;
    if (g->player.dir != DIR_NONE) {
        double inset = 4.0 / (g->player.from_y == g->player.ty ? dx : dy);
        g->gap_x1 = g->player.from_x; g->gap_y1 = g->player.from_y;
        g->gap_x2 = g->player.tx; g->gap_y2 = g->player.ty;
        if (g->player.from_x != g->player.tx)
            at = g->player.from_x < g->player.tx ? g->player.progress - inset :
                 1.0 - g->player.progress + inset;
        else
            at = g->player.from_y < g->player.ty ? g->player.progress - inset :
                 1.0 - g->player.progress + inset;
        if (at < 0.0) at = 0.0;
        if (at > 1.0) at = 1.0;
    } else if (g->gap_x1 >= 0) {
        double inset = 4.0 / (g->gap_y1 == g->gap_y2 ? dx : dy);
        int at_low = g->gap_x1 != g->gap_x2 ?
            g->player.gx == (g->gap_x1 < g->gap_x2 ? g->gap_x1 : g->gap_x2) :
            g->player.gy == (g->gap_y1 < g->gap_y2 ? g->gap_y1 : g->gap_y2);
        at = at_low ? inset : 1.0 - inset;
    }
    if (g->gap_x1 < 0) return;
    e = edge_between(&g->board, g->gap_x1, g->gap_y1, g->gap_x2, g->gap_y2);
    if (!e) return;
    e->blocked_until = g->now + fmax(1.1, 4.2 - .22 * g->level);
    e->gap_at = at;
    g->score -= 5;
    beep(g, 190.0f, 80);
}

static void handle_key(Game *g, SDL_KeyboardEvent *event) {
    SDL_Keycode key = event->keysym.sym;
    if (event->repeat) return;
    if (g->state == ST_NAME) {
        if (key == SDLK_BACKSPACE && g->initials_len > 0)
            g->initials[--g->initials_len] = '\0';
        else if (key == SDLK_RETURN && g->initials_len > 0) g->state = ST_MENU;
        else if (g->initials_len < 3 && key >= SDLK_a && key <= SDLK_z) {
            g->initials[g->initials_len++] = (char)toupper((int)key);
            g->initials[g->initials_len] = '\0';
        }
        return;
    }
    if (g->state == ST_MENU) {
        if (key == SDLK_p) new_game(g);
        else if (key == SDLK_q || key == SDLK_ESCAPE) g->running = 0;
        else if (key == SDLK_n) {
            g->initials_len = 0; g->initials[0] = '\0'; g->state = ST_NAME;
        } else if (key == SDLK_i) g->state = ST_HELP1;
        return;
    }
    if (g->state == ST_HELP1) { g->state = ST_HELP2; return; }
    if (g->state == ST_HELP2) { g->state = ST_MENU; return; }
    if (g->state == ST_GAME_OVER && g->now >= g->state_until) {
        g->state = ST_MENU; return;
    }
    if (g->state == ST_READY) {
        g->state = ST_PLAY;
        Direction d = key_direction(key);
        if (d != DIR_NONE) g->player.queued = d;
        return;
    }
    if (g->state == ST_PLAY) {
        if (key == SDLK_ESCAPE) { g->state = ST_MENU; return; }
        Direction d = key_direction(key);
        if (d != DIR_NONE && g->player.dir != DIR_NONE &&
            d == opposite(g->player.dir)) reverse_actor(&g->player, d);
        else if (d != DIR_NONE) g->player.queued = d;
        else if (key == SDLK_SPACE || key == SDLK_RETURN) create_gap(g);
    }
}

static int self_test(void) {
    Game g;
    static const int expected[6][3] = {
        {3, 3, 9}, {4, 4, 16}, {5, 5, 25},
        {6, 7, 42}, {8, 7, 44}, {8, 7, 44}
    };
    memset(&g, 0, sizeof(g));
    g.rng = 1;
    g.level = 1;
    make_level(&g);
    if (g.board.cols != 3 || g.board.rows != 3 || g.board.total_cells != 9) return 1;
    find_start_vertices(&g.board, &g.player, &g.seeker);
    if (g.player.gx != 0 || g.player.gy != 0) return 2;
    Edge *e = edge_between(&g.board, 0, 0, 1, 0);
    if (!e || !e->exists) return 3;
    if (key_direction(SDLK_w) != DIR_UP || key_direction(SDLK_a) != DIR_LEFT ||
        key_direction(SDLK_s) != DIR_DOWN || key_direction(SDLK_d) != DIR_RIGHT) return 4;
    if (!actor_start(&g, &g.player, DIR_RIGHT)) return 5;
    advance_actor(&g, &g.player, .25 * 104.0 / 190.0, 190.0, 1);
    if (e->painted || fabs(e->paint_low - .25) > .001 || e->paint_high != 1.0) return 6;
    reverse_actor(&g.player, DIR_LEFT);
    advance_actor(&g, &g.player, .25 * 104.0 / 190.0, 190.0, 1);
    if (e->painted || fabs(e->paint_low - .25) > .001 || e->paint_high != 1.0) return 7;
    make_level(&g);
    find_start_vertices(&g.board, &g.player, &g.seeker);
    for (int y = 0; y <= g.board.rows; ++y)
        for (int x = 0; x < g.board.cols; ++x)
            if (g.board.h[y][x].exists) g.board.h[y][x].painted = 1;
    for (int y = 0; y < g.board.rows; ++y)
        for (int x = 0; x <= g.board.cols; ++x)
            if (g.board.v[y][x].exists) g.board.v[y][x].painted = 1;
    g.state = ST_PLAY; g.next_life = 5000;
    check_cells(&g);
    if (g.board.completed_cells != 9 || g.score != 450 || g.state != ST_LEVEL_DONE) return 8;
    g.level = 1; g.score = 0; g.next_life = 5000; g.now = 10.0;
    make_level(&g);
    g.board.h[0][0].painted = g.board.h[1][0].painted = 1;
    g.board.v[0][0].painted = g.board.v[0][1].painted = 1;
    g.bonus_on = 1; g.bonus_x = 0; g.bonus_y = 0; g.state = ST_PLAY;
    check_cells(&g);
    if (g.score != 100 || g.board.completed_cells != 1) return 9;
    start_level(&g); g.state = ST_PLAY; g.now = 10.0; g.score = 0;
    if (!actor_start(&g, &g.player, DIR_RIGHT)) return 10;
    g.player.progress = .5;
    create_gap(&g);
    if (g.score != -5) return 11;
    create_gap(&g);
    if (g.score != -5) return 12;
    reverse_actor(&g.player, DIR_LEFT);
    advance_actor(&g, &g.player, .1 * 104.0 / 190.0, 190.0, 1);
    if (!g.player.waiting_at_gap) return 13;
    for (g.level = 1; g.level <= 6; ++g.level) {
        make_level(&g);
        if (g.board.cols != expected[g.level - 1][0] ||
            g.board.rows != expected[g.level - 1][1] ||
            g.board.total_cells != expected[g.level - 1][2]) return 14;
    }
    for (uint32_t seed = 1; seed <= 100; ++seed)
        for (g.level = 1; g.level <= 40; ++g.level) {
            g.rng = seed;
            make_level(&g);
            if (g.board.cols < 3 || g.board.cols > MAX_COLS ||
                g.board.rows < 2 || g.board.rows > MAX_ROWS ||
                g.board.total_cells < 1 || !cells_connected(&g.board)) return 15;
            if (!g.board.cell[0][0]) return 16;
            find_start_vertices(&g.board, &g.player, &g.seeker);
            if (g.player.gx != 0 || g.player.gy != 0) return 17;
            if (g.level < 5 && (g.seeker.gx != g.board.cols ||
                               g.seeker.gy != g.board.rows)) return 18;
            if (g.level >= 5 && (g.seeker.gx != 6 || g.seeker.gy != 7)) return 19;
        }
    puts("gapper self-test: ok");
    return 0;
}

int main(int argc, char **argv) {
    Game g;
    uint64_t before;
    SDL_AudioSpec want;
    memset(&g, 0, sizeof(g));
    g.speed_scale = 1.0;
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0) return self_test();
    if (argc == 3 && strcmp(argv[1], "--speed") == 0) {
        char *end = NULL;
        g.speed_scale = strtod(argv[2], &end);
        if (!end || *end || g.speed_scale < .25 || g.speed_scale > 4.0) {
            fprintf(stderr, "usage: %s [--speed 0.25..4.0]\n", argv[0]);
            return 2;
        }
    } else if (argc != 1) {
        fprintf(stderr, "usage: %s [--speed 0.25..4.0]\n", argv[0]);
        return 2;
    }
    g.rng = (uint32_t)time(NULL) ^ 0x47415050u;
    g.running = 1;
    g.state = ST_NAME;
    load_scores(&g);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    g.window = SDL_CreateWindow("Gapper - C rewrite", SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, 960, 600,
                                SDL_WINDOW_RESIZABLE);
    g.renderer = SDL_CreateRenderer(g.window, -1,
                    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g.renderer) g.renderer = SDL_CreateRenderer(g.window, -1, SDL_RENDERER_SOFTWARE);
    if (!g.window || !g.renderer) {
        fprintf(stderr, "SDL window failed: %s\n", SDL_GetError());
        SDL_Quit(); return 1;
    }
    SDL_RenderSetLogicalSize(g.renderer, SCREEN_W, SCREEN_H);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    memset(&want, 0, sizeof(want));
    want.freq = 22050; want.format = AUDIO_F32SYS; want.channels = 1; want.samples = 512;
    g.audio = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (g.audio) SDL_PauseAudioDevice(g.audio, 0);
    before = SDL_GetPerformanceCounter();
    while (g.running) {
        SDL_Event event;
        uint64_t current = SDL_GetPerformanceCounter();
        double dt = (double)(current - before) / (double)SDL_GetPerformanceFrequency();
        before = current;
        if (dt > .05) dt = .05;
        g.now = (double)SDL_GetTicks64() / 1000.0;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) g.running = 0;
            else if (event.type == SDL_KEYDOWN) handle_key(&g, &event.key);
        }
        update_game(&g, dt);
        render(&g);
    }
    if (g.audio) SDL_CloseAudioDevice(g.audio);
    SDL_DestroyRenderer(g.renderer);
    SDL_DestroyWindow(g.window);
    SDL_Quit();
    return 0;
}
