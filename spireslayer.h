#ifndef SPIRESLAYER_H
#define SPIRESLAYER_H

#include <stdbool.h>

/*
 * spireslayer.h - Global type definitions and constants for the Spireslayer interpreter.
 *
 * This header defines the core data structures that represent the entire game world.
 * It is included by all compilation units (main.c, parser.c, state.c) to ensure
 * consistent type definitions across the project.
 *
 * Design Decision: We use dynamically allocated arrays (realloc) rather than linked
 * lists for cards, relics, and codex entries. This provides O(1) random access,
 * cache-friendly memory layout, and simpler sorting (qsort) for alphabetical output.
 */

/* Maximum number of characters in a single input line, as specified in the assignment PDF */
#define MAX_LINE_LENGTH 1024

/* The potion belt can hold at most 3 potions simultaneously.
 * Attempting to gain a potion when the belt is full prints "Potion belt is full". */
#define POTION_BELT_SIZE 3

/*
 * RoomType - Enumeration of all valid room types a player can enter.
 *
 * ROOM_NONE is the initial state before any room has been entered.
 * The seven concrete room types correspond to: Monster, Elite, Rest,
 * Shop, Treasure, Event, and Boss. Room names must match exactly
 * (case-sensitive) in the grammar.
 */
typedef enum {
    ROOM_NONE,      /* Initial state - no room entered yet; "Where ?" returns "NONE" */
    ROOM_MONSTER,   /* "Ironclad enters Monster room" */
    ROOM_ELITE,     /* "Ironclad enters Elite room" */
    ROOM_REST,      /* "Ironclad enters Rest room" */
    ROOM_SHOP,      /* "Ironclad enters Shop room" */
    ROOM_TREASURE,  /* "Ironclad enters Treasure room" */
    ROOM_EVENT,     /* "Ironclad enters Event room" */
    ROOM_BOSS       /* "Ironclad enters Boss room" */
} RoomType;

/*
 * EntityType - Categorizes items recorded in the codex as effective against enemies.
 *
 * Used by the "learns" commands:
 *   "Ironclad learns card/relic/potion <name> is effective against <enemy>"
 * and by the fight mechanic to determine victory conditions.
 */
typedef enum {
    TYPE_CARD,      /* A card that is effective against an enemy */
    TYPE_RELIC,     /* A relic that is effective against an enemy */
    TYPE_POTION     /* A potion that is effective against an enemy */
} EntityType;

/*
 * DeckEntry - Represents a unique card name in the player's deck.
 *
 * Rather than storing individual card copies, we track counts:
 *   - base_count:     number of non-upgraded copies
 *   - upgraded_count:  number of upgraded copies (displayed with '+' suffix)
 *   - exhausts:       if true, this card is tagged for exhaust consumption in fights
 *                     (displayed with '*' suffix in Deck listing)
 *
 * Example: 2 copies of "Bash" (1 base, 1 upgraded) with exhaust tag
 *          would display as "1 Bash*, 1 Bash+*"
 */
typedef struct {
    char* name;           /* Dynamically allocated card name string */
    int base_count;       /* Number of non-upgraded copies in the deck */
    int upgraded_count;   /* Number of upgraded copies (shown as Name+) */
    bool exhausts;        /* Whether this card has the exhaust tag (shown as Name*) */
} DeckEntry;

/*
 * EffectivenessEntry - A single "X is effective against enemy" record.
 *
 * Stored inside a CodexEntry. Each entry records what type of item
 * (card, relic, or potion) and its name. During fights, the system
 * checks if the player possesses any of these items to determine victory.
 */
typedef struct {
    EntityType type;      /* TYPE_CARD, TYPE_RELIC, or TYPE_POTION */
    char* name;           /* Dynamically allocated item name */
} EffectivenessEntry;

/*
 * CodexEntry - Knowledge about a specific enemy in the codex.
 *
 * Contains:
 *   - The enemy name
 *   - How many times this enemy has been defeated (for "Defeated <enemy> ?" queries)
 *   - A dynamic array of effectiveness entries (items that counter this enemy)
 *
 * The codex is populated via "Ironclad learns ..." commands and consulted
 * during "Ironclad fights ..." to determine victory or defeat.
 */
typedef struct {
    char* name;                   /* Dynamically allocated enemy name */
    int defeated_count;           /* Number of times this enemy has been defeated */
    EffectivenessEntry* effects;  /* Dynamic array of effectiveness records */
    int effect_count;             /* Current number of effectiveness records */
    int effect_capacity;          /* Allocated capacity (doubles on overflow) */
} CodexEntry;

/*
 * WorldState - The complete game state, persisted across all commands.
 *
 * This struct represents the entire "world" of the Spireslayer game:
 *   - Player stats: gold, HP, max HP
 *   - Dungeon progress: floor number, current room type
 *   - Inventory: deck (cards), relics, potions (belt of 3)
 *   - Knowledge: codex (enemy effectiveness records)
 *   - Exhaust tracking: global list of card names marked as exhaust
 *
 * All dynamic arrays start with capacity 0 and grow via doubling (amortized O(1) insert).
 * Every realloc is guarded against NULL returns to prevent memory loss on OOM.
 *
 * Initial values (set by init_world_state):
 *   - HP: 80/80, Gold: 0, Floor: 0, Room: NONE
 *   - All collections empty
 */
typedef struct {
    int total_gold;       /* Accumulated gold (can only increase via gains/bounties/sells) */
    int hp;               /* Current hit points (clamped at 0 and max_hp) */
    int max_hp;           /* Maximum hit points (starts at 80, increased by "gains N max hp") */
    int floor;            /* Current floor number (incremented on each room entry) */
    RoomType current_room; /* Most recently entered room type */

    /* Dynamic array of unique card entries in the deck */
    DeckEntry* deck;      /* Array of DeckEntry structs */
    int deck_count;       /* Number of distinct card names tracked */
    int deck_capacity;    /* Allocated capacity (grows by doubling) */

    /* Dynamic array of relic names (each relic is unique - duplicates rejected) */
    char** relics;        /* Array of dynamically allocated relic name strings */
    int relic_count;      /* Number of relics currently held */
    int relic_capacity;   /* Allocated capacity (grows by doubling) */

    /* Fixed-size potion belt (max 3 potions) */
    char* potions[POTION_BELT_SIZE]; /* Array of potion name strings (NULL if empty) */
    int potion_count;     /* Number of potions currently in the belt (0-3) */

    /* Global exhaust tag list - card names marked as "exhaust" */
    char** global_exhausts;       /* Array of card name strings that have the exhaust tag */
    int global_exhausts_count;    /* Number of exhaust-tagged card names */
    int global_exhausts_capacity; /* Allocated capacity (grows by doubling) */

    /* Codex: enemy knowledge database */
    CodexEntry* codex;    /* Dynamic array of CodexEntry structs */
    int codex_count;      /* Number of known enemies */
    int codex_capacity;   /* Allocated capacity (grows by doubling) */

} WorldState;

/* Initialize all WorldState fields to their starting values */
void init_world_state(WorldState* state);

/* Free all dynamically allocated memory within the WorldState */
void cleanup_world_state(WorldState* state);

#endif
