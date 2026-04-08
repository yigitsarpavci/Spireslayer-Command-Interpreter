#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spireslayer.h"
#include "state.h"

/*
 * CMPE230 - Assignment 1: Spireslayer World State Operations
 * Authors: 2023400048, 2023400210
 *
 * This file implements all semantic operations on the game state. It serves as
 * the core state manager — each function performs exactly one game action
 * and prints the corresponding response to stdout.
 *
 * Architecture:
 *   - Dynamic arrays (deck, relics, codex, exhausts) use a doubling strategy
 *     for amortized O(1) insertion. Initial capacity is 0, first allocation is 8.
 *   - All realloc calls are guarded via a temporary pointer pattern:
 *       void* t = realloc(ptr, new_size);
 *       if(!t) { FATAL ERROR }
 *       ptr = t;
 *     This prevents memory leaks if realloc fails (the original block is preserved).
 *   - The potion belt is a fixed-size array of 3 slots (POTION_BELT_SIZE).
 *   - HP is clamped: never exceeds max_hp, never drops below 0.
 *   - The codex tracks enemy knowledge and defeat counts.
 *
 * Memory Safety:
 *   - All dynamically allocated strings are freed in cleanup_world_state().
 *   - strdup() is used for all string storage to ensure independent ownership.
 */

/* ============================================================
 * INITIALIZATION AND CLEANUP
 * ============================================================ */

/*
 * init_world_state - Set all game state fields to their starting values.
 *
 * Initial conditions as defined by the assignment specification:
 *   - HP: 80/80 (current/max)
 *   - Gold: 0
 *   - Floor: 0 (no rooms entered yet)
 *   - Current Room: NONE
 *   - Deck, Relics, Potions, Exhausts, Codex: all empty
 */
void init_world_state(WorldState* state) {
    state->total_gold = 0;
    state->hp = 80;
    state->max_hp = 80;
    state->floor = 0;
    state->current_room = ROOM_NONE;
    
    state->deck = NULL;
    state->deck_count = 0;
    state->deck_capacity = 0;
    
    state->relics = NULL;
    state->relic_count = 0;
    state->relic_capacity = 0;
    
    state->potion_count = 0;
    for(int i=0; i<POTION_BELT_SIZE; i++) state->potions[i] = NULL;
    
    state->global_exhausts = NULL;
    state->global_exhausts_count = 0;
    state->global_exhausts_capacity = 0;
    
    state->codex = NULL;
    state->codex_count = 0;
    state->codex_capacity = 0;
}

/*
 * cleanup_world_state - Free all dynamically allocated memory.
 *
 * Iterates through every dynamic collection and frees:
 *   - Each card name string + the deck array itself
 *   - Each relic name string + the relics array
 *   - Each potion name string
 *   - Each exhaust tag string + the exhausts array
 *   - Each codex entry (enemy name, each effectiveness entry name, effects array)
 *     + the codex array itself
 *
 * Called on both "Exit" command and EOF to ensure no memory leaks.
 */
void cleanup_world_state(WorldState* state) {
    /* Free deck entries */
    for(int i=0; i<state->deck_count; i++) free(state->deck[i].name);
    free(state->deck);
    
    /* Free relic name strings */
    for(int i=0; i<state->relic_count; i++) free(state->relics[i]);
    free(state->relics);
    
    /* Free potion name strings */
    for(int i=0; i<state->potion_count; i++) free(state->potions[i]);
    
    /* Free exhaust tag strings */
    for(int i=0; i<state->global_exhausts_count; i++) free(state->global_exhausts[i]);
    free(state->global_exhausts);
    
    /* Free codex entries (each has a name, effects array, and effect name strings) */
    for(int i=0; i<state->codex_count; i++) {
        free(state->codex[i].name);
        for(int j=0; j<state->codex[i].effect_count; j++) {
            free(state->codex[i].effects[j].name);
        }
        free(state->codex[i].effects);
    }
    free(state->codex);
}

/* ============================================================
 * INTERNAL HELPER FUNCTIONS
 * ============================================================ */

/*
 * is_exhaust - Check if a card name is in the global exhaust list.
 *
 * Used when creating new DeckEntry objects to inherit the exhaust tag
 * if it was previously set (e.g., card is marked as exhaust, then later
 * a new copy of the same card is gained — it should also have the tag).
 *
 * @param state  Current game state.
 * @param name   Card name to check.
 * @return       true if the card name is in the exhaust list.
 */
static bool is_exhaust(WorldState* state, const char* name) {
    for(int i=0; i<state->global_exhausts_count; i++) {
        if(strcmp(state->global_exhausts[i], name) == 0) return true;
    }
    return false;
}

/*
 * get_or_create_deck_entry - Find an existing DeckEntry or create a new one.
 *
 * Searches the deck for a card with the given name. If found, returns it.
 * If not found, expands the deck array if needed (doubling capacity), creates
 * a new entry with base_count=0, upgraded_count=0, and inherits the exhaust
 * tag from the global list if applicable.
 *
 * @param state  Current game state.
 * @param name   Card name to find or create.
 * @return       Pointer to the existing or newly created DeckEntry.
 */
