/* Geometry Dash DS - final mockup refinement */
#include <nds.h>
#include <stdio.h>
#include <string.h>

#define GROUND_Y 160
#define TILE 16
#define LEVEL_W 180
#define LEVEL_H 6
#define PLAYER_SX 52
#define PSIZE 16
#define END_X 2360
#define MAPS 5
#define CHARS 4

enum { MENU, PLAY, PAUSE, WIN };
enum { EMPTY, BLOCK, SPIKE };
enum { PAL_CLEAR, PAL_DARK, PAL_BODY, PAL_HIGHLIGHT, PAL_BLOCK, PAL_SPIKE, PAL_EDGE };

typedef struct { const char *name; int theme; int speed; } Map;
typedef struct { const char *name; u16 body; u16 highlight; } Character;

static const Map maps[MAPS] = {
    {"NEON", 0, 3},
    {"SKY", 1, 3},
    {"VOID", 2, 4},
    {"RIFT", 3, 4},
    {"DASH", 4, 5}
};

static const Character chars[CHARS] = {
    {"CUBE", RGB15(0,25,30), RGB15(18,31,31)},
    {"SHIP", RGB15(28,15,25), RGB15(31,27,31)},
    {"BALL", RGB15(16,10,31), RGB15(30,30,31)},
    {"WAVE", RGB15(7,25,13), RGB15(28,31,17)}
};

static u8 level[LEVEL_H][LEVEL_W];
static int bg, state = MENU, map = 0, character = 0;
static int playerX, playerY, angle, attempts;
static s32 fixedY, velocityY;
static bool grounded;
static int carouselOffset;
static u16 *playerGfx, *blockGfx, *spikeGfx;
static u16 *screenPixels;

static void tile(int x, int y, u8 value) {
    if (x >= 0 && x < LEVEL_W && y >= 0 && y < LEVEL_H) level[y][x] = value;
}

static void spikes(int x, int count) {
    for (int i = 0; i < count; i++) tile(x + i, 0, SPIKE);
}

static void blocks(int x, int y, int w, int h) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) tile(x + i, y + j, BLOCK);
    }
}

static void buildLevel(int m) {
    memset(level, 0, sizeof(level));
    spikes(15 + m, 2); blocks(25 + m * 2, 0, 1, 1); spikes(31 + m, 1);
    blocks(42 + m * 2, 0, 2, 1); spikes(54 + m, 2); blocks(67 + m, 0, 1, 1);
    spikes(77 + m, 1); blocks(88 + m * 2, 0, 3, 1); spikes(102 + m, 2);
    blocks(117 + m, 0, 2, 1); spikes(131 + m, 1); blocks(143 + m, 0, 2, 1);
    if (maps[m].speed > 3) {
        for (int x = 12; x < LEVEL_W; x += 25) tile(x, 1, BLOCK);
    }
}

static void pixel(u8 *data, int x, int y, u8 color) {
    int t = (y >> 3) * 2 + (x >> 3);
    data[t * 64 + (y & 7) * 8 + (x & 7)] = color;
}

static void rectangle(u8 *data, int x0, int y0, int x1, int y1, u8 c) {
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) pixel(data, x, y, c);
    }
}

static void upload(u16 *destination, const u8 *source) {
    for (int i = 0; i < 128; i++) {
        destination[i] = (u16)(source[i * 2] | (source[i * 2 + 1] << 8));
    }
}

