#ifndef STATE_H
#define STATE_H

#include "spireslayer.h"

/*
 * state.h - Interface for all Spireslayer game state operations.
 *
 * This module implements the semantic layer of the interpreter. Each function
 * corresponds to a specific grammar rule and performs the required state
 * mutation plus output generation. The parser calls these functions after
 * successfully matching a grammar pattern.
 *
 * All functions print their output directly to stdout. Memory management
 * (dynamic array growth, OOM protection) is handled internally.
 *
 * Functions are grouped into two categories:
 *   1. State Mutations - Commands that modify the game state
 *   2. Read-only Queries - Commands that only read and display state
 */

/* ============================================================
 * STATE MUTATIONS
 * These functions modify the WorldState and print a response.
 * ============================================================ */

/* "Ironclad gains <amount> gold" → Adds gold, prints "Gold obtained" */
void handle_gain_gold(WorldState* state, int amount);

/* "Ironclad gains card <name>" → Adds a base copy, prints "Card added: <name>" */
void handle_gain_card(WorldState* state, const char* name);

/* "Ironclad gains relic <name>" → Adds relic (if unique), prints result */
void handle_gain_relic(WorldState* state, const char* name);

/* "Ironclad gains potion <name>" → Adds to belt (if <3), prints result */
void handle_gain_potion(WorldState* state, const char* name);

/* "Ironclad buys card <name> for <price> gold" → Checks gold, adds card */
void handle_buy_card(WorldState* state, const char* name, int price);

/* "Ironclad buys relic <name> for <price> gold" → Checks gold+uniqueness, adds relic */
void handle_buy_relic(WorldState* state, const char* name, int price);

/* "Ironclad buys potion <name> for <price> gold" → Checks gold+belt capacity, adds */
void handle_buy_potion(WorldState* state, const char* name, int price);

/* "Ironclad removes card <name>" → Removes one base copy, prints result */
void handle_remove_card(WorldState* state, const char* name);

/* "Ironclad removes upgraded card <name>" → Removes one upgraded copy, prints result */
void handle_remove_upgraded_card(WorldState* state, const char* name);

/* "Ironclad upgrades card <name>" → Converts one base copy to upgraded, prints result */
void handle_upgrade_card(WorldState* state, const char* name);

/* "Ironclad enters <RoomType> room" → Increments floor, sets room, prints "Entered" */
void handle_enter_room(WorldState* state, RoomType room);

/* "Ironclad learns card/relic/potion <name> is effective against <enemy>" → Codex update */
void handle_learn_card_effective(WorldState* state, const char* name, const char* enemy);
void handle_learn_relic_effective(WorldState* state, const char* name, const char* enemy);
void handle_learn_potion_effective(WorldState* state, const char* name, const char* enemy);

/* "Ironclad fights <enemy>" → Checks codex for counters, resolves combat */
void handle_fight_enemy(WorldState* state, const char* enemy);

/* "Ironclad fights <enemy> for <bounty> gold" → Same as above, with gold reward on win */
void handle_fight_enemy_bounty(WorldState* state, const char* enemy, int bounty);

/* "Ironclad heals <amount> hp" → Increases HP (capped at max_hp), prints result */
void handle_heal(WorldState* state, int amount);

/* "Ironclad takes <amount> damage" → Decreases HP (clamped at 0), prints result */
void handle_take_damage(WorldState* state, int amount);

/* "Ironclad discards potion <name>" → Removes from belt, prints result */
void handle_discard_potion(WorldState* state, const char* name);

/* "Ironclad sells card <name> for <price> gold" → Removes base copy, adds gold */
void handle_sell_card(WorldState* state, const char* name, int price);

/* "Ironclad sells upgraded card <name> for <price> gold" → Removes upgraded copy, adds gold */
void handle_sell_upgraded_card(WorldState* state, const char* name, int price);

/* "Ironclad sells potion <name> for <price> gold" → Removes from belt, adds gold */
void handle_sell_potion(WorldState* state, const char* name, int price);

/* "Ironclad marks card <name> as exhaust" → Tags card for combat consumption */
void handle_mark_exhaust(WorldState* state, const char* name);

/* "Ironclad gains <amount> max hp" → Increases max HP ceiling, prints result */
void handle_gain_max_hp(WorldState* state, int amount);

/* ============================================================
 * READ-ONLY QUERIES
 * These functions only read state and print a response.
 * ============================================================ */

/* "Total gold ?" → Prints current gold total */
void handle_gold_query(WorldState* state);

/* "Floor ?" → Prints current floor number */
void handle_floor_query(WorldState* state);

/* "Where ?" → Prints current room name (or "NONE") */
void handle_where_query(WorldState* state);

/* "Total card <name> ?" → Prints total copies (base + upgraded) */
void handle_total_card_query(WorldState* state, const char* name);

/* "Total upgraded card <name> ?" → Prints upgraded copy count */
void handle_total_upgraded_card_query(WorldState* state, const char* name);

/* "Deck ?" → Prints alphabetically sorted deck listing */
void handle_deck_query(WorldState* state);

/* "Relics ?" → Prints alphabetically sorted relic list */
void handle_relics_query(WorldState* state);

/* "Potions ?" → Prints alphabetically sorted potion list with counts */
void handle_potions_query(WorldState* state);

/* "What is effective against <enemy> ?" → Prints codex effectiveness entries */
void handle_effective_query(WorldState* state, const char* enemy);

/* "Defeated <enemy> ?" → Prints defeat count for an enemy */
void handle_defeated_query(WorldState* state, const char* enemy);

/* "Health ?" → Prints current/max HP in format "hp/max_hp" */
void handle_health_query(WorldState* state);

/* "Deck size ?" → Prints total number of cards (base + upgraded across all entries) */
void handle_deck_size_query(WorldState* state);

/* "Exhausts ?" → Prints alphabetically sorted list of exhaust-tagged card names */
void handle_exhausts_query(WorldState* state);

#endif