static DeckEntry* get_or_create_deck_entry(WorldState* state, const char* name) {
    /* Search for existing entry */
    for(int i=0; i<state->deck_count; i++) {
        if(strcmp(state->deck[i].name, name) == 0) {
            return &state->deck[i];
        }
    }

    /* Grow array if needed (doubling strategy for amortized O(1)) */
    if(state->deck_count >= state->deck_capacity) {
        state->deck_capacity = state->deck_capacity == 0 ? 8 : state->deck_capacity * 2;
        void* t = realloc(state->deck, state->deck_capacity * sizeof(DeckEntry));
        if(!t) { fprintf(stderr, "FATAL: Out of memory\n"); exit(1); }
        state->deck = t;
    }

    /* Initialize new entry */
    DeckEntry* entry = &state->deck[state->deck_count++];
    entry->name = strdup(name);
    entry->base_count = 0;
    entry->upgraded_count = 0;
    entry->exhausts = is_exhaust(state, name);  /* Inherit exhaust tag if previously set */
    return entry;
}

/* ============================================================
 * GOLD OPERATIONS
 * ============================================================ */

/*
 * handle_gain_gold - Process "Ironclad gains <N> gold".
 * Adds the specified amount to the total gold counter.
 * Output: "Gold obtained"
 */
void handle_gain_gold(WorldState* state, int amount) {
    /* Defensive check to prevent overflow beyond signed 32-bit max (2147483647) */
    if (2147483647LL - state->total_gold < amount) {
        state->total_gold = 2147483647;
    } else {
        state->total_gold += amount;
    }
    printf("Gold obtained\n");
}

/* ============================================================
 * CARD OPERATIONS
 * ============================================================ */

/*
 * handle_gain_card - Process "Ironclad gains card <Name>".
 * Adds one base (non-upgraded) copy of the card to the deck.
 * If the card name is new, a DeckEntry is created.
 * Output: "Card added: <Name>"
 */
void handle_gain_card(WorldState* state, const char* name) {
    DeckEntry* entry = get_or_create_deck_entry(state, name);
    entry->base_count++;
    printf("Card added: %s\n", name);
}

/* ============================================================
 * RELIC OPERATIONS
 * ============================================================ */

/*
 * handle_gain_relic - Process "Ironclad gains relic <Name>".
 * Relics are unique — duplicates are rejected.
 * Output: "Relic obtained: <Name>" on success,
 *         "Already has relic: <Name>" if duplicate.
 */
void handle_gain_relic(WorldState* state, const char* name) {
    /* Check for duplicate */
    for(int i=0; i<state->relic_count; i++) {
        if(strcmp(state->relics[i], name) == 0) {
            printf("Already has relic: %s\n", name);
            return;
        }
    }

    /* Grow array if needed */
    if(state->relic_count >= state->relic_capacity) {
        state->relic_capacity = state->relic_capacity == 0 ? 8 : state->relic_capacity * 2;
        void* t = realloc(state->relics, state->relic_capacity * sizeof(char*));
        if(!t) { fprintf(stderr, "FATAL: Out of memory\n"); exit(1); }
        state->relics = t;
    }

    state->relics[state->relic_count++] = strdup(name);
    printf("Relic obtained: %s\n", name);
}

/* ============================================================
 * POTION OPERATIONS
 * ============================================================ */

/*
 * handle_gain_potion - Process "Ironclad gains potion <Name>".
 * The potion belt holds at most POTION_BELT_SIZE (3) potions.
 * Unlike relics, duplicate potions ARE allowed.
 * Output: "Potion obtained: <Name>" on success,
 *         "Potion belt is full" if capacity reached.
 */
void handle_gain_potion(WorldState* state, const char* name) {
    if(state->potion_count >= POTION_BELT_SIZE) {
        printf("Potion belt is full\n");
        return;
    }
    state->potions[state->potion_count++] = strdup(name);
    printf("Potion obtained: %s\n", name);
}

/* ============================================================
 * BUY OPERATIONS (Card, Relic, Potion)
 * All buy operations check gold sufficiency first.
 * ============================================================ */

/*
 * handle_buy_card - Process "Ironclad buys card <Name> for <Price> gold".
 * Deducts gold and adds one base copy.
 * Output: "Not enough gold" if insufficient, "Card added: <Name>" on success.
 */
void handle_buy_card(WorldState* state, const char* name, int price) {
    if(state->total_gold < price) {
        printf("Not enough gold\n");
        return;
    }
    state->total_gold -= price;
    DeckEntry* entry = get_or_create_deck_entry(state, name);
    entry->base_count++;
    printf("Card added: %s\n", name);
}

/*
 * handle_buy_relic - Process "Ironclad buys relic <Name> for <Price> gold".
 * Checks gold AND uniqueness (already-owned relics are rejected WITHOUT deducting gold).
 * Note: Gold check happens BEFORE uniqueness check.
 */
