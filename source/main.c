/*
 * CUBE DASH DS
 * Runner de ritmo original para Nintendo DS: un cubo avanza solo,
 * tú saltas pinchos y bloques. Solo usa libnds:
 * sin archivos externos, sin FAT y sin MP3 (para evitar pantallas negras).
 *
 * Controles: A / B / UP / L / R / tocar la pantalla = saltar (mantener = saltar seguido)
 *            START = reiniciar
 */

#include <nds.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Ajustes que puedes tocar
// ---------------------------------------------------------------------------
#define GROUND_Y    160          // y (pantalla) del suelo
#define TILE        16           // tamaño de cada casilla
#define LEVEL_W     144          // ancho del nivel en casillas
#define LEVEL_H     6            // alto máximo del nivel en casillas
#define PLAYER_SX   48           // x fija del cubo en pantalla
#define PSIZE       16           // tamaño del cubo
#define SPEED       3            // píxeles por frame que avanza el cubo
#define JUMP_V      (-1450)      // velocidad de salto (punto fijo 8.8)
#define GRAV        121          // gravedad por frame (punto fijo 8.8)
#define MAX_FALL    2300         // velocidad máxima de caída
#define LAND_TOL    4            // píxeles de tolerancia para "subirse" a un bloque
#define END_X       ((LEVEL_W - 4) * TILE)   // al pasar esta x ganas

// ---------------------------------------------------------------------------
// Tipos y estado
// ---------------------------------------------------------------------------
enum { T_EMPTY = 0, T_BLOCK = 1, T_SPIKE = 2 };
enum { ST_PLAY, ST_DEAD, ST_WIN };

// Índices de la paleta de sprites
enum { C_TRANS = 0, C_DARK, C_CYAN, C_LIGHT, C_BLOCK, C_ACCENT, C_SPIKE, C_EDGE };

static u8 grid[LEVEL_H][LEVEL_W];   // grid[fila desde el suelo][columna]

static int  bgId;
static u16 *gfxPlayer, *gfxBlock, *gfxSpike;

static int  px, py;        // posición del cubo (mundo x, pantalla y)
static s32  fy, vy;        // y en punto fijo 8.8 y velocidad vertical
static bool onGround;
static int  rot;           // rotación del cubo en grados
static int  state;
static int  deadTimer;
static int  attempts = 0;
static int  lastPct  = -1;

// ---------------------------------------------------------------------------
// Nivel (edítalo a tu gusto)
//   spikes(col, n)          -> n pinchos seguidos en el suelo
//   blocks(col, fila, w, h) -> rectángulo de bloques (fila 0 = sobre el suelo)
// ---------------------------------------------------------------------------
static void putTile(int c, int r, u8 t) {
    if (c >= 0 && c < LEVEL_W && r >= 0 && r < LEVEL_H) grid[r][c] = t;
}
static void spikes(int c, int n) {
    for (int i = 0; i < n; i++) putTile(c + i, 0, T_SPIKE);
}
static void blocks(int c, int r, int w, int h) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) putTile(c + i, r + j, T_BLOCK);
}

static void buildLevel(void) {
    memset(grid, 0, sizeof(grid));

    spikes(16, 1);
    spikes(25, 2);
    blocks(34, 0, 1, 1);
    spikes(42, 1);
    blocks(50, 0, 4, 1);      // plataforma baja
    spikes(60, 2);
    blocks(69, 0, 2, 1);
    spikes(78, 1);
    spikes(88, 1);
    blocks(98, 0, 3, 1);
    spikes(104, 2);
    spikes(114, 1);
    blocks(120, 0, 1, 1);
    spikes(128, 2);
}

// ---------------------------------------------------------------------------
// Gráficos generados por código (no hay archivos externos)
// ---------------------------------------------------------------------------
// Un sprite 16x16 de 256 colores se guarda como 4 tiles de 8x8 en este orden:
// (0,0) (1,0) (0,1) (1,1)
static void putPix(u8 *buf, int x, int y, u8 c) {
    int tile = (y >> 3) * 2 + (x >> 3);
    buf[tile * 64 + (y & 7) * 8 + (x & 7)] = c;
}

// rellena [x0,x1) x [y0,y1)
static void rect(u8 *buf, int x0, int y0, int x1, int y1, u8 c) {
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            putPix(buf, x, y, c);
}

