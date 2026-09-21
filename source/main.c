#include <nds.h>
#include <stdio.h>
#include <string.h>

#define GROUND_Y      160
#define TILE          16
#define LEVEL_W       180
#define LEVEL_H       6
#define PLAYER_SX     52
#define PSIZE         16
#define SPEED         3
#define JUMP_V        (-1450)
#define GRAV          121
#define MAX_FALL      2300
#define END_X         2360
#define MAP_COUNT     5
#define CHAR_COUNT    4

enum { ST_MENU = 0, ST_PLAY, ST_PAUSED, ST_WIN };
enum { T_EMPTY = 0, T_BLOCK = 1, T_SPIKE = 2 };
enum {
    C_TRANS = 0,
    C_DARK,
    C_CYAN,
    C_LIGHT,
    C_BLOCK,
    C_ACCENT,
    C_SPIKE,
    C_EDGE,
    C_PINK,
    C_YELLOW,
    C_PURPLE,
    C_GREEN,
    C_ORANGE
};

typedef struct {
    const char *name;
    int theme;
    int speed;
} MapDef;

typedef struct {
    const char *name;
    u16 body;
    u16 accent;
} CharacterDef;

static MapDef maps[MAP_COUNT] = {
    { "NEON", 0, 3 },
    { "SKY", 1, 3 },
    { "VOID", 2, 4 },
    { "RIFT", 3, 4 },
    { "DASH", 4, 5 }
};

static CharacterDef characters[CHAR_COUNT] = {
    { "CUBE", RGB15(0, 25, 30), RGB15(17, 31, 31) },
    { "SHIP", RGB15(27, 18, 26), RGB15(31, 25, 29) },
    { "BALL", RGB15(18, 14, 31), RGB15(29, 30, 31) },
    { "WAVE", RGB15(10, 26, 15), RGB15(28, 31, 16) }
};

static int bgId;
static u16 *gfxPlayer, *gfxBlock, *gfxSpike;
static u16 *topGfx;
static u8 grid[LEVEL_H][LEVEL_W];
static int px, py;
static s32 fy, vy;
static bool onGround;
static int rot;
static int state;
static int attempts = 0;
static int mapIndex = 0;
static int characterIndex = 0;
static int menuSpin = 0;
static int startGameTimer = 0;

static void putTile(int c, int r, u8 t) {
    if (c >= 0 && c < LEVEL_W && r >= 0 && r < LEVEL_H) grid[r][c] = t;
}

static void spikes(int c, int n) {
    for (int i = 0; i < n; i++) putTile(c + i, 0, T_SPIKE);
}

static void blocks(int c, int r, int w, int h) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) putTile(c + i, r + j, T_BLOCK);
    }
}

static void buildLevelForMap(int idx) {
    memset(grid, 0, sizeof(grid));
    int m = maps[idx].theme;
    int s = maps[idx].speed;

    switch (m) {
        case 0:
            spikes(15, 2);
            blocks(24, 0, 1, 1);
            spikes(28, 1);
            blocks(36, 0, 2, 1);
            spikes(47, 2);
            blocks(57, 0, 1, 1);
            spikes(66, 1);
            blocks(78, 0, 3, 1);
            spikes(88, 2);
            blocks(98, 0, 2, 1);
            spikes(110, 1);
            blocks(120, 0, 2, 1);
            spikes(133, 2);
            break;
        case 1:
            spikes(18, 1);
            blocks(28, 0, 2, 1);
            spikes(35, 2);
            blocks(46, 0, 3, 1);
            spikes(58, 1);
            blocks(70, 0, 1, 1);
            spikes(79, 2);
            blocks(91, 0, 2, 1);
            spikes(100, 1);
            blocks(112, 0, 4, 1);
            spikes(124, 1);
            break;
        case 2:
            spikes(14, 2);
            blocks(28, 0, 1, 1);
            spikes(35, 1);
            blocks(45, 0, 3, 1);
            spikes(56, 2);
            blocks(69, 0, 2, 1);
            spikes(82, 1);
            blocks(92, 0, 4, 1);
            spikes(103, 2);
            blocks(116, 0, 1, 1);
            spikes(124, 1);
            blocks(138, 0, 2, 1);
            break;
        case 3:
            spikes(20, 1);
            blocks(30, 0, 2, 1);
            spikes(42, 2);
            blocks(55, 0, 2, 1);
            spikes(67, 1);
            blocks(76, 0, 3, 1);
            spikes(92, 2);
            blocks(105, 0, 2, 1);
            spikes(120, 1);
            blocks(129, 0, 4, 1);
            spikes(146, 1);
            break;
        default:
            spikes(19, 1);
            blocks(31, 0, 2, 1);
            spikes(41, 2);
            blocks(53, 0, 1, 1);
            spikes(62, 2);
            blocks(75, 0, 3, 1);
            spikes(90, 1);
            blocks(102, 0, 2, 1);
            spikes(112, 2);
            blocks(124, 0, 2, 1);
            spikes(139, 1);
            break;
    }

    if (s > 3) {
        for (int x = 10; x < LEVEL_W; x += 24) {
            if ((x / 5) % 2 == 0) putTile(x, 1, T_BLOCK);
        }
    }
}