void handle_buy_relic(WorldState* state, const char* name, int price) {
    if(state->total_gold < price) {
        printf("Not enough gold\n");
        return;
    }

    /* Check for duplicate relic */
    for(int i=0; i<state->relic_count; i++) {
        if(strcmp(state->relics[i], name) == 0) {
            printf("Already has relic: %s\n", name);
            return;
        }
    }

    state->total_gold -= price;

    /* Grow array if needed */
    if(state->relic_count >= state->relic_capacity) {
        state->relic_capacity = state->relic_capacity == 0 ? 8 : state->relic_capacity * 2;
        void* t = realloc(state->relics, state->relic_capacity * sizeof(char*));
        if(!t) { fprintf(stderr, "FATAL: Out of memory\n"); exit(1); }
        state->relics = t;
    }

    state->relics[state->relic_count++] = strdup(name);
    printf("Relic obtained: %s\n", name);
}

/*
 * handle_buy_potion - Process "Ironclad buys potion <Name> for <Price> gold".
 * Checks gold AND belt capacity (full belt is rejected WITHOUT deducting gold).
 */
void handle_buy_potion(WorldState* state, const char* name, int price) {
    if(state->total_gold < price) {
        printf("Not enough gold\n");
        return;
    }
    if(state->potion_count >= POTION_BELT_SIZE) {
        printf("Potion belt is full\n");
        return;
    }
    state->total_gold -= price;
    state->potions[state->potion_count++] = strdup(name);
    printf("Potion obtained: %s\n", name);
}

/* ============================================================
 * CARD REMOVAL AND UPGRADE
 * ============================================================ */

/*
 * handle_remove_card - Process "Ironclad removes card <Name>".
 * Removes one base (non-upgraded) copy from the deck.
 * Output: "Card removed: <Name>" or "Card not found: <Name>".
 */
void handle_remove_card(WorldState* state, const char* name) {
    for(int i=0; i<state->deck_count; i++) {
        if(strcmp(state->deck[i].name, name) == 0) {
            if(state->deck[i].base_count > 0) {
                state->deck[i].base_count--;
                printf("Card removed: %s\n", name);
                return;
            }
        }
    }
    printf("Card not found: %s\n", name);
}

/*
 * handle_remove_upgraded_card - Process "Ironclad removes upgraded card <Name>".
 * Removes one upgraded copy from the deck.
 * Output: "Upgraded card removed: <Name>" or "Upgraded card not found: <Name>".
 */
void handle_remove_upgraded_card(WorldState* state, const char* name) {
    for(int i=0; i<state->deck_count; i++) {
        if(strcmp(state->deck[i].name, name) == 0) {
            if(state->deck[i].upgraded_count > 0) {
                state->deck[i].upgraded_count--;
                printf("Upgraded card removed: %s\n", name);
                return;
            }
        }
    }
    printf("Upgraded card not found: %s\n", name);
}

/*
 * handle_upgrade_card - Process "Ironclad upgrades card <Name>".
 * Converts one base copy into an upgraded copy (base_count--, upgraded_count++).
 * Output: "Card upgraded: <Name>" or "Card not found: <Name>".
 */
void handle_upgrade_card(WorldState* state, const char* name) {
    for(int i=0; i<state->deck_count; i++) {
        if(strcmp(state->deck[i].name, name) == 0) {
            if(state->deck[i].base_count > 0) {
                state->deck[i].base_count--;
                state->deck[i].upgraded_count++;
                printf("Card upgraded: %s\n", name);
                return;
            }
        }
    }
    printf("Card not found: %s\n", name);
}

/* ============================================================
 * ROOM NAVIGATION
 * ============================================================ */

/*
 * handle_enter_room - Process "Ironclad enters <RoomType> room".
 * Increments the floor counter and updates the current room type.
 * Output: "Entered <RoomType> room"
 */
void handle_enter_room(WorldState* state, RoomType room) {
    state->floor++;
    state->current_room = room;
    const char* room_names[] = {"NONE", "Monster", "Elite", "Rest", "Shop", "Treasure", "Event", "Boss"};
    printf("Entered %s room\n", room_names[room]);
}

/* ============================================================
 * CODEX / EFFECTIVENESS SYSTEM
 * ============================================================ */

/*
 * get_or_create_codex - Find or create a codex entry for an enemy.
 *
 * If the enemy is already known, returns the existing entry and sets *is_new = false.
 * If the enemy is new, creates a fresh entry and sets *is_new = true.
 * This distinction is used by learn_effective to print "created" vs "updated".
 *
 * @param state   Current game state.
 * @param enemy   Enemy name to look up.
 * @param is_new  Output: set to true if a new entry was created.
 * @return        Pointer to the codex entry for this enemy.
 */
static CodexEntry* get_or_create_codex(WorldState* state, const char* enemy, bool* is_new) {
    /* Search for existing entry */
    for(int i=0; i<state->codex_count; i++) {
        if(strcmp(state->codex[i].name, enemy) == 0) {
            *is_new = false;
            return &state->codex[i];
        }
    }

    /* Grow array if needed */
    if(state->codex_count >= state->codex_capacity) {
        state->codex_capacity = state->codex_capacity == 0 ? 8 : state->codex_capacity * 2;
        void* t = realloc(state->codex, state->codex_capacity * sizeof(CodexEntry));
        if(!t) { fprintf(stderr, "FATAL: Out of memory\n"); exit(1); }
        state->codex = t;
    }

    /* Initialize new codex entry */
    CodexEntry* entry = &state->codex[state->codex_count++];
    entry->name = strdup(enemy);
    entry->defeated_count = 0;
    entry->effects = NULL;
    entry->effect_count = 0;
    entry->effect_capacity = 0;
    *is_new = true;
    return entry;
}