static void makeSprites(void) {
    static u8 data[256];

    memset(data, 0, sizeof(data));
    rectangle(data, 0, 0, 16, 16, PAL_DARK);
    rectangle(data, 2, 2, 14, 14, PAL_BODY);
    rectangle(data, 5, 5, 11, 11, PAL_HIGHLIGHT);
    upload(playerGfx, data);

    memset(data, 0, sizeof(data));
    rectangle(data, 0, 0, 16, 16, PAL_DARK);
    rectangle(data, 1, 1, 15, 15, PAL_BLOCK);
    rectangle(data, 3, 3, 13, 13, PAL_HIGHLIGHT);
    upload(blockGfx, data);

    memset(data, 0, sizeof(data));
    for (int y = 0; y < 16; y++) {
        int left = 7 - y / 2;
        int right = 8 + y / 2;
        for (int x = left; x <= right; x++) {
            u8 c = (x == left || x == right || y == 15) ? PAL_EDGE : PAL_SPIKE;
            pixel(data, x, y, c);
        }
    }
    upload(spikeGfx, data);

    SPRITE_PALETTE[PAL_DARK] = RGB15(1, 1, 4);
    SPRITE_PALETTE[PAL_BODY] = chars[character].body;
    SPRITE_PALETTE[PAL_HIGHLIGHT] = chars[character].highlight;
    SPRITE_PALETTE[PAL_BLOCK] = RGB15(8, 8, 24);
    SPRITE_PALETTE[PAL_SPIKE] = RGB15(29, 29, 31);
    SPRITE_PALETTE[PAL_EDGE] = RGB15(2, 2, 6);
}

static void paintBackground(int theme) {
    screenPixels = bgGetGfxPtr(bg);
    for (int y = 0; y < 192; y++) {
        for (int x = 0; x < 256; x++) {
            int r = 5, g = 12, b = 27;
            if (theme == 0) { r = 7 + (y >> 2); g = 16 + (y >> 2); b = 27 + (x >> 4); }
            if (theme == 1) { r = 6 + (x >> 4); g = 14 + (y >> 3); b = 30; }
            if (theme == 2) { r = 12 + (y >> 3); g = 8 + (x >> 5); b = 18 + (x >> 4); }
            if (theme == 3) { r = 18 + (y >> 3); g = 11 + (x >> 5); b = 20; }
            if (theme == 4) { r = 15 + (x >> 4); g = 9 + (y >> 3); b = 20; }
            if (y >= GROUND_Y) {
                r = 3; g = 4; b = 10;
                if ((x & 15) == 0) { r = 6; g = 8; b = 16; }
            }
            if ((x & 31) == 0 || (y & 31) == 0) { r += 2; g += 1; b += 2; }
            screenPixels[y * 256 + x] = RGB15(r, g, b) | BIT(15);
        }
    }
}

static void drawGlyph(int x, int y, char c, u16 color) {
    static const u8 font[26][5] = {
        {0x1F,0x11,0x11,0x1F,0x00},
        {0x1F,0x15,0x15,0x0A,0x00},
        {0x0F,0x15,0x15,0x1D,0x00},
        {0x1F,0x15,0x15,0x0A,0x00},
        {0x1F,0x05,0x05,0x01,0x00},
        {0x1F,0x15,0x15,0x11,0x00},
        {0x1F,0x05,0x05,0x1F,0x00},
        {0x1F,0x11,0x11,0x11,0x00},
        {0x1F,0x11,0x11,0x1F,0x00},
        {0x00,0x11,0x1F,0x11,0x00},
        {0x1F,0x04,0x04,0x1F,0x00},
        {0x1F,0x14,0x14,0x04,0x00},
        {0x1F,0x04,0x04,0x1F,0x00},
        {0x1F,0x15,0x15,0x1F,0x00},
        {0x0F,0x15,0x15,0x0F,0x00},
        {0x1F,0x15,0x15,0x11,0x00},
        {0x1F,0x05,0x05,0x01,0x00},
        {0x1F,0x14,0x14,0x1F,0x00},
        {0x1F,0x04,0x14,0x1F,0x00},
        {0x00,0x00,0x1F,0x00,0x00},
        {0x00,0x1F,0x10,0x1F,0x00},
        {0x1F,0x10,0x1F,0x00,0x00},
        {0x11,0x1F,0x11,0x1F,0x00},
        {0x1F,0x0A,0x0A,0x1F,0x00},
        {0x1F,0x1D,0x15,0x11,0x00},
        {0x1F,0x15,0x1D,0x1F,0x00}
    };
    if (c < 'A' || c > 'Z') return;
    int idx = c - 'A';
    for (int yy = 0; yy < 5; yy++) {
        for (int xx = 0; xx < 5; xx++) {
            if (font[idx][yy] & (1 << (4 - xx))) {
                int px = x + xx * 2;
                int py = y + yy * 2;
                if (px >= 0 && px < 256 && py >= 0 && py < 192) {
                    screenPixels[py * 256 + px] = color;
                    screenPixels[(py + 1) * 256 + px] = color;
                    screenPixels[py * 256 + px + 1] = color;
                    screenPixels[(py + 1) * 256 + px + 1] = color;
                }
            }
        }
    }
}