static void putPix(u8 *buf, int x, int y, u8 c) {
    int tile = (y >> 3) * 2 + (x >> 3);
    buf[tile * 64 + (y & 7) * 8 + (x & 7)] = c;
}

static void rect(u8 *buf, int x0, int y0, int x1, int y1, u8 c) {
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) putPix(buf, x, y, c);
    }
}

static void uploadSprite(u16 *dst, const u8 *src) {
    for (int i = 0; i < 128; i++) {
        dst[i] = (u16)(src[2 * i] | (src[2 * i + 1] << 8));
    }
}

static void makeSprites(void) {
    static u8 buf[256];

    memset(buf, 0, sizeof(buf));
    rect(buf, 0, 0, 16, 16, C_DARK);
    rect(buf, 2, 2, 14, 14, characters[characterIndex].body);
    rect(buf, 5, 5, 11, 11, characters[characterIndex].accent);
    uploadSprite(gfxPlayer, buf);

    memset(buf, 0, sizeof(buf));
    rect(buf, 0, 0, 16, 16, C_DARK);
    rect(buf, 1, 1, 15, 15, C_BLOCK);
    rect(buf, 3, 3, 13, 13, C_ACCENT);
    rect(buf, 4, 4, 12, 12, C_BLOCK);
    uploadSprite(gfxBlock, buf);

    memset(buf, 0, sizeof(buf));
    for (int y = 0; y < 16; y++) {
        int x0 = 7 - y / 2;
        int x1 = 8 + y / 2;
        for (int x = x0; x <= x1; x++) {
            u8 c = (x == x0 || x == x1 || y == 15) ? C_EDGE : C_SPIKE;
            putPix(buf, x, y, c);
        }
    }
    uploadSprite(gfxSpike, buf);

    SPRITE_PALETTE[C_DARK]   = RGB15(1, 1, 4);
    SPRITE_PALETTE[C_CYAN]   = RGB15(0, 24, 31);
    SPRITE_PALETTE[C_LIGHT]  = RGB15(20, 31, 31);
    SPRITE_PALETTE[C_BLOCK]  = RGB15(6, 6, 20);
    SPRITE_PALETTE[C_ACCENT] = RGB15(12, 14, 31);
    SPRITE_PALETTE[C_SPIKE]  = RGB15(28, 28, 31);
    SPRITE_PALETTE[C_EDGE]   = RGB15(2, 2, 6);
    SPRITE_PALETTE[C_PINK]   = RGB15(31, 12, 31);
    SPRITE_PALETTE[C_YELLOW] = RGB15(31, 27, 10);
    SPRITE_PALETTE[C_PURPLE] = RGB15(21, 8, 29);
    SPRITE_PALETTE[C_GREEN]  = RGB15(11, 27, 16);
    SPRITE_PALETTE[C_ORANGE] = RGB15(31, 18, 0);
}

static void drawTopBackground(int mapTheme) {
    topGfx = bgGetGfxPtr(bgId);
    for (int y = 0; y < 192; y++) {
        for (int x = 0; x < 256; x++) {
            int r = 5, g = 12, b = 28;
            switch (mapTheme) {
                case 0: r = 7 + (y >> 2); g = 16 + (y >> 2); b = 26 + (x >> 4); break;
                case 1: r = 7 + (x >> 4); g = 15 + (y >> 3); b = 30; break;
                case 2: r = 12 + (y >> 3); g = 8 + (x >> 5); b = 18 + (x >> 4); break;
                case 3: r = 18 + (y >> 3); g = 12 + (x >> 5); b = 20; break;
                case 4: r = 14 + (x >> 4); g = 10 + (y >> 3); b = 20; break;
            }
            if (y >= GROUND_Y) {
                r = 3; g = 4; b = 10;
                if ((x & 15) == 0) { r = 6; g = 8; b = 16; }
            }
            if ((x % 32 == 0) || (y % 32 == 0)) {
                r += 2; g += 1; b += 2;
            }
            topGfx[y * 256 + x] = RGB15(r, g, b) | BIT(15);
        }
    }
}