/*
 * learn_effective - Record that an item is effective against an enemy.
 *
 * Creates a codex entry for the enemy if it doesn't exist, then adds the
 * effectiveness record. Duplicate records (same type + same name for the
 * same enemy) are detected and rejected with "Effectiveness already noted".
 *
 * Output:
 *   - "Codex entry created: <Enemy>" (first time this enemy is seen)
 *   - "Codex entry updated: <Enemy>" (enemy already known, new record added)
 *   - "Effectiveness already noted" (duplicate record)
 *
 * @param state  Current game state.
 * @param type   TYPE_CARD, TYPE_RELIC, or TYPE_POTION.
 * @param name   Item name that is effective.
 * @param enemy  Enemy name this item is effective against.
 */
static void learn_effective(WorldState* state, EntityType type, const char* name, const char* enemy) {
    bool is_new;
    CodexEntry* entry = get_or_create_codex(state, enemy, &is_new);
    
    /* Check for duplicate effectiveness record */
    for(int i=0; i<entry->effect_count; i++) {
        if(entry->effects[i].type == type && strcmp(entry->effects[i].name, name) == 0) {
            printf("Effectiveness already noted\n");
            return;
        }
    }
    
    /* Grow effects array if needed */
    if(entry->effect_count >= entry->effect_capacity) {
        entry->effect_capacity = entry->effect_capacity == 0 ? 4 : entry->effect_capacity * 2;
        void* t = realloc(entry->effects, entry->effect_capacity * sizeof(EffectivenessEntry));
        if(!t) { fprintf(stderr, "FATAL: Out of memory\n"); exit(1); }
        entry->effects = t;
    }
    
    /* Add the new effectiveness record */
    entry->effects[entry->effect_count].type = type;
    entry->effects[entry->effect_count].name = strdup(name);
    entry->effect_count++;
    
    /* Print appropriate message based on whether this is a new or existing enemy */
    if(is_new) {
        printf("Codex entry created: %s\n", enemy);
    } else {
        printf("Codex entry updated: %s\n", enemy);
    }
}

/* Public wrappers for the three entity types */
void handle_learn_card_effective(WorldState* state, const char* name, const char* enemy) {
    learn_effective(state, TYPE_CARD, name, enemy);
}
void handle_learn_relic_effective(WorldState* state, const char* name, const char* enemy) {
    learn_effective(state, TYPE_RELIC, name, enemy);
}
void handle_learn_potion_effective(WorldState* state, const char* name, const char* enemy) {
    learn_effective(state, TYPE_POTION, name, enemy);
}

/* ============================================================
 * COMBAT SYSTEM
 * ============================================================ */

/*
 * handle_fight_enemy_impl - Core combat resolution logic.
 *
 * Victory Condition:
 *   The player wins if they possess ANY item (card, relic, or potion) that has
 *   been recorded as "effective against" the enemy in the codex. Specifically:
 *     - A card counts if base_count > 0 OR upgraded_count > 0
 *     - A relic counts if it's in the relics array
 *     - A potion counts if it's in the potion belt
 *
 * On Victory:
 *   1. Consume effective potions: remove one copy of each effective potion
 *   2. Consume exhausting cards: if an effective card has the exhaust tag,
 *      remove one copy (prefer base over upgraded)
 *   3. Increment the enemy's defeated_count
 *   4. Add bounty gold (if any)
 *   5. Print victory message
 *
 * On Defeat:
 *   1. Take exactly 15 HP damage (clamped at 0; reaching 0 is NOT game over)
 *   2. Print defeat message with remaining HP
 *
 * @param state   Current game state.
 * @param enemy   Name of the enemy being fought.
 * @param bounty  Gold reward on victory (0 if no bounty specified).
 */