// La VRAM solo admite escrituras de 16 bits, así que copiamos de dos en dos bytes
static void uploadSprite(u16 *dst, const u8 *src) {
    for (int i = 0; i < 128; i++)
        dst[i] = (u16)(src[2 * i] | (src[2 * i + 1] << 8));
}

static void makeSprites(void) {
    static u8 buf[256];

    // Cubo
    memset(buf, 0, sizeof(buf));
    rect(buf, 0, 0, 16, 16, C_DARK);
    rect(buf, 1, 1, 15, 15, C_CYAN);
    rect(buf, 4, 4, 12, 12, C_DARK);
    rect(buf, 5, 5, 11, 11, C_LIGHT);
    uploadSprite(gfxPlayer, buf);

    // Bloque
    memset(buf, 0, sizeof(buf));
    rect(buf, 0, 0, 16, 16, C_DARK);
    rect(buf, 1, 1, 15, 15, C_BLOCK);
    rect(buf, 3, 3, 13, 13, C_ACCENT);
    rect(buf, 4, 4, 12, 12, C_BLOCK);
    uploadSprite(gfxBlock, buf);

    // Pincho (triángulo)
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

    // Paleta (índice 0 = transparente)
    SPRITE_PALETTE[C_DARK]   = RGB15(1, 1, 4);
    SPRITE_PALETTE[C_CYAN]   = RGB15(0, 24, 31);
    SPRITE_PALETTE[C_LIGHT]  = RGB15(20, 31, 31);
    SPRITE_PALETTE[C_BLOCK]  = RGB15(6, 6, 20);
    SPRITE_PALETTE[C_ACCENT] = RGB15(12, 14, 31);
    SPRITE_PALETTE[C_SPIKE]  = RGB15(28, 28, 31);
    SPRITE_PALETTE[C_EDGE]   = RGB15(2, 2, 6);
}

// Fondo dibujado una sola vez (degradado, rejilla y suelo). Se desplaza con el scroll.
static void drawBackground(void) {
    bgId = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    u16 *gfx = bgGetGfxPtr(bgId);

    for (int y = 0; y < 192; y++) {
        for (int x = 0; x < 256; x++) {
            int r, g, b;
            if (y < GROUND_Y) {
                r = 2 + y / 24;
                g = 4 + y / 10;
                b = 14 + y / 12;
                if ((x & 31) == 0) { r += 2; g += 3; b += 2; }
            } else if (y < GROUND_Y + 2) {
                r = 8; g = 28; b = 31;
            } else {
                r = 3; g = 4; b = 10;
                if ((x & 15) == 0) { r = 6; g = 8; b = 16; }
            }
            gfx[y * 256 + x] = RGB15(r, g, b) | BIT(15);
        }
    }
}

// ---------------------------------------------------------------------------
// Pantalla inferior (texto)
// ---------------------------------------------------------------------------
static void drawHud(void) {
    consoleClear();
    printf("\x1b[1;2HCUBE DASH DS");
    printf("\x1b[3;2HA / B / Tocar = saltar");
    printf("\x1b[4;2H(mantener = salto continuo)");
    printf("\x1b[5;2HSTART = reiniciar");
    printf("\x1b[8;2HIntento: %d", attempts);
}

static void updatePct(void) {
    int pct = (px - PLAYER_SX) * 100 / (END_X - PLAYER_SX);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (pct != lastPct) {
        lastPct = pct;
        printf("\x1b[10;2HProgreso: %3d%%", pct);
    }
}

// ---------------------------------------------------------------------------
// Lógica
// ---------------------------------------------------------------------------
static void resetPlayer(void) {
    px = PLAYER_SX;
    py = GROUND_Y - PSIZE;
    fy = py << 8;
    vy = 0;
    onGround = true;
    rot = 0;
    state = ST_PLAY;
    lastPct = -1;
    attempts++;
    drawHud();
}

