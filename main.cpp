#include "raylib.h"
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------
// Global textures & sprite layout
// ---------------------------------------------------------

Texture2D texKnight;
Texture2D texRogue;
Texture2D texMage;
Texture2D texEnemyGrunt;
Texture2D texEnemyFast;
Texture2D texEnemyTank;
Texture2D texEnemyBoss;
Texture2D texCoin;
Texture2D texProjectile;   // optional (not used heavily, but available)

const int PLAYER_SPRITE_COLS = 4;
const int PLAYER_SPRITE_ROWS = 3; // 0 idle, 1 run, 2 attack

const int ENEMY_SPRITE_COLS = 4;
const int ENEMY_SPRITE_ROWS = 2; // 0 walk, 1 attack

// ---------------------------------------------------------
// Enums and basic structs
// ---------------------------------------------------------

enum class GameState { MENU, PLAYING, SHOP, VICTORY, GAMEOVER };
enum class EnemyType { GRUNT, FAST, TANK, BOSS };
enum class PlayerClass { KNIGHT, ROGUE, MAGE };

struct Coin {
    Vector2 pos;
    bool collected = false;
};

struct Enemy {
    Vector2 pos;
    Vector2 size;
    int maxHP;
    int hp;
    float speed;
    EnemyType type;
    bool alive = true;

    // Attack / telegraph
    float attackCooldown = 0.0f;
    bool windingUp = false;
    float windupTimer = 0.0f;
    bool attackingAnim = false;
    float attackAnimTimer = 0.0f;

    // Damage gating
    int lastHitAttackId = -1;      // for melee
    int lastProjectileHitId = -1;  // for mage projectiles

    // Sprite / animation
    Texture2D* sprite = nullptr;
    int animFrame = 0;
    int animRow = 0;
    int animMaxFrames = ENEMY_SPRITE_COLS;
    float animTimer = 0.0f;
    float animFrameTime = 0.15f;
};

struct Projectile {
    Vector2 pos;
    Vector2 vel;
    float radius;
    float life;
    bool active;
    int damage;
    int id; // unique per projectile so enemies only take one hit per projectile
};

struct Player {
    std::string name;
    Vector2 pos;
    Vector2 size;
    int maxHP;
    int hp;
    float speed;
    int baseDamage;
    bool facingRight = true;

    // Attack / combo
    bool attacking = false;
    float attackTimer = 0.0f;
    float attackDuration = 0.15f;
    float comboTimer = 0.0f;
    int comboStep = 0;
    Rectangle attackHitbox{};
    int coins = 0;

    // Upgrades
    int damageLevel = 0;
    int healthLevel = 0;
    int speedLevel = 0;

    // Special abilities
    bool blocking = false;
    float blockTimer = 0.0f;
    float blockCooldown = 0.0f;
    float blockCooldownTimer = 0.0f;

    bool dodging = false;
    float dodgeTimer = 0.0f;
    float dodgeDuration = 0.0f;
    float dodgeCooldown = 0.0f;
    float dodgeCooldownTimer = 0.0f;
    float dodgeDir = 0.0f;

    float blinkCooldown = 0.0f;
    float blinkCooldownTimer = 0.0f;

    bool invincible = false;
    float invincibleTimer = 0.0f;

    // Tag each melee attack instance
    int currentAttackId = -1;

    // Sprite / animation
    Texture2D* sprite = nullptr;
    int animFrame = 0;
    int animRow = 0;
    int animMaxFrames = PLAYER_SPRITE_COLS;
    float animTimer = 0.0f;
    float animFrameTime = 0.12f;
};

struct CharacterClass {
    std::string name;
    int maxHP;
    float speed;
    int baseDamage;
    Color color;      // used as fallback tint / debug
    PlayerClass type;
};

// ---------------------------------------------------------
// Constants
// ---------------------------------------------------------

static const float GROUND_TOP = 350.0f;
static const float GROUND_BOTTOM = 430.0f;
static const float LEVEL_LENGTH = 3000.0f;

static const float ENEMY_SPAWN_INTERVAL = 3.0f;
static const float COMBO_RESET_TIME = 1.0f;

static float gHitStopTimer = 0.0f;
static int gAttackCounter = 0;
static int gProjectileCounter = 0;

// ---------------------------------------------------------
// Utility
// ---------------------------------------------------------

Rectangle MakeRect(Vector2 pos, Vector2 size) {
    return { pos.x - size.x * 0.5f, pos.y - size.y, size.x, size.y };
}

bool RectOverlap(Rectangle a, Rectangle b) {
    return CheckCollisionRecs(a, b);
}

// Combo damage multipliers per class
// Tuned vs GRUNT HP (~90):
// Knight ~3 hits, Rogue ~6, Mage ~8–9
float GetComboMultiplier(PlayerClass pc, int step) {
    if (step < 1) step = 1;
    if (step > 3) step = 3;

    switch (pc) {
    case PlayerClass::KNIGHT:
        if (step == 1) return 1.0f;
        if (step == 2) return 1.3f;
        return 2.0f;   // big finisher
    case PlayerClass::ROGUE:
        if (step == 1) return 0.7f;
        if (step == 2) return 0.9f;
        return 1.1f;
    case PlayerClass::MAGE:
        if (step == 1) return 0.8f;
        if (step == 2) return 1.0f;
        return 1.2f;
    }
    return 1.0f;
}

// ---------------------------------------------------------
// Enemy creation
// ---------------------------------------------------------