void handle_fight_enemy_impl(WorldState* state, const char* enemy, int bounty) {
    CodexEntry* entry = NULL;

    /* Look up the enemy in the codex */
    for(int i=0; i<state->codex_count; i++) {
        if(strcmp(state->codex[i].name, enemy) == 0) {
            entry = &state->codex[i];
            break;
        }
    }
    
    /* Determine if the player can win: check each effectiveness entry
     * against the player's current inventory */
    bool win = false;
    if(entry) {
        for(int i=0; i<entry->effect_count; i++) {
            if(entry->effects[i].type == TYPE_CARD) {
                /* Check if player has this card in their deck */
                for(int j=0; j<state->deck_count; j++) {
                    if(strcmp(state->deck[j].name, entry->effects[i].name) == 0) {
                        if(state->deck[j].base_count > 0 || state->deck[j].upgraded_count > 0) {
                            win = true; break;
                        }
                    }
                }
            } else if(entry->effects[i].type == TYPE_RELIC) {
                /* Check if player has this relic */
                for(int j=0; j<state->relic_count; j++) {
                    if(strcmp(state->relics[j], entry->effects[i].name) == 0) {
                        win = true; break;
                    }
                }
            } else if(entry->effects[i].type == TYPE_POTION) {
                /* Check if player has this potion in their belt */
                for(int j=0; j<state->potion_count; j++) {
                    if(strcmp(state->potions[j], entry->effects[i].name) == 0) {
                        win = true; break;
                    }
                }
            }
            if(win) break;
        }
    }
    
    if(win) {
        /* === VICTORY: Process consumptions === */
        for(int i=0; i<entry->effect_count; i++) {
            if(entry->effects[i].type == TYPE_POTION) {
                /* Consume one copy of each effective potion from the belt */
                for(int j=0; j<state->potion_count; j++) {
                    if(strcmp(state->potions[j], entry->effects[i].name) == 0) {
                        free(state->potions[j]);
                        /* Shift remaining potions left to fill the gap */
                        for(int k=j; k<state->potion_count-1; k++) {
                            state->potions[k] = state->potions[k+1];
                        }
                        state->potion_count--;
                        break;  /* Consume only one copy */
                    }
                }
            } else if(entry->effects[i].type == TYPE_CARD) {
                /* Consume one copy of each exhausting effective card.
                 * Only cards with the exhaust tag are consumed.
                 * Prefer removing a base copy over an upgraded copy. */
                for(int j=0; j<state->deck_count; j++) {
                    if(strcmp(state->deck[j].name, entry->effects[i].name) == 0) {
                        if(state->deck[j].exhausts && (state->deck[j].base_count > 0 || state->deck[j].upgraded_count > 0)) {
                            if(state->deck[j].base_count > 0) {
                                state->deck[j].base_count--;
                            } else {
                                state->deck[j].upgraded_count--;
                            }
                        }
                        break;
                    }
                }
            }
            /* Relics are NOT consumed on use — they persist indefinitely */
        }

        /* Record the defeat and award bounty */
        entry->defeated_count++;
        if(bounty > 0) {
            state->total_gold += bounty;
            printf("Ironclad defeats %s and gains %d gold\n", enemy, bounty);
        } else {
            printf("Ironclad defeats %s\n", enemy);
        }
    } else {
        /* === DEFEAT: Take 15 damage, clamped at 0 HP ===
         * Note: Reaching 0 HP is NOT game over. The player can still
         * perform all actions including healing back above 0. */
        state->hp = (state->hp - 15) < 0 ? 0 : (state->hp - 15);
        printf("Ironclad is outmatched and flees with %d hp remaining\n", state->hp);
    }
}

/* Public wrappers for fights with and without bounty */
void handle_fight_enemy(WorldState* state, const char* enemy) {
    handle_fight_enemy_impl(state, enemy, 0);
}
void handle_fight_enemy_bounty(WorldState* state, const char* enemy, int bounty) {
    handle_fight_enemy_impl(state, enemy, bounty);
}

/* ============================================================
 * HP OPERATIONS
 * ============================================================ */

/*
 * handle_heal - Process "Ironclad heals <N> hp".
 * Increases HP by the specified amount, capped at max_hp.
 * Output: "Ironclad heals to <final_hp>"
 */
void handle_heal(WorldState* state, int amount) {
    /* Safe addition to prevent overflow, then clamped at max_hp per spec */
    if (2147483647LL - state->hp < amount) {
        state->hp = state->max_hp;
    } else {
        state->hp += amount;
        if(state->hp > state->max_hp) state->hp = state->max_hp;
    }
    printf("Ironclad heals to %d\n", state->hp);
}

/*
 * handle_take_damage - Process "Ironclad takes <N> damage".
 * Decreases HP by the specified amount, clamped at 0.
 * Reaching 0 HP does NOT end the game.
 * Output: "Ironclad health drops to <final_hp>"
 */
void handle_take_damage(WorldState* state, int amount) {
    state->hp -= amount;
    if(state->hp < 0) state->hp = 0;
    printf("Ironclad health drops to %d\n", state->hp);
}

/* ============================================================
 * POTION DISCARD AND SELL
 * ============================================================ */

/*
 * handle_discard_potion - Process "Ironclad discards potion <Name>".
 * Removes the potion from the belt without gaining gold.
 * Output: "Potion discarded: <Name>" or "Potion not found: <Name>".
 */
void handle_discard_potion(WorldState* state, const char* name) {
    for(int i=0; i<state->potion_count; i++) {
        if(strcmp(state->potions[i], name) == 0) {
            free(state->potions[i]);
            /* Shift remaining potions left */
            for(int j=i; j<state->potion_count-1; j++) {
                state->potions[j] = state->potions[j+1];
            }
            state->potion_count--;
            printf("Potion discarded: %s\n", name);
            return;
        }
    }
    printf("Potion not found: %s\n", name);
}

/* ============================================================
 * SELL OPERATIONS
 * ============================================================ */

/*
 * handle_sell_card - Process "Ironclad sells card <Name> for <Price> gold".
 * Removes one base copy and adds gold.
 * Output: "Card sold: <Name>" or "Card not found: <Name>".
 */