static void drawGlyphBig(int x, int y, char ch, u16 color) {
    static const unsigned char fontA[7] = { 0x7E, 0x11, 0x11, 0x7E, 0x00, 0x00, 0x00 };
    static const unsigned char fontD[7] = { 0x7F, 0x49, 0x49, 0x36, 0x00, 0x00, 0x00 };
    static const unsigned char fontE[7] = { 0x7F, 0x49, 0x49, 0x41, 0x00, 0x00, 0x00 };
    static const unsigned char fontF[7] = { 0x7F, 0x09, 0x09, 0x01, 0x00, 0x00, 0x00 };
    static const unsigned char fontG[7] = { 0x3E, 0x41, 0x49, 0x49, 0x3A, 0x00, 0x00 };
    static const unsigned char fontH[7] = { 0x7F, 0x08, 0x08, 0x7F, 0x00, 0x00, 0x00 };
    static const unsigned char fontI[7] = { 0x00, 0x41, 0x7F, 0x41, 0x00, 0x00, 0x00 };
    static const unsigned char fontK[7] = { 0x7F, 0x08, 0x14, 0x22, 0x00, 0x00, 0x00 };
    static const unsigned char fontM[7] = { 0x7F, 0x02, 0x04, 0x02, 0x7F, 0x00, 0x00 };
    static const unsigned char fontN[7] = { 0x7F, 0x04, 0x08, 0x10, 0x7F, 0x00, 0x00 };
    static const unsigned char fontO[7] = { 0x3E, 0x41, 0x41, 0x3E, 0x00, 0x00, 0x00 };
    static const unsigned char fontR[7] = { 0x7F, 0x09, 0x19, 0x66, 0x00, 0x00, 0x00 };
    static const unsigned char fontS[7] = { 0x46, 0x49, 0x49, 0x31, 0x00, 0x00, 0x00 };
    static const unsigned char fontT[7] = { 0x01, 0x01, 0x7F, 0x01, 0x01, 0x00, 0x00 };
    static const unsigned char fontY[7] = { 0x01, 0x1F, 0x60, 0x80, 0x00, 0x00, 0x00 };
    static const unsigned char fontV[7] = { 0x3F, 0x40, 0x40, 0x3F, 0x00, 0x00, 0x00 };
    static const unsigned char fontSpace[7] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const unsigned char fontDash[7] = { 0x00, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x00 };

    const unsigned char *data = fontSpace;
    switch (ch) {
        case 'A': data = fontA; break;
        case 'D': data = fontD; break;
        case 'E': data = fontE; break;
        case 'F': data = fontF; break;
        case 'G': data = fontG; break;
        case 'H': data = fontH; break;
        case 'I': data = fontI; break;
        case 'K': data = fontK; break;
        case 'M': data = fontM; break;
        case 'N': data = fontN; break;
        case 'O': data = fontO; break;
        case 'R': data = fontR; break;
        case 'S': data = fontS; break;
        case 'T': data = fontT; break;
        case 'V': data = fontV; break;
        case 'Y': data = fontY; break;
        case ' ': data = fontSpace; break;
        case '-': data = fontDash; break;
        default: return;
    }

    for (int yy = 0; yy < 7; yy++) {
        for (int xx = 0; xx < 5; xx++) {
            if ((data[yy] >> (4 - xx)) & 1) {
                int px = x + xx * 2;
                int py = y + yy * 2;
                if (px >= 0 && px < 256 && py >= 0 && py < 160) {
                    topGfx[py * 256 + px] = color;
                    topGfx[(py + 1) * 256 + px] = color;
                    topGfx[py * 256 + px + 1] = color;
                    topGfx[(py + 1) * 256 + px + 1] = color;
                }
            }
        }
    }
}