Enemy MakeEnemy(EnemyType type, float x, float laneY) {
    Enemy e{};
    e.type = type;
    e.pos = { x, laneY };
    e.alive = true;
    e.windingUp = false;
    e.windupTimer = 0.0f;
    e.attackingAnim = false;
    e.attackAnimTimer = 0.0f;
    e.lastHitAttackId = -1;
    e.lastProjectileHitId = -1;

    switch (type) {
    case EnemyType::GRUNT:
        e.size = { 40, 70 };
        e.maxHP = e.hp = 90;
        e.speed = 80.0f;
        e.sprite = &texEnemyGrunt;
        break;
    case EnemyType::FAST:
        e.size = { 32, 60 };
        e.maxHP = e.hp = 80;
        e.speed = 135.0f;
        e.sprite = &texEnemyFast;
        break;
    case EnemyType::TANK:
        e.size = { 60, 90 };
        e.maxHP = e.hp = 150;
        e.speed = 55.0f;
        e.sprite = &texEnemyTank;
        break;
    case EnemyType::BOSS:
        e.size = { 100, 140 };
        e.maxHP = e.hp = 450;
        e.speed = 70.0f;
        e.sprite = &texEnemyBoss;
        break;
    }

    e.animFrame = 0;
    e.animRow = 0;
    e.animMaxFrames = ENEMY_SPRITE_COLS;
    e.animTimer = 0.0f;
    e.animFrameTime = 0.15f;

    return e;
}

// ---------------------------------------------------------
// Shop
// ---------------------------------------------------------

struct ShopOption {
    std::string label;
    int baseCost;
};

static ShopOption shopOptions[] = {
    { "Increase Damage",  5 },
    { "Increase Max HP",  5 },
    { "Increase Speed",   5 }
};

int GetUpgradeCost(const Player& p, int index) {
    int level = 0;
    if (index == 0) level = p.damageLevel;
    else if (index == 1) level = p.healthLevel;
    else if (index == 2) level = p.speedLevel;

    return shopOptions[index].baseCost * (1 + level);
}

void ApplyUpgrade(Player& p, int index) {
    if (index == 0) {
        p.damageLevel++;
        p.baseDamage += 3;
    }
    else if (index == 1) {
        p.healthLevel++;
        p.maxHP += 15;
        p.hp = p.maxHP;
    }
    else if (index == 2) {
        p.speedLevel++;
        p.speed += 20.0f;
    }
}

// ---------------------------------------------------------
// Main
// ---------------------------------------------------------