static void die(void) {
    state = ST_DEAD;
    deadTimer = 40;
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
    px += SPEED;
    py = fy >> 8;
    onGround = false;

    // Suelo
    if (py + PSIZE >= GROUND_Y) {
        py = GROUND_Y - PSIZE;
        fy = py << 8;
        vy = 0;
        onGround = true;
    }

    // Colisiones con las casillas cercanas
    int c0 = px >> 4;
    int c1 = (px + PSIZE - 1) >> 4;
    for (int c = c0; c <= c1; c++) {
        if (c < 0 || c >= LEVEL_W) continue;
        for (int r = 0; r < LEVEL_H; r++) {
            u8 t = grid[r][c];
            if (t == T_EMPTY) continue;

            int bx = c * TILE;
            int by = GROUND_Y - (r + 1) * TILE;   // borde superior de la casilla

            if (t == T_BLOCK) {
                if (px + PSIZE > bx && px < bx + TILE &&
                    py + PSIZE > by && py < by + TILE) {
                    int pen = py + PSIZE - by;    // cuánto se hunde por arriba
                    if (vy >= 0 && (prevBottom <= by || pen <= LAND_TOL)) {
                        py = by - PSIZE;          // aterriza encima
                        fy = py << 8;
                        vy = 0;
                        onGround = true;
                    } else {
                        die();                    // choque de lado o por debajo
                        return;
                    }
                }
            } else { // T_SPIKE: hitbox más pequeña que el dibujo
                if (px + PSIZE - 2 > bx + 4 && px + 2 < bx + 12 &&
                    py + PSIZE - 2 > by + 6 && py + 2 < by + TILE) {
                    die();
                    return;
                }
            }
        }
    }

    // Rotación del cubo: gira en el aire y se alinea al aterrizar
    if (onGround) {
        rot = ((rot + 45) / 90) * 90;
        rot %= 360;
    } else {
        rot += 4;
        if (rot >= 360) rot -= 360;
    }

    updatePct();

    if (px >= END_X) {
        state = ST_WIN;
        printf("\x1b[13;2HNIVEL COMPLETADO!");
        printf("\x1b[15;2HToca o pulsa A");
    }
}

// ---------------------------------------------------------------------------
// Dibujado
// ---------------------------------------------------------------------------
static void hideSprite(int id) {
    oamSet(&oamMain, id, 0, 0, 0, 0, SpriteSize_16x16, SpriteColorFormat_256Color,
           gfxBlock, -1, false, true, false, false, false);
}

static void render(void) {
    int cam = px - PLAYER_SX;
    int id = 1;

    // Bloques y pinchos visibles (sprites)
    int c0 = cam >> 4;
    for (int c = c0; c <= c0 + 16; c++) {
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

    // Cubo (sprite afín para poder rotarlo; tamaño doble para que no se recorten las esquinas)
    if (state == ST_DEAD) {
        hideSprite(0);
    } else {
        oamRotateScale(&oamMain, 0, (rot * 32768) / 360, 256, 256);
        oamSet(&oamMain, 0, PLAYER_SX - 8, py - 8, 0, 0, SpriteSize_16x16,
               SpriteColorFormat_256Color, gfxPlayer, 0, true, false, false, false, false);
    }

    bgSetScroll(bgId, cam & 255, 0);
}

// ---------------------------------------------------------------------------
int main(void) {
    // Pantalla de arriba: fondo bitmap + sprites. Pantalla de abajo: consola de texto.
    videoSetMode(MODE_5_2D | DISPLAY_BG3_ACTIVE | DISPLAY_SPR_ACTIVE);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankB(VRAM_B_MAIN_SPRITE);
    consoleDemoInit();

    drawBackground();

    oamInit(&oamMain, SpriteMapping_1D_32, false);
    gfxPlayer = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_256Color);
    gfxBlock  = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_256Color);
    gfxSpike  = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_256Color);
    makeSprites();

    buildLevel();
    resetPlayer();

    while (1) {
        scanKeys();
        u32 down = keysDown();

        switch (state) {
            case ST_PLAY:
                if (down & KEY_START) { resetPlayer(); break; }
                updatePlay();
                break;
            case ST_DEAD:
                if (--deadTimer <= 0) resetPlayer();
                break;
            case ST_WIN:
                if (down & (KEY_A | KEY_B | KEY_TOUCH | KEY_START)) {
                    attempts = 0;
                    resetPlayer();
                }
                break;
        }

        render();
        swiWaitForVBlank();
        oamUpdate(&oamMain);
        bgUpdate();
    }

    return 0;
}