void handle_sell_card(WorldState* state, const char* name, int price) {
    for(int i=0; i<state->deck_count; i++) {
        if(strcmp(state->deck[i].name, name) == 0 && state->deck[i].base_count > 0) {
            state->deck[i].base_count--;
            state->total_gold += price;
            printf("Card sold: %s\n", name);
            return;
        }
    }
    printf("Card not found: %s\n", name);
}

/*
 * handle_sell_upgraded_card - Process "Ironclad sells upgraded card <Name> for <Price> gold".
 * Removes one upgraded copy and adds gold.
 * Output: "Upgraded card sold: <Name>" or "Upgraded card not found: <Name>".
 */
void handle_sell_upgraded_card(WorldState* state, const char* name, int price) {
    for(int i=0; i<state->deck_count; i++) {
        if(strcmp(state->deck[i].name, name) == 0 && state->deck[i].upgraded_count > 0) {
            state->deck[i].upgraded_count--;
            state->total_gold += price;
            printf("Upgraded card sold: %s\n", name);
            return;
        }
    }
    printf("Upgraded card not found: %s\n", name);
}

/*
 * handle_sell_potion - Process "Ironclad sells potion <Name> for <Price> gold".
 * Removes from belt and adds gold.
 * Output: "Potion sold: <Name>" or "Potion not found: <Name>".
 */
void handle_sell_potion(WorldState* state, const char* name, int price) {
    for(int i=0; i<state->potion_count; i++) {
        if(strcmp(state->potions[i], name) == 0) {
            free(state->potions[i]);
            /* Shift remaining potions left */
            for(int j=i; j<state->potion_count-1; j++) {
                state->potions[j] = state->potions[j+1];
            }
            state->potion_count--;
            state->total_gold += price;
            printf("Potion sold: %s\n", name);
            return;
        }
    }
    printf("Potion not found: %s\n", name);
}

/* ============================================================
 * EXHAUST SYSTEM
 * ============================================================ */

/*
 * handle_mark_exhaust - Process "Ironclad marks card <Name> as exhaust".
 *
 * Adds the card name to the global exhaust list. If the card is already
 * in the list, prints "Card already exhausts". Otherwise, marks all existing
 * DeckEntry objects with that name as exhausting, and adds to the global list.
 *
 * The exhaust tag affects:
 *   1. Deck display: exhausting cards show a '*' suffix (e.g., "Strike*")
 *   2. Combat: exhausting effective cards lose one copy after a victory
 *
 * Output: "Card marked as exhaust: <Name>" or "Card already exhausts: <Name>".
 */
void handle_mark_exhaust(WorldState* state, const char* name) {
    /* Check for duplicate */
    for(int i=0; i<state->global_exhausts_count; i++) {
        if(strcmp(state->global_exhausts[i], name) == 0) {
            printf("Card already exhausts: %s\n", name);
            return;
        }
    }

    /* Grow exhaust list if needed */
    if(state->global_exhausts_count >= state->global_exhausts_capacity) {
        state->global_exhausts_capacity = state->global_exhausts_capacity == 0 ? 8 : state->global_exhausts_capacity * 2;
        void* t = realloc(state->global_exhausts, state->global_exhausts_capacity * sizeof(char*));
        if(!t) { fprintf(stderr, "FATAL: Out of memory\n"); exit(1); }
        state->global_exhausts = t;
    }

    state->global_exhausts[state->global_exhausts_count++] = strdup(name);

    /* Apply the exhaust tag to all existing deck entries with this name */
    for(int i=0; i<state->deck_count; i++) {
        if(strcmp(state->deck[i].name, name) == 0) {
            state->deck[i].exhausts = true;
        }
    }

    printf("Card marked as exhaust: %s\n", name);
}

/*
 * handle_gain_max_hp - Process "Ironclad gains <N> max hp".
 * Increases the HP ceiling. Current HP is NOT affected.
 * Output: "Max health increased to <new_max>"
 */
void handle_gain_max_hp(WorldState* state, int amount) {
    state->max_hp += amount;
    printf("Max health increased to %d\n", state->max_hp);
}

/* ============================================================
 * QUERY HANDLERS (Read-Only)
 * ============================================================ */

/* "Total gold ?" → Print current gold total */
void handle_gold_query(WorldState* state) { printf("%d\n", state->total_gold); }

/* "Floor ?" → Print current floor number */
void handle_floor_query(WorldState* state) { printf("%d\n", state->floor); }

/* "Where ?" → Print current room type name (or "NONE" if no room entered) */
void handle_where_query(WorldState* state) {
    const char* room_names[] = {"NONE", "Monster", "Elite", "Rest", "Shop", "Treasure", "Event", "Boss"};
    printf("%s\n", room_names[state->current_room]);
}

/*
 * handle_total_card_query - Process "Total card <Name> ?".
 * Prints the total number of copies (base + upgraded) of the specified card.
 * Prints 0 if the card is not in the deck.
 */
void handle_total_card_query(WorldState* state, const char* name) {
    for(int i=0; i<state->deck_count; i++) {
        if(strcmp(state->deck[i].name, name) == 0) {
            printf("%d\n", state->deck[i].base_count + state->deck[i].upgraded_count);
            return;
        }
    }
    printf("0\n");
}