static void text(int x, int y, const char *s, u16 color) {
    while (*s) {
        drawGlyph(x, y, *s++, color);
        x += 13;
    }
}

static void drawTopUi(void) {
    u16 green = RGB15(12, 31, 9);
    u16 dark = RGB15(2, 18, 7);
    text(12, 12, "GEOMETRY", dark);
    text(10, 10, "GEOMETRY", green);
    text(113, 52, "DASH", dark);
    text(111, 50, "DASH", green);
    text(90, 125, maps[map].name, RGB15(0, 31, 21));
}

static void drawBottomUi(void) {
    consoleClear();
    printf("\x1b[1;1H      [ GEOMETRY DASH ]\n");
    printf("\x1b[3;1H  <  %s  >\n", maps[map].name);
    printf("\x1b[5;1H  PLAY   PAUSE   MUSIC\n");
    printf("\x1b[7;1H  [A]  [START]  [L/R]  MAP\n");
    printf("\x1b[9;1H  CHAR: %s  ATTEMPTS: %d\n", chars[character].name, attempts);
    printf("\x1b[11;1H  PREVIEW: clickable map card\n");
    printf("\x1b[13;1H  MENU: %s\n", state == MENU ? "SELECT" : state == PLAY ? "GAME" : "PAUSE");
}

static void resetPlayer(void) {
    playerX = PLAYER_SX;
    playerY = GROUND_Y - PSIZE;
    fixedY = playerY << 8;
    velocityY = 0;
    grounded = true;
    angle = 0;
    state = MENU;
    attempts++;
    drawBottomUi();
}

static void startLevel(void) {
    buildLevel(map);
    playerX = PLAYER_SX;
    playerY = GROUND_Y - PSIZE;
    fixedY = playerY << 8;
    velocityY = 0;
    grounded = true;
    angle = 0;
    state = PLAY;
}

static void dead(void) { state = PAUSE; }

static void updateGame(void) {
    u32 held = keysHeld();
    if (grounded && (held & (KEY_A | KEY_B | KEY_UP | KEY_TOUCH))) {
        velocityY = -1450;
        grounded = false;
    }

    int oldBottom = playerY + PSIZE;
    velocityY += 121;
    if (velocityY > 2300) velocityY = 2300;
    fixedY += velocityY;
    playerX += maps[map].speed + 1;
    playerY = fixedY >> 8;
    grounded = false;

    if (playerY + PSIZE >= GROUND_Y) {
        playerY = GROUND_Y - PSIZE;
        fixedY = playerY << 8;
        velocityY = 0;
        grounded = true;
    }

    int x0 = playerX >> 4;
    int x1 = (playerX + PSIZE - 1) >> 4;
    for (int x = x0; x <= x1; x++) {
        if (x < 0 || x >= LEVEL_W) continue;
        for (int y = 0; y < LEVEL_H; y++) {
            if (!level[y][x]) continue;
            int bx = x * TILE;
            int by = GROUND_Y - (y + 1) * TILE;
            if (level[y][x] == SPIKE) {
                if (playerX + 14 > bx + 4 && playerX + 2 < bx + 12 && playerY + 14 > by + 6) {
                    dead();
                    return;
                }
            } else if (playerX + PSIZE > bx && playerX < bx + TILE && playerY + PSIZE > by && playerY < by + TILE) {
                if (velocityY >= 0 && (oldBottom <= by || playerY + PSIZE - by <= 4)) {
                    playerY = by - PSIZE;
                    fixedY = playerY << 8;
                    velocityY = 0;
                    grounded = true;
                } else {
                    dead();
                    return;
                }
            }
        }
    }

    if (grounded) angle = ((angle + 45) / 90) * 90; else angle += 4;
    if (angle >= 360) angle -= 360;
    if (playerX >= END_X) state = WIN;
}