int main() {
    const int screenWidth = 1280;
    const int screenHeight = 720;

    InitWindow(screenWidth, screenHeight, "2.5D Beat 'Em Up (raylib)");
    InitAudioDevice();

    // ---- Load textures ----
    texKnight      = LoadTexture("assets/knight.png");
    texRogue       = LoadTexture("assets/rogue.png");
    texMage        = LoadTexture("assets/mage.png");
    texEnemyGrunt  = LoadTexture("assets/enemy_grunt.png");
    texEnemyFast   = LoadTexture("assets/enemy_fast.png");
    texEnemyTank   = LoadTexture("assets/enemy_tank.png");
    texEnemyBoss   = LoadTexture("assets/enemy_boss.png");
    texCoin        = LoadTexture("assets/coin.png");
    texProjectile  = LoadTexture("assets/projectile.png"); // optional

    SetTargetFPS(60);

    // Character classes
    std::vector<CharacterClass> classes = {
        { "Knight", 170, 180.0f, 20, RED,    PlayerClass::KNIGHT }, // slow, heavy
        { "Rogue",  110, 270.0f, 14, GREEN,  PlayerClass::ROGUE },  // fast, weak
        { "Mage",   90,  190.0f, 10, PURPLE, PlayerClass::MAGE }    // ranged
    };

    int selectedClassIndex = 0;

    Player player{};
    PlayerClass playerClass = PlayerClass::KNIGHT;
    GameState state = GameState::MENU;

    Camera2D camera{};
    camera.offset = { (float)screenWidth / 2.0f, (float)screenHeight / 2.0f };
    camera.zoom = 1.0f;

    std::vector<Enemy> enemies;
    std::vector<Coin> coins;
    std::vector<Projectile> projectiles;

    bool bossSpawned = false;
    bool bossDefeated = false;
    float enemySpawnTimer = 0.0f;
    int shopSelection = 0;

    // Simple SFX hooks (files optional)
    Sound sfxKnightSwing = LoadSound("sfx_knight_swing.wav");
    Sound sfxRogueSwing  = LoadSound("sfx_rogue_swing.wav");
    Sound sfxMageCast    = LoadSound("sfx_mage_cast.wav");
    Sound sfxHit         = LoadSound("sfx_hit.wav");
    Sound sfxEnemySwing  = LoadSound("sfx_enemy_swing.wav");
    Sound sfxBlock       = LoadSound("sfx_block.wav");
    Sound sfxDodge       = LoadSound("sfx_dodge.wav");
    Sound sfxBlink       = LoadSound("sfx_blink.wav");

    auto ResetGame = [&]() {
        CharacterClass cc = classes[selectedClassIndex];
        playerClass = cc.type;

        player.name = cc.name;
        player.size = { 40, 75 };
        player.pos = { 100.0f, (GROUND_TOP + GROUND_BOTTOM) * 0.5f };
        player.maxHP = cc.maxHP;
        player.hp = player.maxHP;
        player.speed = cc.speed;
        player.baseDamage = cc.baseDamage;
        player.facingRight = true;

        player.attacking = false;
        player.attackTimer = 0.0f;
        player.attackDuration = 0.15f;
        player.comboTimer = 0.0f;
        player.comboStep = 0;
        player.coins = 0;
        player.damageLevel = player.healthLevel = player.speedLevel = 0;
        player.currentAttackId = -1;

        // Abilities
        player.blocking = false;
        player.blockTimer = 0.0f;
        player.blockCooldown = 0.0f;
        player.blockCooldownTimer = 0.0f;

        player.dodging = false;
        player.dodgeTimer = 0.0f;
        player.dodgeDuration = 0.0f;
        player.dodgeCooldown = 0.0f;
        player.dodgeCooldownTimer = 0.0f;
        player.dodgeDir = 0.0f;

        player.blinkCooldown = 0.0f;
        player.blinkCooldownTimer = 0.0f;

        player.invincible = false;
        player.invincibleTimer = 0.0f;

        // Per-class ability tuning
        if (playerClass == PlayerClass::KNIGHT) {
            player.blockCooldown = 1.0f;
            player.sprite = &texKnight;
        } else if (playerClass == PlayerClass::ROGUE) {
            player.dodgeDuration = 0.25f;
            player.dodgeCooldown = 0.9f;
            player.sprite = &texRogue;
        } else if (playerClass == PlayerClass::MAGE) {
            player.blinkCooldown = 1.2f;
            player.sprite = &texMage;
        }

        // Anim defaults
        player.animFrame = 0;
        player.animRow = 0;
        player.animMaxFrames = PLAYER_SPRITE_COLS;
        player.animTimer = 0.0f;
        player.animFrameTime = 0.12f;

        enemies.clear();
        coins.clear();
        projectiles.clear();
        bossSpawned = false;
        bossDefeated = false;
        enemySpawnTimer = 0.0f;

        camera.target = player.pos;
        gHitStopTimer = 0.0f;
        gAttackCounter = 0;
        gProjectileCounter = 0;
    };

    ResetGame();

    // ---------------------------------------------------------
    // Game loop
    // ---------------------------------------------------------
    while (!WindowShouldClose()) {
        float realDt = GetFrameTime();
        if (realDt > 0.05f) realDt = 0.05f;

        // Hit stop
        gHitStopTimer -= realDt;
        if (gHitStopTimer < 0.0f) gHitStopTimer = 0.0f;
        float gameDt = (gHitStopTimer > 0.0f) ? 0.0f : realDt;

        // =========================
        // UPDATE
        // =========================
        if (state == GameState::MENU) {
            if (IsKeyPressed(KEY_RIGHT)) {
                selectedClassIndex = (selectedClassIndex + 1) % (int)classes.size();
            }
            if (IsKeyPressed(KEY_LEFT)) {
                selectedClassIndex--;
                if (selectedClassIndex < 0) selectedClassIndex = (int)classes.size() - 1;
            }

            if (IsKeyPressed(KEY_ENTER)) {
                ResetGame();
            state = GameState::PLAYING;
            }

        } else if (state == GameState::PLAYING) {
            // -------- Input & movement ----------
            Vector2 move = { 0, 0 };
            if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  move.x -= 1.0f;
            if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) move.x += 1.0f;
            if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))    move.y -= 1.0f;
            if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  move.y += 1.0f;

            float mag = std::sqrt(move.x * move.x + move.y * move.y);
            if (mag > 0.0f) {
                move.x /= mag;
                move.y /= mag;
            }

            float moveSpeed = player.speed;

            // Rogue dodge overrides movement
            if (player.dodging) {
                move = { player.dodgeDir, 0.0f };
                moveSpeed = player.speed * 3.5f;
            }

            player.pos.x += move.x * moveSpeed * gameDt;
            player.pos.y += move.y * player.speed * gameDt;

            if (player.pos.x < 0) player.pos.x = 0;
            if (player.pos.x > LEVEL_LENGTH) player.pos.x = LEVEL_LENGTH;
            if (player.pos.y < GROUND_TOP) player.pos.y = GROUND_TOP;
            if (player.pos.y > GROUND_BOTTOM) player.pos.y = GROUND_BOTTOM;

            if (!player.dodging) {
                if (move.x > 0) player.facingRight = true;
                else if (move.x < 0) player.facingRight = false;
            }

            // Shop access
            if (IsKeyPressed(KEY_TAB)) {
                state = GameState::SHOP;
            }

            // --- Ability timers ---
            if (player.blockCooldownTimer > 0.0f)
                player.blockCooldownTimer -= gameDt;
            if (player.blockCooldownTimer < 0.0f)
                player.blockCooldownTimer = 0.0f;

            if (player.dodgeCooldownTimer > 0.0f)
                player.dodgeCooldownTimer -= gameDt;
            if (player.dodgeCooldownTimer < 0.0f)
                player.dodgeCooldownTimer = 0.0f;

            if (player.blinkCooldownTimer > 0.0f)
                player.blinkCooldownTimer -= gameDt;
            if (player.blinkCooldownTimer < 0.0f)
                player.blinkCooldownTimer = 0.0f;

            if (player.invincibleTimer > 0.0f) {
                player.invincibleTimer -= gameDt;
                if (player.invincibleTimer <= 0.0f) {
                    player.invincible = false;
                }
            }

            // Knight block
            if (playerClass == PlayerClass::KNIGHT) {
                if (!player.blocking && player.blockCooldownTimer <= 0.0f && IsKeyPressed(KEY_K)) {
                    player.blocking = true;
                    player.blockTimer = 0.7f;
                    player.blockCooldownTimer = player.blockCooldown;
                    if (IsAudioDeviceReady()) PlaySound(sfxBlock);
                }
                if (player.blocking) {
                    player.blockTimer -= gameDt;
                    if (player.blockTimer <= 0.0f) {
                        player.blocking = false;
                    }
                }
            }

            // Rogue dodge
            if (playerClass == PlayerClass::ROGUE) {
                if (!player.dodging && player.dodgeCooldownTimer <= 0.0f && IsKeyPressed(KEY_K)) {
                    player.dodging = true;
                    player.dodgeTimer = player.dodgeDuration;
                    player.dodgeCooldownTimer = player.dodgeCooldown;
                    player.dodgeDir = player.facingRight ? 1.0f : -1.0f;
                    player.invincible = true;
                    player.invincibleTimer = player.dodgeDuration;
                    if (IsAudioDeviceReady()) PlaySound(sfxDodge);
                }
                if (player.dodging) {
                    player.dodgeTimer -= gameDt;
                    if (player.dodgeTimer <= 0.0f) {
                        player.dodging = false;
                    }
                }
            }

            // Mage blink
            if (playerClass == PlayerClass::MAGE) {
                if (player.blinkCooldownTimer <= 0.0f && IsKeyPressed(KEY_K)) {
                    float dir = player.facingRight ? 1.0f : -1.0f;
                    float blinkDist = 150.0f;
                    player.pos.x += dir * blinkDist;
                    if (player.pos.x < 0) player.pos.x = 0;
                    if (player.pos.x > LEVEL_LENGTH) player.pos.x = LEVEL_LENGTH;
                    player.blinkCooldownTimer = player.blinkCooldown;
                    player.invincible = true;
                    player.invincibleTimer = 0.15f;
                    if (IsAudioDeviceReady()) PlaySound(sfxBlink);
                }
            }

            // -------- ATTACK / COMBO ----------
            player.comboTimer += gameDt;
            if (player.comboTimer > COMBO_RESET_TIME) {
                player.comboTimer = 0.0f;
                player.comboStep = 0;
            }

            bool meleeClass = (playerClass == PlayerClass::KNIGHT || playerClass == PlayerClass::ROGUE);

            if (!player.attacking && IsKeyPressed(KEY_J)) {
                player.attacking = true;
                player.attackTimer = 0.0f;
                player.attackDuration = 0.15f; // base, will override per class/step
                player.comboTimer = 0.0f;
                player.comboStep++;
                if (player.comboStep > 3) player.comboStep = 1;

                float dir = player.facingRight ? 1.0f : -1.0f;

                if (meleeClass) {
                    // New melee attack ID
                    gAttackCounter++;
                    player.currentAttackId = gAttackCounter;

                    float attackRange = 0.0f;
                    float attackWidth = 0.0f;
                    float attackHeight = 0.0f;

                    if (playerClass == PlayerClass::KNIGHT) {
                        if (player.comboStep == 1) {
                            player.attackDuration = 0.28f;
                            attackRange = 55.0f;
                            attackWidth = 60.0f;
                            attackHeight = 70.0f;
                        } else if (player.comboStep == 2) {
                            player.attackDuration = 0.32f;
                            attackRange = 65.0f;
                            attackWidth = 70.0f;
                            attackHeight = 75.0f;
                        } else {
                            player.attackDuration = 0.40f;
                            attackRange = 80.0f;
                            attackWidth = 85.0f;
                            attackHeight = 80.0f;
                        }
                        if (IsAudioDeviceReady()) PlaySound(sfxKnightSwing);
                    } else { // Rogue
                        if (player.comboStep == 1) {
                            player.attackDuration = 0.12f;
                            attackRange = 45.0f;
                            attackWidth = 35.0f;
                            attackHeight = 55.0f;
                        } else if (player.comboStep == 2) {
                            player.attackDuration = 0.14f;
                            attackRange = 55.0f;
                            attackWidth = 40.0f;
                            attackHeight = 55.0f;
                        } else {
                            player.attackDuration = 0.16f;
                            attackRange = 60.0f;
                            attackWidth = 45.0f;
                            attackHeight = 55.0f;
                        }
                        if (IsAudioDeviceReady()) PlaySound(sfxRogueSwing);
                    }

                    // Hitbox: wider and closer so it hits enemies hugging you
                    attackWidth += 20.0f;
                    Vector2 center = {
                        player.pos.x + dir * (attackRange * 0.6f),
                        player.pos.y
                    };
                    player.attackHitbox = MakeRect(center, { attackWidth, attackHeight });
                } else {
                    // Mage projectile (piercing, unique id)
                    player.attackDuration = 0.22f;
                    if (IsAudioDeviceReady()) PlaySound(sfxMageCast);

                    float comboMul = GetComboMultiplier(playerClass, player.comboStep);
                    int dmg = (int)std::round(player.baseDamage * comboMul);

                    Projectile p{};
                    p.active = true;
                    p.life = 1.2f;
                    p.radius = (player.comboStep == 1 ? 18.0f : (player.comboStep == 2 ? 22.0f : 26.0f));
                    p.vel = { dir * (player.comboStep == 1 ? 420.0f : (player.comboStep == 2 ? 460.0f : 520.0f)), 0.0f };
                    p.pos = { player.pos.x + dir * 30.0f, player.pos.y - 25.0f };
                    p.damage = dmg;
                    p.id = ++gProjectileCounter;

                    projectiles.push_back(p);
                }
            }

            if (player.attacking) {
                player.attackTimer += gameDt;
                if (player.attackTimer > player.attackDuration) {
                    player.attacking = false;
                }
            }

            // -------- PLAYER ANIMATION UPDATE --------
            if (player.sprite && player.sprite->width > 0) {
                bool isMoving = (std::fabs(move.x) > 0.01f || std::fabs(move.y) > 0.01f);

                if (player.attacking)      player.animRow = 2; // attack row
                else if (isMoving)         player.animRow = 1; // run row
                else                       player.animRow = 0; // idle row

                player.animTimer += gameDt;
                if (player.animTimer >= player.animFrameTime) {
                    player.animTimer = 0.0f;
                    player.animFrame = (player.animFrame + 1) % player.animMaxFrames;
                }
            }

            // -------- ENEMY SPAWNING ----------
            enemySpawnTimer += gameDt;
            if (enemySpawnTimer > ENEMY_SPAWN_INTERVAL && !bossSpawned) {
                enemySpawnTimer = 0.0f;

                float laneY = GROUND_TOP + (float)GetRandomValue(0, 100);
                if (laneY > GROUND_BOTTOM) laneY = GROUND_BOTTOM;

                float spawnX = player.pos.x + (float)GetRandomValue(250, 450);
                if (spawnX < 400.0f) spawnX = 400.0f;
                if (spawnX > LEVEL_LENGTH - 300.0f) spawnX = LEVEL_LENGTH - 300.0f;

                int r = GetRandomValue(0, 2);
                EnemyType type = EnemyType::GRUNT;
                if (r == 1) type = EnemyType::FAST;
                else if (r == 2) type = EnemyType::TANK;

                enemies.push_back(MakeEnemy(type, spawnX, laneY));
            }

            // Spawn boss near the end
            if (!bossSpawned && player.pos.x > LEVEL_LENGTH - 600.0f) {
                bossSpawned = true;
                float laneY = (GROUND_TOP + GROUND_BOTTOM) * 0.5f;
                enemies.push_back(MakeEnemy(EnemyType::BOSS, LEVEL_LENGTH - 200.0f, laneY));
            }

            // -------- PROJECTILES UPDATE (Mage) ----------
            for (auto& p : projectiles) {
                if (!p.active) continue;
                p.pos.x += p.vel.x * gameDt;
                p.pos.y += p.vel.y * gameDt;
                p.life -= gameDt;
                if (p.life <= 0.0f || p.pos.x < -200.0f || p.pos.x > LEVEL_LENGTH + 200.0f) {
                    p.active = false;
                }
            }

            // -------- ENEMY AI + DAMAGE ----------
            Rectangle pr = MakeRect(player.pos, player.size);

            for (auto& e : enemies) {
                if (!e.alive) continue;
                Rectangle er = MakeRect(e.pos, e.size);

                // Movement only if not in windup / attack anim
                if (!e.windingUp && !e.attackingAnim) {
                    Vector2 dir = { player.pos.x - e.pos.x, player.pos.y - e.pos.y };
                    float dist = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                    if (dist > 5.0f) {
                        dir.x /= dist;
                        dir.y /= dist;
                    } else {
                        dir = { 0,0 };
                    }

                    e.pos.x += dir.x * e.speed * gameDt;
                    e.pos.y += dir.y * e.speed * 0.6f * gameDt;

                    if (e.pos.y < GROUND_TOP) e.pos.y = GROUND_TOP;
                    if (e.pos.y > GROUND_BOTTOM) e.pos.y = GROUND_BOTTOM;

                    er = MakeRect(e.pos, e.size);
                }

                e.attackCooldown -= gameDt;
                if (e.attackCooldown < 0.0f) e.attackCooldown = 0.0f;

                // Enemy attack windup + telegraph
                if (!e.windingUp && !e.attackingAnim && e.attackCooldown <= 0.0f && RectOverlap(er, pr)) {
                    e.windingUp = true;

                    float baseWindup = 0.35f;
                    if (e.type == EnemyType::FAST) baseWindup = 0.25f;
                    else if (e.type == EnemyType::TANK) baseWindup = 0.45f;
                    else if (e.type == EnemyType::BOSS) baseWindup = 0.6f;

                    e.windupTimer = baseWindup;
                }

                if (e.windingUp) {
                    e.windupTimer -= gameDt;
                    if (e.windupTimer <= 0.0f) {
                        er = MakeRect(e.pos, e.size);
                        pr = MakeRect(player.pos, player.size);
                        if (RectOverlap(er, pr)) {
                            int dmg = 6;
                            if (e.type == EnemyType::FAST) dmg = 8;
                            if (e.type == EnemyType::TANK) dmg = 13;
                            if (e.type == EnemyType::BOSS) dmg = 20;

                            int finalDmg = dmg;

                            if (player.invincible) {
                                finalDmg = 0;
                            } else if (player.blocking && playerClass == PlayerClass::KNIGHT) {
                                finalDmg = dmg / 3;
                                if (IsAudioDeviceReady()) PlaySound(sfxBlock);
                            }

                            if (finalDmg > 0) {
                                player.hp -= finalDmg;
                                if (player.hp < 0) player.hp = 0;
                                gHitStopTimer = std::max(gHitStopTimer, 0.05f);
                                if (IsAudioDeviceReady()) PlaySound(sfxEnemySwing);
                            }
                        }

                        e.windingUp = false;
                        e.attackingAnim = true;
                        e.attackAnimTimer = 0.22f;
                        e.attackCooldown = 1.1f;
                    }
                }

                if (e.attackingAnim) {
                    e.attackAnimTimer -= gameDt;
                    if (e.attackAnimTimer <= 0.0f) {
                        e.attackingAnim = false;
                    }
                }

                // Enemy animation
                if (e.sprite && e.sprite->width > 0) {
                    if (e.windingUp || e.attackingAnim) e.animRow = 1;
                    else e.animRow = 0;

                    e.animTimer += gameDt;
                    if (e.animTimer >= e.animFrameTime) {
                        e.animTimer = 0.0f;
                        e.animFrame = (e.animFrame + 1) % e.animMaxFrames;
                    }
                }

                // Player melee attack hits enemy: one hit per enemy per attackId
                bool meleeClassLocal = (playerClass == PlayerClass::KNIGHT || playerClass == PlayerClass::ROGUE);
                if (meleeClassLocal && player.attacking && player.currentAttackId >= 0) {
                    er = MakeRect(e.pos, e.size);
                    if (e.lastHitAttackId != player.currentAttackId && RectOverlap(player.attackHitbox, er)) {
                        e.lastHitAttackId = player.currentAttackId;

                        float comboMul = GetComboMultiplier(playerClass, player.comboStep);
                        int dmg = (int)std::round(player.baseDamage * comboMul);
                        e.hp -= dmg;

                        // Hitstop mainly for melee
                        gHitStopTimer = std::max(
                            gHitStopTimer,
                            (playerClass == PlayerClass::KNIGHT && player.comboStep == 3) ? 0.06f : 0.03f
                        );
                        if (IsAudioDeviceReady()) PlaySound(sfxHit);

                        // Knockback on every melee hit (toned down)
                        float kdDir = (e.pos.x < player.pos.x) ? -1.0f : 1.0f;
                        float knockDist = 0.0f;

                        if (playerClass == PlayerClass::KNIGHT) {
                            if (player.comboStep == 3)      knockDist = 90.0f; // big finisher
                            else                            knockDist = 35.0f; // modest shove
                        } else { // Rogue
                            knockDist = 22.0f; // lighter push
                        }

                        e.pos.x += kdDir * knockDist;

                        if (e.hp <= 0) {
                            e.alive = false;

                            int coinCount = 1;
                            if (e.type == EnemyType::TANK) coinCount = 3;
                            if (e.type == EnemyType::BOSS) coinCount = 10;
                            for (int i = 0; i < coinCount; ++i) {
                                Coin c{};
                                c.pos = { e.pos.x + (float)GetRandomValue(-10, 10),
                                          e.pos.y - (float)GetRandomValue(0, 20) };
                                coins.push_back(c);
                            }

                            if (e.type == EnemyType::BOSS) {
                                bossDefeated = true;
                                state = GameState::VICTORY;
                            }
                        }
                    }
                }
            }

            // Mage projectiles (piercing, 1 hit per enemy, NO hitstop)
            if (playerClass == PlayerClass::MAGE) {
                for (auto& p : projectiles) {
                    if (!p.active) continue;
                    for (auto& e : enemies) {
                        if (!e.alive) continue;
                        Rectangle er = MakeRect(e.pos, e.size);
                        if (CheckCollisionCircleRec(p.pos, p.radius, er)) {
                            if (e.lastProjectileHitId != p.id) {
                                e.lastProjectileHitId = p.id;

                                e.hp -= p.damage;
                                if (IsAudioDeviceReady()) PlaySound(sfxHit);
                                // No hitstop so projectile keeps flying

                                if (e.hp <= 0) {
                                    e.alive = false;

                                    int coinCount = 1;
                                    if (e.type == EnemyType::TANK) coinCount = 3;
                                    if (e.type == EnemyType::BOSS) coinCount = 10;
                                    for (int i = 0; i < coinCount; ++i) {
                                        Coin c{};
                                        c.pos = { e.pos.x + (float)GetRandomValue(-10, 10),
                                                  e.pos.y - (float)GetRandomValue(0, 20) };
                                        coins.push_back(c);
                                    }

                                    if (e.type == EnemyType::BOSS) {
                                        bossDefeated = true;
                                        state = GameState::VICTORY;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // -------- COINS ----------
            for (auto& c : coins) {
                if (c.collected) continue;
                Rectangle cr = { c.pos.x - 6, c.pos.y - 6, 12, 12 };
                pr = MakeRect(player.pos, player.size);
                if (RectOverlap(cr, pr)) {
                    c.collected = true;
                    player.coins++;
                }
            }

            // -------- HP / Game Over ----------
            if (player.hp <= 0) {
                state = GameState::GAMEOVER;
            }

            // Update camera
            camera.target = { player.pos.x, (GROUND_TOP + GROUND_BOTTOM) * 0.5f };

        } else if (state == GameState::SHOP) {
            if (IsKeyPressed(KEY_DOWN)) {
                shopSelection++;
                if (shopSelection > 2) shopSelection = 0;
            }
            if (IsKeyPressed(KEY_UP)) {
                shopSelection--;
                if (shopSelection < 0) shopSelection = 2;
            }

            if (IsKeyPressed(KEY_ENTER)) {
                int cost = GetUpgradeCost(player, shopSelection);
                if (player.coins >= cost) {
                    player.coins -= cost;
                    ApplyUpgrade(player, shopSelection);
                }
            }

            if (IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_ESCAPE)) {
                state = GameState::PLAYING;
            }

        } else if (state == GameState::GAMEOVER) {
            if (IsKeyPressed(KEY_ENTER)) {
                ResetGame();
                state = GameState::PLAYING;
            }
        } else if (state == GameState::VICTORY) {
            if (IsKeyPressed(KEY_ENTER)) {
                ResetGame();
                state = GameState::PLAYING;
            }
        }

        // =========================
        // DRAW
        // =========================
        BeginDrawing();
        ClearBackground(BLACK);

        if (state == GameState::MENU) {
            DrawText("2.5D PIXEL BEAT 'EM UP", screenWidth / 2 - 230, 120, 30, RAYWHITE);
            DrawText("Use LEFT / RIGHT to choose a character, ENTER to start",
                     screenWidth / 2 - 360, 170, 20, GRAY);

            int startX = screenWidth / 2 - 300;
            int y = 260;

            for (int i = 0; i < (int)classes.size(); ++i) {
                auto& cc = classes[i];
                int x = startX + i * 220;

                Color frameColor = (i == selectedClassIndex) ? YELLOW : DARKGRAY;
                DrawRectangleLines(x, y, 180, 220, frameColor);

                DrawText(cc.name.c_str(), x + 20, y + 10, 22, RAYWHITE);

                DrawRectangle(x + 70, y + 50, 40, 70, cc.color);
                DrawCircle(x + 90, y + 50, 18, cc.color);

                DrawText(TextFormat("HP: %d", cc.maxHP), x + 20, y + 140, 18, LIGHTGRAY);
                DrawText(TextFormat("SPD: %.0f", cc.speed), x + 20, y + 165, 18, LIGHTGRAY);
                DrawText(TextFormat("DMG: %d", cc.baseDamage), x + 20, y + 190, 18, LIGHTGRAY);
            }

            // Controls tutorial (bottom)
            int tutorialX = screenWidth / 2 - 280;
            int tutorialY = 500;

            DrawText("CONTROLS:", tutorialX, tutorialY, 24, YELLOW);
            DrawText("- MOVE:  W / A / S / D   or   Arrow Keys", tutorialX, tutorialY + 40, 20, RAYWHITE);
            DrawText("- ATTACK / COMBO:  J", tutorialX, tutorialY + 70, 20, RAYWHITE);
            DrawText("- SPECIAL:  K  (Block / Dodge / Blink)", tutorialX, tutorialY + 100, 20, RAYWHITE);
            DrawText("- SHOP:  TAB", tutorialX, tutorialY + 130, 20, RAYWHITE);
            DrawText("- GOAL: Reach the far right and defeat the boss", tutorialX, tutorialY + 160, 20, RAYWHITE);

        } else {
            BeginMode2D(camera);

            // Background (simple)
            float bgParallax = 0.4f;
            float bgX = -camera.target.x * bgParallax;
            DrawRectangle((int)bgX - 2000, 0, 4000, screenHeight, DARKBLUE);
            DrawRectangle((int)bgX - 2000, 200, 4000, 200, DARKPURPLE);

            // Ground
            DrawRectangle(-10000, (int)GROUND_BOTTOM, 20000, screenHeight - (int)GROUND_BOTTOM, DARKBROWN);
            DrawRectangle(-10000, (int)GROUND_TOP, 20000, (int)(GROUND_BOTTOM - GROUND_TOP), BROWN);
            DrawLine(-10000, (int)((GROUND_TOP + GROUND_BOTTOM) * 0.5f),
                     10000, (int)((GROUND_TOP + GROUND_BOTTOM) * 0.5f), DARKBROWN);

            // Coins
            for (auto& c : coins) {
                if (c.collected) continue;

                if (texCoin.width > 0) {
                    float scale = 1.5f;
                    Rectangle src = { 0, 0, (float)texCoin.width, (float)texCoin.height };
                    Rectangle dst = { c.pos.x, c.pos.y, texCoin.width * scale, texCoin.height * scale };
                    Vector2 origin = { texCoin.width * scale * 0.5f, texCoin.height * scale * 0.5f };
                    DrawTexturePro(texCoin, src, dst, origin, 0.0f, WHITE);
                } else {
                    DrawCircle((int)c.pos.x, (int)GROUND_BOTTOM + 3, 4, BLACK);
                    DrawCircle((int)c.pos.x, (int)c.pos.y, 6, GOLD);
                }
            }

            // Projectiles (Mage)
            for (auto& p : projectiles) {
                if (!p.active) continue;

                if (texProjectile.width > 0) {
                    float scale = 1.0f;
                    Rectangle src = { 0, 0, (float)texProjectile.width, (float)texProjectile.height };
                    Rectangle dst = { p.pos.x, p.pos.y, texProjectile.width * scale, texProjectile.height * scale };
                    Vector2 origin = { texProjectile.width * scale * 0.5f, texProjectile.height * scale * 0.5f };
                    DrawTexturePro(texProjectile, src, dst, origin, 0.0f, WHITE);
                } else {
                    DrawCircle((int)p.pos.x, (int)p.pos.y, p.radius + 4.0f, DARKPURPLE);
                    DrawCircle((int)p.pos.x, (int)p.pos.y, p.radius, SKYBLUE);
                }
            }

            // Sort entities by Y (fake 2.5D layering)
            struct DrawEntity {
                float y;
                bool isPlayer;
                Enemy* enemy;
            };
            std::vector<DrawEntity> entities;
            entities.reserve(enemies.size() + 1);

            for (auto& e : enemies) {
                if (!e.alive) continue;
                entities.push_back({ e.pos.y, false, &e });
            }
            entities.push_back({ player.pos.y, true, nullptr });

            std::sort(entities.begin(), entities.end(),
                      [](const DrawEntity& a, const DrawEntity& b) { return a.y < b.y; });

            for (auto& ent : entities) {
                if (ent.isPlayer) {
// Shadow under the player's feet (follows lane)
DrawEllipse((int)player.pos.x, (int)player.pos.y + 3, 30, 10, { 0, 0, 0, 120 });


                    Color baseCol = classes[selectedClassIndex].color;
                    if (player.blocking)      baseCol = Fade(baseCol, 0.7f);
                    if (player.dodging)       baseCol = SKYBLUE;
                    if (player.invincible)    baseCol = Fade(baseCol, 0.6f);

                    Vector2 drawPos = player.pos;

                    // Simple per-class body motion (lean / bob)
                    if (player.attacking) {
                        float atkPhase = player.attackDuration > 0.0f
                            ? player.attackTimer / player.attackDuration
                            : 0.0f;
                        if (atkPhase < 0.0f) atkPhase = 0.0f;
                        if (atkPhase > 1.0f) atkPhase = 1.0f;
                        float swing = std::sin(atkPhase * PI);
                        float dirSign = player.facingRight ? 1.0f : -1.0f;

                        if (playerClass == PlayerClass::KNIGHT) {
                            drawPos.x += swing * 6.0f * dirSign;
                        } else if (playerClass == PlayerClass::ROGUE) {
                            drawPos.x += swing * 10.0f * dirSign;
                            drawPos.y -= swing * 4.0f;
                        } else if (playerClass == PlayerClass::MAGE) {
                            drawPos.y -= swing * 5.0f;
                        }
                    }

                    if (player.sprite && player.sprite->width > 0) {
                        int frameWidth  = player.sprite->width / PLAYER_SPRITE_COLS;
                        int frameHeight = player.sprite->height / PLAYER_SPRITE_ROWS;

                        Rectangle src = {
                            (float)(frameWidth * player.animFrame),
                            (float)(frameHeight * player.animRow),
                            (float)(frameWidth * (player.facingRight ? 1 : -1)),
                            (float)frameHeight
                        };

                        float scale = 2.5f;
                        Rectangle dst = {
                            drawPos.x,
                            drawPos.y,
                            frameWidth * scale,
                            frameHeight * scale
                        };

                        Vector2 origin = { frameWidth * scale * 0.5f, frameHeight * scale };
                        DrawTexturePro(*player.sprite, src, dst, origin, 0.0f, WHITE);
                    } else {
                        // Fallback: old rectangles if no sprite
                        Rectangle body = MakeRect(drawPos, player.size);
                        DrawRectangleRec(body, baseCol);
                        DrawCircle((int)drawPos.x,
                                   (int)(drawPos.y - player.size.y + 15),
                                   18,
                                   baseCol);
                    }

                    // Debug melee hitbox
                    if ((playerClass == PlayerClass::KNIGHT || playerClass == PlayerClass::ROGUE)
                        && player.attacking) {
                        DrawRectangleLinesEx(
                            player.attackHitbox,
                            2.0f,
                            (playerClass == PlayerClass::KNIGHT && player.comboStep == 3) ? ORANGE : YELLOW
                        );
                    }

                } else {
                    Enemy* e = ent.enemy;
                    Rectangle er = MakeRect(e->pos, e->size);

                    // Shadow
DrawEllipse((int)e->pos.x, (int)e->pos.y + 3,
            (int)(e->size.x * 0.8f), 10, { 0, 0, 0, 120 });


                    Color col = RED;
                    if (e->type == EnemyType::FAST) col = ORANGE;
                    else if (e->type == EnemyType::TANK) col = MAROON;
                    else if (e->type == EnemyType::BOSS) col = DARKPURPLE;

                    // Sprite
                    if (e->sprite && e->sprite->width > 0) {
                        int frameWidth  = e->sprite->width / ENEMY_SPRITE_COLS;
                        int frameHeight = e->sprite->height / ENEMY_SPRITE_ROWS;

                        bool faceRight = (player.pos.x >= e->pos.x);
                        Rectangle src = {
                            (float)(frameWidth * e->animFrame),
                            (float)(frameHeight * e->animRow),
                            (float)(frameWidth * (faceRight ? 1 : -1)),
                            (float)frameHeight
                        };

                        float scale = 2.3f;
                        Rectangle dst = {
                            e->pos.x,
                            e->pos.y,
                            frameWidth * scale,
                            frameHeight * scale
                        };
                        Vector2 origin = { frameWidth * scale * 0.5f, frameHeight * scale };
                        DrawTexturePro(*e->sprite, src, dst, origin, 0.0f, WHITE);
                    } else {
                        DrawRectangleRec(er, col);
                    }

                    if (e->attackingAnim) {
                        DrawRectangleLinesEx(er, 3.0f, RED);
                    }

                    float hpRatio = (float)e->hp / (float)e->maxHP;
                    DrawRectangle((int)er.x, (int)(er.y - 8), (int)er.width, 5, DARKGRAY);
                    DrawRectangle((int)er.x, (int)(er.y - 8), (int)(er.width * hpRatio), 5, RED);
                }
            }

            // Level end gate
            DrawRectangle((int)(LEVEL_LENGTH + 20), (int)GROUND_TOP - 40,
                          40, (int)(GROUND_BOTTOM - GROUND_TOP + 40), GRAY);

            EndMode2D();

            // ----- HUD -----
            DrawRectangle(20, 20, 260, 24, DARKGRAY);
            float hpRatio = (float)player.hp / (float)player.maxHP;
            DrawRectangle(20, 20, (int)(260 * hpRatio), 24, RED);
            DrawRectangleLines(20, 20, 260, 24, BLACK);
            DrawText(TextFormat("%s HP: %d/%d", player.name.c_str(), player.hp, player.maxHP),
                     26, 24, 18, RAYWHITE);

            DrawText(TextFormat("Coins: %d", player.coins), 20, 60, 22, GOLD);

            if (player.comboStep > 0 && player.comboTimer < COMBO_RESET_TIME) {
                DrawText(TextFormat("COMBO x%d", player.comboStep), 20, 90, 24, YELLOW);
            }

            if (bossSpawned && !bossDefeated) {
                DrawText("BOSS FIGHT!", screenWidth / 2 - 80, 20, 24, MAROON);
            }

            DrawText("Press TAB for Shop", screenWidth - 260, 20, 20, LIGHTGRAY);

            // Shop overlay
            if (state == GameState::SHOP) {
                DrawRectangle(200, 140, screenWidth - 400, screenHeight - 280, Fade(BLACK, 0.85f));
                DrawRectangleLines(200, 140, screenWidth - 400, screenHeight - 280, YELLOW);

                DrawText("SHOP", screenWidth / 2 - 40, 160, 28, YELLOW);
                DrawText(TextFormat("Coins: %d", player.coins), 220, 200, 22, GOLD);
                DrawText("UP/DOWN: select   ENTER: buy   TAB/ESC: back", 220, 230, 18, RAYWHITE);

                int listY = 270;
                for (int i = 0; i < 3; ++i) {
                    Color col = (shopSelection == i) ? SKYBLUE : RAYWHITE;
                    int cost = GetUpgradeCost(player, i);
                    std::string label = shopOptions[i].label + " (Cost: " + std::to_string(cost) + ")";
                    DrawText(label.c_str(), 240, listY + i * 40, 22, col);
                }
            }

            if (state == GameState::GAMEOVER) {
                DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, 0.6f));
                DrawText("YOU DIED", screenWidth / 2 - 80, screenHeight / 2 - 20, 36, RED);
                DrawText("Press ENTER to restart", screenWidth / 2 - 150, screenHeight / 2 + 20, 22, RAYWHITE);
            }

            if (state == GameState::VICTORY) {
                DrawRectangle(0, 0, screenWidth, screenHeight, Fade(BLACK, 0.6f));
                DrawText("BOSS DEFEATED!", screenWidth / 2 - 140, screenHeight / 2 - 20, 32, SKYBLUE);
                DrawText("Press ENTER to play again", screenWidth / 2 - 170, screenHeight / 2 + 20, 22, RAYWHITE);
            }
        }

        EndDrawing();
    }

    // Cleanup textures
    UnloadTexture(texKnight);
    UnloadTexture(texRogue);
    UnloadTexture(texMage);
    UnloadTexture(texEnemyGrunt);
    UnloadTexture(texEnemyFast);
    UnloadTexture(texEnemyTank);
    UnloadTexture(texEnemyBoss);
    UnloadTexture(texCoin);
    UnloadTexture(texProjectile);

    // Cleanup sounds
    UnloadSound(sfxKnightSwing);
    UnloadSound(sfxRogueSwing);
    UnloadSound(sfxMageCast);
    UnloadSound(sfxHit);
    UnloadSound(sfxEnemySwing);
    UnloadSound(sfxBlock);
    UnloadSound(sfxDodge);
    UnloadSound(sfxBlink);

    CloseAudioDevice();
    CloseWindow();
    return 0;
}