/*
 * handle_total_upgraded_card_query - Process "Total upgraded card <Name> ?".
 * Prints the number of upgraded copies of the specified card.
 * Prints 0 if the card is not in the deck or has no upgraded copies.
 */
void handle_total_upgraded_card_query(WorldState* state, const char* name) {
    for(int i=0; i<state->deck_count; i++) {
        if(strcmp(state->deck[i].name, name) == 0) {
            printf("%d\n", state->deck[i].upgraded_count);
            return;
        }
    }
    printf("0\n");
}

/* ---- Deck Query Helpers ---- */

/* Temporary struct for sorting cards alphabetically */
typedef struct {
    char* name;
    int base_count;
    int up_count;
    bool exhausts;
} TmpDeckEntry;

/* Comparator for qsort: alphabetical order by card name (C strcmp) */
static int compare_deck(const void* a, const void* b) {
    const TmpDeckEntry* d1 = (const TmpDeckEntry*)a;
    const TmpDeckEntry* d2 = (const TmpDeckEntry*)b;
    return strcmp(d1->name, d2->name);
}

/*
 * handle_deck_query - Process "Deck ?".
 *
 * Lists all cards in the deck, sorted alphabetically by name. For each card:
 *   - Base copies shown as: "<count> <Name>[*]"
 *   - Upgraded copies shown as: "<count> <Name>+[*]"
 *   - '*' suffix indicates the card has the exhaust tag
 *
 * Cards with 0 total copies (base + upgraded) are excluded.
 * Multiple entries for the same card are shown separately (base then upgraded).
 * Entries are comma-separated on a single line.
 * Prints "None" if the deck is empty.
 */
void handle_deck_query(WorldState* state) {
    /* Count cards that have at least one copy */
    int total_cards = 0;
    for(int i=0; i<state->deck_count; i++) {
        if(state->deck[i].base_count > 0 || state->deck[i].upgraded_count > 0) total_cards++;
    }

    if(total_cards == 0) {
        printf("None\n");
        return;
    }
    
    /* Create temporary array for sorting (we don't want to mutate deck order) */
    TmpDeckEntry* tmp = malloc(total_cards * sizeof(TmpDeckEntry));
    if(!tmp) { fprintf(stderr, "FATAL: Out of memory\n"); exit(1); }

    int idx = 0;
    for(int i=0; i<state->deck_count; i++) {
        if(state->deck[i].base_count > 0 || state->deck[i].upgraded_count > 0) {
            tmp[idx].name = state->deck[i].name;
            tmp[idx].base_count = state->deck[i].base_count;
            tmp[idx].up_count = state->deck[i].upgraded_count;
            tmp[idx].exhausts = state->deck[i].exhausts;
            idx++;
        }
    }

    /* Sort alphabetically by card name */
    qsort(tmp, total_cards, sizeof(TmpDeckEntry), compare_deck);
    
    /* Print each card group: base copies first, then upgraded copies */
    bool first = true;
    for(int i=0; i<total_cards; i++) {
        if(tmp[i].base_count > 0) {
            if(!first) printf(", ");
            printf("%d %s%s", tmp[i].base_count, tmp[i].name, tmp[i].exhausts ? "*" : "");
            first = false;
        }
        if(tmp[i].up_count > 0) {
            if(!first) printf(", ");
            printf("%d %s+%s", tmp[i].up_count, tmp[i].name, tmp[i].exhausts ? "*" : "");
            first = false;
        }
    }
    printf("\n");
    free(tmp);
}

/* Comparator for qsort: alphabetical order for string pointers */
static int compare_strings(const void* a, const void* b) {
    const char* s1 = *(const char**)a;
    const char* s2 = *(const char**)b;
    return strcmp(s1, s2);
}

/*
 * handle_relics_query - Process "Relics ?".
 * Lists all relics alphabetically, comma-separated. Prints "None" if empty.
 */
void handle_relics_query(WorldState* state) {
    if(state->relic_count == 0) {
        printf("None\n");
        return;
    }

    /* Create a sorted copy of the relics array (don't mutate original order) */
    char** tmp = malloc(state->relic_count * sizeof(char*));
    if(!tmp) { fprintf(stderr, "FATAL: Out of memory\n"); exit(1); }
    for(int i=0; i<state->relic_count; i++) tmp[i] = state->relics[i];
    qsort(tmp, state->relic_count, sizeof(char*), compare_strings);

    for(int i=0; i<state->relic_count; i++) {
        if(i > 0) printf(", ");
        printf("%s", tmp[i]);
    }
    printf("\n");
    free(tmp);
}

/* ---- Potion Query Helpers ---- */

/* Temporary struct for grouping and sorting potions by name */
typedef struct {
    char* name;
    int count;
} TmpPotion;

/* Comparator for qsort: alphabetical order for potion groups */
static int compare_potions(const void* a, const void* b) {
    const TmpPotion* p1 = (const TmpPotion*)a;
    const TmpPotion* p2 = (const TmpPotion*)b;
    return strcmp(p1->name, p2->name);
}