static void hide(int id) {
    oamSet(&oamMain, id, 0, 0, 0, 0, SpriteSize_16x16, SpriteColorFormat_256Color,
           blockGfx, -1, false, true, false, false, false);
}

static void renderGame(void) {
    int camera = playerX - PLAYER_SX;
    int id = 1;

    for (int x = camera >> 4; x <= (camera >> 4) + 18; x++) {
        if (x < 0 || x >= LEVEL_W) continue;
        for (int y = 0; y < LEVEL_H; y++) {
            if (!level[y][x] || id >= 127) continue;
            oamSet(&oamMain, id++, x * TILE - camera, GROUND_Y - (y + 1) * TILE, 1, 0,
                   SpriteSize_16x16, SpriteColorFormat_256Color,
                   level[y][x] == BLOCK ? blockGfx : spikeGfx,
                   -1, false, false, false, false, false);
        }
    }

    while (id < 128) hide(id++);
    if (state != PAUSE) {
        oamRotateScale(&oamMain, 0, (angle * 32768) / 360, 256, 256);
        oamSet(&oamMain, 0, PLAYER_SX - 8, playerY - 8, 0, 0, SpriteSize_16x16,
               SpriteColorFormat_256Color, playerGfx, 0, true, false, false, false, false);
    } else {
        hide(0);
    }

    bgSetScroll(bg, camera & 255, 0);
    drawTopUi();
}

static void handleMenu(void) {
    u32 down = keysDown();
    if (down & KEY_LEFT) {
        map = (map + MAPS - 1) % MAPS;
        carouselOffset = -1;
    }
    if (down & KEY_RIGHT) {
        map = (map + 1) % MAPS;
        carouselOffset = 1;
    }
    if (down & KEY_UP) {
        character = (character + CHARS - 1) % CHARS;
        makeSprites();
    }
    if (down & KEY_DOWN) {
        character = (character + 1) % CHARS;
        makeSprites();
    }
    if (down & KEY_A) startLevel();
    if (down & KEY_START) state = PAUSE;
    paintBackground(maps[map].theme);
    drawTopUi();
    drawBottomUi();
}

static void handlePause(void) {
    u32 down = keysDown();
    if (down & KEY_A) startLevel();
    if (down & KEY_SELECT) { state = MENU; drawBottomUi(); }
    if (down & KEY_LEFT) { map = (map + MAPS - 1) % MAPS; buildLevel(map); }
    if (down & KEY_RIGHT) { map = (map + 1) % MAPS; buildLevel(map); }
    paintBackground(maps[map].theme);
    drawTopUi();
    drawBottomUi();
}

int main(void) {
    videoSetMode(MODE_5_2D | DISPLAY_BG3_ACTIVE | DISPLAY_SPR_ACTIVE);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankB(VRAM_B_MAIN_SPRITE);
    consoleDemoInit();

    bg = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    paintBackground(maps[map].theme);

    oamInit(&oamMain, SpriteMapping_1D_32, false);
    playerGfx = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_256Color);
    blockGfx = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_256Color);
    spikeGfx = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_256Color);
    makeSprites();
    buildLevel(map);
    resetPlayer();

    while (1) {
        scanKeys();
        u32 down = keysDown();

        if (state == MENU) {
            handleMenu();
        } else if (state == PLAY) {
            updateGame();
            renderGame();
            if (down & KEY_START) { state = PAUSE; drawBottomUi(); }
        } else if (state == PAUSE) {
            handlePause();
        } else if (state == WIN) {
            if (down & (KEY_A | KEY_B | KEY_START)) { state = MENU; drawBottomUi(); }
            drawTopUi();
        }

        if (carouselOffset) carouselOffset += (carouselOffset > 0) ? -1 : 1;

        swiWaitForVBlank();
        oamUpdate(&oamMain);
        bgUpdate();
    }

    return 0;
}