static void drawTitleLine(const char *text, int x, int y) {
    int dx = x;
    for (const char *c = text; *c; c++) {
        drawGlyphBig(dx, y, *c, RGB15(0, 31, 15));
        dx += 14;
    }
}

static void drawMapBanner(const char *text) {
    drawTitleLine("GEOMETRY", 14, 12);
    drawTitleLine("DASH", 112, 52);
    int x = 22;
    int y = 136;
    for (const char *c = text; *c; c++) {
        drawGlyphBig(x, y, *c, RGB15(0, 31, 19));
        x += 10;
    }
}

static void drawHudBottom(void) {
    consoleClear();
    printf("\x1b[1;2HGEOMETRY DASH\n");
    printf("MAP: %s\n", maps[mapIndex].name);
    printf("CHAR: %s\n", characters[characterIndex].name);
    printf("A = PLAY  START = PAUSE\n");
    printf("L/R = MAP  U/D = CHAR\n");
    printf("ATTEMPTS: %d\n", attempts);
}

static void resetPlayer(void) {
    px = PLAYER_SX;
    py = GROUND_Y - PSIZE;
    fy = py << 8;
    vy = 0;
    onGround = true;
    rot = 0;
    state = ST_MENU;
    attempts++;
    drawHudBottom();
}

static void startRun(void) {
    buildLevelForMap(mapIndex);
    px = PLAYER_SX;
    py = GROUND_Y - PSIZE;
    fy = py << 8;
    vy = 0;
    onGround = true;
    rot = 0;
    state = ST_PLAY;
    startGameTimer = 30;
}

static void die(void) {
    state = ST_PAUSED;
}

static void updatePlay(void) {
    u32 held = keysHeld();
    bool press = (held & (KEY_A | KEY_B | KEY_UP | KEY_L | KEY_R | KEY_TOUCH)) != 0;

    if (onGround && press) {
        vy = JUMP_V;
        onGround = false;
    }

    int prevBottom = py + PSIZE;
    vy += GRAV;
    if (vy > MAX_FALL) vy = MAX_FALL;
    fy += vy;
    px += maps[mapIndex].speed + 1;
    py = fy >> 8;
    onGround = false;

    if (py + PSIZE >= GROUND_Y) {
        py = GROUND_Y - PSIZE;
        fy = py << 8;
        vy = 0;
        onGround = true;
    }

    int c0 = px >> 4;
    int c1 = (px + PSIZE - 1) >> 4;
    for (int c = c0; c <= c1; c++) {
        if (c < 0 || c >= LEVEL_W) continue;
        for (int r = 0; r < LEVEL_H; r++) {
            u8 t = grid[r][c];
            if (t == T_EMPTY) continue;

            int bx = c * TILE;
            int by = GROUND_Y - (r + 1) * TILE;

            if (t == T_BLOCK) {
                if (px + PSIZE > bx && px < bx + TILE &&
                    py + PSIZE > by && py < by + TILE) {
                    int pen = py + PSIZE - by;
                    if (vy >= 0 && (prevBottom <= by || pen <= 4)) {
                        py = by - PSIZE;
                        fy = py << 8;
                        vy = 0;
                        onGround = true;
                    } else {
                        die();
                        return;
                    }
                }
            } else {
                if (px + PSIZE - 2 > bx + 4 && px + 2 < bx + 12 &&
                    py + PSIZE - 2 > by + 6 && py + 2 < by + TILE) {
                    die();
                    return;
                }
            }
        }
    }

    if (onGround) {
        rot = ((rot + 45) / 90) * 90;
        rot %= 360;
    } else {
        rot += 4;
        if (rot >= 360) rot -= 360;
    }

    if (px >= END_X) {
        state = ST_WIN;
    }
}

static void hideSprite(int id) {
    oamSet(&oamMain, id, 0, 0, 0, 0, SpriteSize_16x16, SpriteColorFormat_256Color,
           gfxBlock, -1, false, true, false, false, false);
}