/*
 * handle_potions_query - Process "Potions ?".
 *
 * Groups potions by name, counts duplicates, sorts alphabetically.
 * Output format: "<count> <Name>, <count> <Name>, ..."
 * Prints "None" if belt is empty.
 */
void handle_potions_query(WorldState* state) {
    if(state->potion_count == 0) {
        printf("None\n");
        return;
    }

    /* Group potions by name and count duplicates */
    TmpPotion tmp[POTION_BELT_SIZE];
    int count = 0;
    for(int i=0; i<state->potion_count; i++) {
        bool found = false;
        for(int j=0; j<count; j++) {
            if(strcmp(tmp[j].name, state->potions[i]) == 0) {
                tmp[j].count++;
                found = true; break;
            }
        }
        if(!found) {
            tmp[count].name = state->potions[i];
            tmp[count].count = 1;
            count++;
        }
    }

    /* Sort groups alphabetically */
    qsort(tmp, count, sizeof(TmpPotion), compare_potions);

    for(int i=0; i<count; i++) {
        if(i > 0) printf(", ");
        printf("%d %s", tmp[i].count, tmp[i].name);
    }
    printf("\n");
}

/*
 * handle_effective_query - Process "What is effective against <Enemy> ?".
 *
 * Lists all items recorded as effective against the specified enemy,
 * sorted alphabetically by their formatted string ("card X", "potion Y", "relic Z").
 * Format: "type name, type name, ..."
 * Prints "No codex data for <Enemy>" if the enemy is unknown.
 *
 * Note: Sorting is done on the formatted strings, so "card X" comes before
 * "potion Y" which comes before "relic Z" in most cases (alphabetical by type prefix).
 */
void handle_effective_query(WorldState* state, const char* enemy) {
    CodexEntry* entry = NULL;
    for(int i=0; i<state->codex_count; i++) {
        if(strcmp(state->codex[i].name, enemy) == 0) {
            entry = &state->codex[i];
            break;
        }
    }

    if(!entry) {
        printf("No codex data for %s\n", enemy);
        return;
    }
    
    /* Build formatted strings: "card X", "relic Y", "potion Z" */
    char** formatted = malloc(entry->effect_count * sizeof(char*));
    if(!formatted) { fprintf(stderr, "FATAL: Out of memory\n"); exit(1); }
    for(int i=0; i<entry->effect_count; i++) {
        char buf[2048];
        if(entry->effects[i].type == TYPE_CARD) snprintf(buf, sizeof(buf), "card %s", entry->effects[i].name);
        else if(entry->effects[i].type == TYPE_RELIC) snprintf(buf, sizeof(buf), "relic %s", entry->effects[i].name);
        else snprintf(buf, sizeof(buf), "potion %s", entry->effects[i].name);
        formatted[i] = strdup(buf);
    }

    /* Sort formatted strings alphabetically */
    qsort(formatted, entry->effect_count, sizeof(char*), compare_strings);

    for(int i=0; i<entry->effect_count; i++) {
        if(i > 0) printf(", ");
        printf("%s", formatted[i]);
        free(formatted[i]);
    }
    free(formatted);
    printf("\n");
}

/*
 * handle_defeated_query - Process "Defeated <Enemy> ?".
 * Prints the number of times the enemy has been defeated.
 * Prints 0 if the enemy is unknown.
 */
void handle_defeated_query(WorldState* state, const char* enemy) {
    for(int i=0; i<state->codex_count; i++) {
        if(strcmp(state->codex[i].name, enemy) == 0) {
            printf("%d\n", state->codex[i].defeated_count);
            return;
        }
    }
    printf("0\n");
}

/*
 * handle_health_query - Process "Health ?".
 * Prints current and max HP in the format "current/max".
 */
void handle_health_query(WorldState* state) {
    printf("%d/%d\n", state->hp, state->max_hp);
}

/*
 * handle_deck_size_query - Process "Deck size ?".
 * Prints the total number of cards (base + upgraded across all card names).
 */
void handle_deck_size_query(WorldState* state) {
    int total = 0;
    for(int i=0; i<state->deck_count; i++) {
        total += state->deck[i].base_count + state->deck[i].upgraded_count;
    }
    printf("%d\n", total);
}

/*
 * handle_exhausts_query - Process "Exhausts ?".
 * Lists all card names that have been marked as exhaust, sorted alphabetically.
 * Prints "None" if no cards are tagged.
 */
void handle_exhausts_query(WorldState* state) {
    if(state->global_exhausts_count == 0) {
        printf("None\n");
        return;
    }

    /* Create sorted copy of exhaust list */
    char** tmp = malloc(state->global_exhausts_count * sizeof(char*));
    if(!tmp) { fprintf(stderr, "FATAL: Out of memory\n"); exit(1); }
    for(int i=0; i<state->global_exhausts_count; i++) tmp[i] = state->global_exhausts[i];
    qsort(tmp, state->global_exhausts_count, sizeof(char*), compare_strings);

    for(int i=0; i<state->global_exhausts_count; i++) {
        if(i > 0) printf(", ");
        printf("%s", tmp[i]);
    }
    printf("\n");
    free(tmp);
}