static void renderGame(void) {
    int cam = px - PLAYER_SX;
    int id = 1;

    int c0 = cam >> 4;
    for (int c = c0; c <= c0 + 18; c++) {
        if (c < 0 || c >= LEVEL_W) continue;
        for (int r = 0; r < LEVEL_H; r++) {
            u8 t = grid[r][c];
            if (t == T_EMPTY || id >= 127) continue;
            int sx = c * TILE - cam;
            int sy = GROUND_Y - (r + 1) * TILE;
            oamSet(&oamMain, id++, sx, sy, 1, 0, SpriteSize_16x16,
                   SpriteColorFormat_256Color,
                   (t == T_BLOCK) ? gfxBlock : gfxSpike,
                   -1, false, false, false, false, false);
        }
    }
    for (; id < 128; id++) hideSprite(id);

    if (state != ST_PAUSED) {
        oamRotateScale(&oamMain, 0, (rot * 32768) / 360, 256, 256);
        oamSet(&oamMain, 0, PLAYER_SX - 8, py - 8, 0, 0, SpriteSize_16x16,
               SpriteColorFormat_256Color, gfxPlayer, 0, true, false, false, false, false);
    } else {
        hideSprite(0);
    }

    bgSetScroll(bgId, cam & 255, 0);
    drawMapBanner(maps[mapIndex].name);
}

static void handleMenuInput(void) {
    u32 down = keysDown();
    if (down & KEY_LEFT) {
        mapIndex = (mapIndex + MAP_COUNT - 1) % MAP_COUNT;
        menuSpin = 24;
    }
    if (down & KEY_RIGHT) {
        mapIndex = (mapIndex + 1) % MAP_COUNT;
        menuSpin = -24;
    }
    if (down & KEY_UP) {
        characterIndex = (characterIndex + CHAR_COUNT - 1) % CHAR_COUNT;
        makeSprites();
    }
    if (down & KEY_DOWN) {
        characterIndex = (characterIndex + 1) % CHAR_COUNT;
        makeSprites();
    }
    if (down & KEY_A) {
        startRun();
    }
    if (down & KEY_START) {
        state = ST_PAUSED;
    }
}

static void handleInGameInput(void) {
    u32 down = keysDown();
    if (down & KEY_START) {
        state = ST_PAUSED;
        return;
    }
    if (down & (KEY_A | KEY_B | KEY_UP | KEY_L | KEY_R | KEY_TOUCH)) {
        if (onGround) {
            vy = JUMP_V;
            onGround = false;
        }
    }
    if (down & KEY_SELECT) {
        state = ST_MENU;
        drawHudBottom();
    }
}

int main(void) {
    videoSetMode(MODE_5_2D | DISPLAY_BG3_ACTIVE | DISPLAY_SPR_ACTIVE);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankB(VRAM_B_MAIN_SPRITE);
    consoleDemoInit();

    bgId = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    drawTopBackground(maps[mapIndex].theme);

    oamInit(&oamMain, SpriteMapping_1D_32, false);
    gfxPlayer = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_256Color);
    gfxBlock  = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_256Color);
    gfxSpike  = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_256Color);
    makeSprites();

    buildLevelForMap(mapIndex);
    resetPlayer();
    drawHudBottom();

    while (1) {
        scanKeys();
        u32 down = keysDown();

        if (state == ST_MENU) {
            handleMenuInput();
            drawTopBackground(maps[mapIndex].theme);
            drawMapBanner(maps[mapIndex].name);
            drawHudBottom();
        } else if (state == ST_PLAY) {
            handleInGameInput();
            updatePlay();
            renderGame();
        } else if (state == ST_PAUSED) {
            if (down & KEY_A) {
                startRun();
            }
            if (down & KEY_SELECT) {
                state = ST_MENU;
                drawHudBottom();
            }
            if (down & KEY_LEFT) {
                mapIndex = (mapIndex + MAP_COUNT - 1) % MAP_COUNT;
                buildLevelForMap(mapIndex);
            }
            if (down & KEY_RIGHT) {
                mapIndex = (mapIndex + 1) % MAP_COUNT;
                buildLevelForMap(mapIndex);
            }
            drawTopBackground(maps[mapIndex].theme);
            drawMapBanner(maps[mapIndex].name);
            drawHudBottom();
        } else if (state == ST_WIN) {
            if (down & (KEY_A | KEY_B | KEY_START)) {
                state = ST_MENU;
                drawHudBottom();
            }
            drawTopBackground(maps[mapIndex].theme);
            drawMapBanner(maps[mapIndex].name);
        }

        if (menuSpin != 0) {
            menuSpin += (menuSpin > 0) ? -2 : 2;
        }

        if (startGameTimer > 0) startGameTimer--;

        swiWaitForVBlank();
        oamUpdate(&oamMain);
        bgUpdate();
    }

    return 0;
}
