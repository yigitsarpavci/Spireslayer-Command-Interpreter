#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <errno.h>
#include "spireslayer.h"
#include "state.h"
#include "parser.h"

/*
 * CMPE230 - Assignment 1: Spireslayer Command Parser
 * Authors: 2023400048, 2023400210
 *
 * This file implements the lexical analysis and grammar matching engine.
 * 
 * Architecture:
 *   1. TOKENIZATION: Input is split into tokens by ASCII space (0x20) characters.
 *      Multiple consecutive spaces produce no empty tokens; they are simply skipped.
 *      Each token stores both its string value and its original position/length
 *      in the input line (needed for the "exactly one space" name validation).
 *
 *   2. GRAMMAR MATCHING: A backtracking pattern matcher attempts to match the
 *      token sequence against every valid grammar rule. Each rule tries to consume
 *      tokens from the current position. If the match fails partway, the position
 *      is reset (backtracked) and the next rule is tried.
 *
 *   3. SEMANTIC DISPATCH: When a complete match is found (all tokens consumed),
 *      the corresponding state.c handler is called. The handler performs the
 *      game state mutation and prints the response.
 *
 *   4. MEMORY SAFETY: All dynamically allocated strings (from extract_name) are
 *      freed on both success and failure paths to prevent memory leaks.
 */

/* ============================================================
 * TOKEN STRUCTURE
 * ============================================================ */

/*
 * Token - A single space-delimited word from the input line.
 *
 * Fields:
 *   str        - Null-terminated copy of the token string
 *   orig_start - Pointer to the token's start in the original input line
 *   orig_len   - Length of the token in the original input
 *
 * orig_start and orig_len are used by match_name() to verify that
 * consecutive name words are separated by exactly one space in the
 * original input (not just after tokenization).
 */
typedef struct {
    char str[MAX_LINE_LENGTH + 2]; /* Token string value (max 1024 chars per spec + null byte) */
    const char* orig_start;   /* Pointer to this token's position in the original line */
    int orig_len;             /* Length of this token in the original line */
} Token;

/* ============================================================
 * RESERVED KEYWORD VALIDATION
 * ============================================================ */

/*
 * is_reserved - Check if a word is a reserved keyword in the grammar.
 *
 * Reserved words cannot appear as part of entity names (card/relic/potion/enemy).
 * For example, "Ironclad gains card gold" is INVALID because "gold" is reserved.
 *
 * The complete list of reserved words is derived from the BNF grammar in the
 * assignment specification. It includes all fixed tokens that appear in any
 * grammar rule.
 *
 * @param word  Null-terminated word to check.
 * @return      true if the word is reserved, false if it can be used in names.
 */
static bool is_reserved(const char* word) {
    const char* reserved[] = {
        "Ironclad", "gains", "gold", "max", "hp", "card", "relic", "potion", "buys", "for", "removes",
        "upgraded", "upgrades", "enters", "room", "learns", "is", "effective", "against", "fights",
        "heals", "takes", "damage", "discards", "sells", "marks", "as", "exhaust", "Total", "Floor",
        "Where", "Deck", "size", "Relics", "Potions", "What", "Defeated", "Health", "Exhausts", "Exit"
    };
    int n = sizeof(reserved) / sizeof(reserved[0]);
    for(int i=0; i<n; i++) {
        if(strcmp(word, reserved[i]) == 0) return true;
    }
    return false;
}

/*
 * is_valid_name_word - Validate a single word within an entity name.
 *
 * A valid name word must:
 *   1. Consist entirely of English alphabetic characters (A-Z, a-z)
 *   2. NOT be a reserved keyword
 *
 * This means names like "123Card", "fire_ball", or "gold" are all invalid.
 *
 * @param word  Null-terminated word to validate.
 * @return      true if valid for use in a name, false otherwise.
 */
static bool is_valid_name_word(const char* word) {
    for(int i=0; word[i]; i++) {
        if(!isalpha((unsigned char)word[i])) return false;
    }
    return !is_reserved(word);
}

/* ============================================================
 * TOKEN MATCHING PRIMITIVES
 * ============================================================ */

/*
 * match_integer - Match and consume a strictly positive integer token.
 *
 * Validation rules (from assignment PDF):
 *   - Must start with a digit 1-9 (no leading zeros, no sign prefix)
 *   - All characters must be digits
 *   - Value must be in range [1, 2147483647] (signed 32-bit max)
 *   - Parsed via strtoll to catch overflow on 32-bit systems
 *
 * Zero is explicitly rejected (the spec says strictly positive integers only).
 * Leading zeros like "01" or "007" are also rejected.
 *
 * @param tokens     Token array from the tokenizer.
 * @param num_tokens Total number of tokens.
 * @param pos        Current position in the token array (advanced on success).
 * @param out        Output: the parsed integer value.
 * @return           true if a valid integer was matched and consumed.
 */
static bool match_integer(Token* tokens, int num_tokens, int* pos, int* out) {
    if(*pos >= num_tokens) return false;
    const char* str = tokens[*pos].str;

    /* Empty token check */
    if(str[0] == '\0') return false;

    /* Must start with 1-9 (rejects: leading zero "07", negative "-5", plus "+3", zero "0") */
    if(str[0] < '1' || str[0] > '9') return false;

    /* All characters must be digits */
    for(int i=0; str[i]; i++) {
        if(!isdigit((unsigned char)str[i])) return false;
    }
    
    /* Parse as 64-bit to detect overflow, then clamp to 32-bit range.
     * Using strtoll instead of strtol for hardware-independent overflow detection.
     * errno is checked for ERANGE which catches astronomically large numbers. */
    errno = 0;
    long long val = strtoll(str, NULL, 10);
    if(errno == ERANGE) return false;
    
    /* Enforce 32-bit signed integer range as per spec constraint */
    if(val <= 0 || val > 2147483647LL) return false;

    *out = (int)val;
    (*pos)++;
    return true;
}

/*
 * match_keyword - Match and consume a specific keyword token.
 *
 * Performs exact string comparison (case-sensitive). On match, the
 * position pointer is advanced past the consumed token.
 *
 * @param tokens     Token array.
 * @param num_tokens Total token count.
 * @param pos        Current position (advanced on match).
 * @param kw         Expected keyword string to match.
 * @return           true if the current token exactly matches kw.
 */
static bool match_keyword(Token* tokens, int num_tokens, int* pos, const char* kw) {
    if(*pos >= num_tokens) return false;
    if(strcmp(tokens[*pos].str, kw) == 0) {
        (*pos)++;
        return true;
    }
    return false;
}

/*
 * match_question_mark - Match and consume a "?" token.
 *
 * Query commands require "?" as a separate, space-delimited token.
 * For example, "Total gold ?" is valid but "Total gold?" is not
 * (because "gold?" would be one token, not two).
 *
 * @param tokens     Token array.
 * @param num_tokens Total token count.
 * @param pos        Current position (advanced on match).
 * @return           true if the current token is exactly "?".
 */
static bool match_question_mark(Token* tokens, int num_tokens, int* pos) {
    return match_keyword(tokens, num_tokens, pos, "?");
}

/* ============================================================
 * NAME EXTRACTION AND VALIDATION
 * ============================================================ */

/*
 * extract_name - Reconstruct a multi-word name from a range of tokens.
 *
 * Concatenates tokens from index 'start' to 'end' (inclusive) with single
 * spaces between them. The caller is responsible for freeing the result.
 *
 * @param tokens  Token array.
 * @param start   First token index (inclusive).
 * @param end     Last token index (inclusive).
 * @return        Newly allocated string, or NULL if start > end.
 */
static char* extract_name(Token* tokens, int start, int end) {
    if(start > end) return NULL;

    /* Calculate total length needed: sum of token lengths + spaces + null terminator */
    int len = 0;
    for(int i=start; i<=end; i++) len += tokens[i].orig_len + 1;
    char* name = malloc(len + 1);
    
    /* Fatal exit on allocation failure to prevent silent data corruption */
    if(!name) { fprintf(stderr, "FATAL: Out of memory\n"); exit(1); }
    
    /* Build the name string with single-space separators */
    name[0] = '\0';
    for(int i=start; i<=end; i++) {
        strcat(name, tokens[i].str);
        if(i < end) strcat(name, " ");
    }
    return name;
}

/*
 * match_name - Match and consume a sequence of tokens forming an entity name.
 *
 * Names consist of one or more "valid name words" (alphabetic, non-reserved).
 * The matching continues until either:
 *   - A stop word is encountered (e.g., "for", "is", "?")
 *   - The end of the token array is reached
 *
 * After collecting candidate name tokens, two validations are performed:
 *   1. Each word must pass is_valid_name_word() (all letters, not reserved)
 *   2. In the ORIGINAL input, consecutive words must be separated by
 *      EXACTLY one space (not multiple spaces)
 *
 * The second check is critical: "Ironclad gains card Twin  Strike" (two spaces)
 * has "Twin" and "Strike" as separate tokens after tokenization, but the
 * original gap between them is 2 spaces, which violates the name format rule.
 *
 * @param tokens         Token array.
 * @param num_tokens     Total token count.
 * @param pos            Current position (advanced past the name on success).
 * @param out_name       Output: newly allocated name string (caller must free).
 * @param stop_words     Array of keyword strings that terminate the name.
 * @param num_stop_words Number of stop words.
 * @return               true if a valid non-empty name was matched.
 */
static bool match_name(Token* tokens, int num_tokens, int* pos, char** out_name, const char** stop_words, int num_stop_words) {
    int start = *pos;

    /* Consume tokens until a stop word or end of tokens */
    while(*pos < num_tokens) {
        bool is_stop = false;
        for(int i=0; i<num_stop_words; i++) {
            if(strcmp(tokens[*pos].str, stop_words[i]) == 0) { is_stop = true; break; }
        }
        if(is_stop) break;
        (*pos)++;
    }

    int end = *pos - 1;

    /* Name must contain at least one word */
    if(start > end) return false;

    /* Validate each word and check inter-word spacing */
    for(int i=start; i<=end; i++) {
        if(!is_valid_name_word(tokens[i].str)) return false;
        if(i > start) {
            /* Calculate the gap in the ORIGINAL input between adjacent name words.
             * This is: (start of current token) - (end of previous token).
             * The gap must be exactly 1 space character. */
            int gap = tokens[i].orig_start - (tokens[i-1].orig_start + tokens[i-1].orig_len);
            if(gap != 1) return false;
        }
    }

    *out_name = extract_name(tokens, start, end);
    return true;
}

/* ============================================================
 * MAIN PARSER / EXECUTOR
 * ============================================================ */

/*
 * execute_line - Tokenize and parse a single command line.
 *
 * This is the core of the interpreter. It:
 *   1. Tokenizes the input by ASCII spaces
 *   2. Tries each grammar rule in sequence via backtracking
 *   3. On a successful match (all tokens consumed), calls the handler
 *   4. Returns 1 on success, 0 if no rule matched
 *
 * Grammar rules are organized by their leading keyword(s):
 *   - "Ironclad gains ..."  (gold, max hp, card, relic, potion)
 *   - "Ironclad buys ..."   (card, relic, potion)
 *   - "Ironclad removes ..."(card, upgraded card)
 *   - "Ironclad upgrades ..."
 *   - "Ironclad enters ..."
 *   - "Ironclad learns ..."
 *   - "Ironclad fights ..."
 *   - "Ironclad heals ..."
 *   - "Ironclad takes ..."
 *   - "Ironclad discards ..."
 *   - "Ironclad sells ..."
 *   - "Ironclad marks ..."
 *   - "Total ..."           (gold, card, upgraded card)
 *   - "Floor ?"
 *   - "Where ?"
 *   - "Deck ?" / "Deck size ?"
 *   - "Relics ?"
 *   - "Potions ?"
 *   - "What is effective against ... ?"
 *   - "Defeated ... ?"
 *   - "Health ?"
 *   - "Exhausts ?"
 *
 * @param state  Pointer to the game state (may be mutated).
 * @param line   Null-terminated input string.
 * @return       1 if a valid command was matched and executed, 0 otherwise.
 */
int execute_line(WorldState* state, const char* line) {
    Token tokens[1024];
    int num_tokens = 0;
    int len = strlen(line);
    int i = 0;
    
    /* === TOKENIZATION PHASE ===
     * Split input by ASCII space (0x20) characters.
     * Multiple consecutive spaces are treated as a single delimiter.
     * Tabs and other whitespace are NOT treated as delimiters. */
    while(i < len) {
        if(line[i] == ' ') {
            i++;  /* Skip spaces between tokens */
        } else {
            int start = i;
            while(i < len && line[i] != ' ') i++;  /* Find token end */
            int tlen = i - start;
            if(tlen > 0) {
                /* Store token string and its original position for gap validation */
                strncpy(tokens[num_tokens].str, line + start, tlen);
                tokens[num_tokens].str[tlen] = '\0';
                tokens[num_tokens].orig_start = line + start;
                tokens[num_tokens].orig_len = tlen;
                num_tokens++;
            }
        }
    }
    
    /* Empty input (only spaces) produces no tokens - treat as invalid */
    if(num_tokens == 0) return 0;
    
    int pos = 0;
    
    /* === GRAMMAR MATCHING PHASE ===
     * Each block below attempts to match one grammar rule.
     * On partial failure, the position is reset (backtracked) and the next rule is tried.
     * On complete success (pos == num_tokens), the handler is called and we return 1.
     * Dynamically allocated names are always freed on failure to prevent leaks. */

    /* ---- "Ironclad gains ..." rules ---- */
    pos = 0;
    if(match_keyword(tokens, num_tokens, &pos, "Ironclad") && match_keyword(tokens, num_tokens, &pos, "gains")) {
        int amount;
        if(match_integer(tokens, num_tokens, &pos, &amount)) {
            /* "Ironclad gains <N> gold" */
            if(match_keyword(tokens, num_tokens, &pos, "gold") && pos == num_tokens) {
                handle_gain_gold(state, amount); return 1;
            /* "Ironclad gains <N> max hp" */
            } else if(match_keyword(tokens, num_tokens, &pos, "max") && match_keyword(tokens, num_tokens, &pos, "hp") && pos == num_tokens) {
                handle_gain_max_hp(state, amount); return 1;
            }
        }
        
        /* Backtrack to just after "gains" for item-type rules */
        pos = 2;
        if(match_keyword(tokens, num_tokens, &pos, "card")) {
            /* "Ironclad gains card <Name>" */
            char* name = NULL;
            const char* stops[] = {"dummy"};
            if(match_name(tokens, num_tokens, &pos, &name, stops, 0)) {
                if(pos == num_tokens) { handle_gain_card(state, name); free(name); return 1; }
            }
            if(name) free(name);
        } else if(match_keyword(tokens, num_tokens, &pos, "relic")) {
            /* "Ironclad gains relic <Name>" */
            char* name = NULL;
            const char* stops[] = {"dummy"};
            if(match_name(tokens, num_tokens, &pos, &name, stops, 0)) {
                if(pos == num_tokens) { handle_gain_relic(state, name); free(name); return 1; }
            }
            if(name) free(name);
        } else if(match_keyword(tokens, num_tokens, &pos, "potion")) {
            /* "Ironclad gains potion <Name>" */
            char* name = NULL;
            const char* stops[] = {"dummy"};
            if(match_name(tokens, num_tokens, &pos, &name, stops, 0)) {
                if(pos == num_tokens) { handle_gain_potion(state, name); free(name); return 1; }
            }
            if(name) free(name);
        }
    }

    /* ---- "Ironclad buys ..." rules ---- */
    pos = 0;
    if(match_keyword(tokens, num_tokens, &pos, "Ironclad") && match_keyword(tokens, num_tokens, &pos, "buys")) {
        pos = 2;
        if(match_keyword(tokens, num_tokens, &pos, "card")) {
            /* "Ironclad buys card <Name> for <Price> gold" */
            char* name = NULL;
            const char* stops[] = {"for"};
            if(match_name(tokens, num_tokens, &pos, &name, stops, 1) && match_keyword(tokens, num_tokens, &pos, "for")) {
                int price;
                if(match_integer(tokens, num_tokens, &pos, &price) && match_keyword(tokens, num_tokens, &pos, "gold") && pos == num_tokens) {
                    handle_buy_card(state, name, price); free(name); return 1;
                }
            }
            if(name) free(name);
        } else if(match_keyword(tokens, num_tokens, &pos, "relic")) {
            /* "Ironclad buys relic <Name> for <Price> gold" */
            char* name = NULL;
            const char* stops[] = {"for"};
            if(match_name(tokens, num_tokens, &pos, &name, stops, 1) && match_keyword(tokens, num_tokens, &pos, "for")) {
                int price;
                if(match_integer(tokens, num_tokens, &pos, &price) && match_keyword(tokens, num_tokens, &pos, "gold") && pos == num_tokens) {
                    handle_buy_relic(state, name, price); free(name); return 1;
                }
            }
            if(name) free(name);
        } else if(match_keyword(tokens, num_tokens, &pos, "potion")) {
            /* "Ironclad buys potion <Name> for <Price> gold" */
            char* name = NULL;
            const char* stops[] = {"for"};
            if(match_name(tokens, num_tokens, &pos, &name, stops, 1) && match_keyword(tokens, num_tokens, &pos, "for")) {
                int price;
                if(match_integer(tokens, num_tokens, &pos, &price) && match_keyword(tokens, num_tokens, &pos, "gold") && pos == num_tokens) {
                    handle_buy_potion(state, name, price); free(name); return 1;
                }
            }
            if(name) free(name);
        }
    }

    /* ---- "Ironclad removes ..." rules ---- */
    pos = 0;
    if(match_keyword(tokens, num_tokens, &pos, "Ironclad") && match_keyword(tokens, num_tokens, &pos, "removes")) {
        if(match_keyword(tokens, num_tokens, &pos, "card")) {
            /* "Ironclad removes card <Name>" */
            char* name = NULL;
            const char* stops[] = {"dummy"};
            if(match_name(tokens, num_tokens, &pos, &name, stops, 0)) {
                if(pos == num_tokens) { handle_remove_card(state, name); free(name); return 1; }
            }
            if(name) free(name);
        } else if(match_keyword(tokens, num_tokens, &pos, "upgraded") && match_keyword(tokens, num_tokens, &pos, "card")) {
            /* "Ironclad removes upgraded card <Name>" */
            char* name = NULL;
            const char* stops[] = {"dummy"};
            if(match_name(tokens, num_tokens, &pos, &name, stops, 0)) {
                if(pos == num_tokens) { handle_remove_upgraded_card(state, name); free(name); return 1; }
            }
            if(name) free(name);
        }
    }

    /* ---- "Ironclad upgrades card <Name>" ---- */
    pos = 0;
    if(match_keyword(tokens, num_tokens, &pos, "Ironclad") && match_keyword(tokens, num_tokens, &pos, "upgrades") && match_keyword(tokens, num_tokens, &pos, "card")) {
        char* name = NULL;
        const char* stops[] = {"dummy"};
        if(match_name(tokens, num_tokens, &pos, &name, stops, 0)) {
            if(pos == num_tokens) { handle_upgrade_card(state, name); free(name); return 1; }
        }
        if(name) free(name);
    }

    /* ---- "Ironclad enters <RoomType> room" ---- */
    pos = 0;
    if(match_keyword(tokens, num_tokens, &pos, "Ironclad") && match_keyword(tokens, num_tokens, &pos, "enters")) {
        /* Room type must be one of exactly 7 valid types (case-sensitive) */
        char* room_types[] = {"Monster", "Elite", "Rest", "Shop", "Treasure", "Event", "Boss"};
        RoomType room_enums[] = {ROOM_MONSTER, ROOM_ELITE, ROOM_REST, ROOM_SHOP, ROOM_TREASURE, ROOM_EVENT, ROOM_BOSS};
        if(pos < num_tokens) {
            for(int j=0; j<7; j++) {
                if(strcmp(tokens[pos].str, room_types[j]) == 0) {
                    pos++;
                    if(match_keyword(tokens, num_tokens, &pos, "room") && pos == num_tokens) {
                        handle_enter_room(state, room_enums[j]); return 1;
                    }
                    break;
                }
            }
        }
    }

    /* ---- "Ironclad learns card/relic/potion <Name> is effective against <Enemy>" ---- */
    pos = 0;
    if(match_keyword(tokens, num_tokens, &pos, "Ironclad") && match_keyword(tokens, num_tokens, &pos, "learns")) {
        if(match_keyword(tokens, num_tokens, &pos, "card")) {
            /* "Ironclad learns card <Item> is effective against <Enemy>" */
            char* item = NULL; char* enemy = NULL;
            const char* stops1[] = {"is"}; const char* stops2[] = {"dummy"};
            if(match_name(tokens, num_tokens, &pos, &item, stops1, 1) && 
               match_keyword(tokens, num_tokens, &pos, "is") && 
               match_keyword(tokens, num_tokens, &pos, "effective") && 
               match_keyword(tokens, num_tokens, &pos, "against") && 
               match_name(tokens, num_tokens, &pos, &enemy, stops2, 0)) {
                if(pos == num_tokens) {
                    handle_learn_card_effective(state, item, enemy); 
                    free(item); free(enemy); return 1;
                }
            }
            if(item) free(item); if(enemy) free(enemy);
        } else if(match_keyword(tokens, num_tokens, &pos, "relic")) {
            /* "Ironclad learns relic <Item> is effective against <Enemy>" */
            char* item = NULL; char* enemy = NULL;
            const char* stops1[] = {"is"}; const char* stops2[] = {"dummy"};
            if(match_name(tokens, num_tokens, &pos, &item, stops1, 1) && 
               match_keyword(tokens, num_tokens, &pos, "is") && 
               match_keyword(tokens, num_tokens, &pos, "effective") && 
               match_keyword(tokens, num_tokens, &pos, "against") && 
               match_name(tokens, num_tokens, &pos, &enemy, stops2, 0)) {
                if(pos == num_tokens) {
                    handle_learn_relic_effective(state, item, enemy); 
                    free(item); free(enemy); return 1;
                }
            }
            if(item) free(item); if(enemy) free(enemy);
        } else if(match_keyword(tokens, num_tokens, &pos, "potion")) {
            /* "Ironclad learns potion <Item> is effective against <Enemy>" */
            char* item = NULL; char* enemy = NULL;
            const char* stops1[] = {"is"}; const char* stops2[] = {"dummy"};
            if(match_name(tokens, num_tokens, &pos, &item, stops1, 1) && 
               match_keyword(tokens, num_tokens, &pos, "is") && 
               match_keyword(tokens, num_tokens, &pos, "effective") && 
               match_keyword(tokens, num_tokens, &pos, "against") && 
               match_name(tokens, num_tokens, &pos, &enemy, stops2, 0)) {
                if(pos == num_tokens) {
                    handle_learn_potion_effective(state, item, enemy); 
                    free(item); free(enemy); return 1;
                }
            }
            if(item) free(item); if(enemy) free(enemy);
        }
    }

    /* ---- "Ironclad fights <Enemy> [for <Bounty> gold]" ---- 
     * Two forms: with or without bounty gold.
     * The "for" variant is tried first because the enemy name uses "for" as a stop word.
     * If the "for" form doesn't fully match, we backtrack and try the plain form. */
    pos = 0;
    if(match_keyword(tokens, num_tokens, &pos, "Ironclad") && match_keyword(tokens, num_tokens, &pos, "fights")) {
        char* enemy = NULL;
        const char* stops[] = {"for"};
        int start = pos;

        /* Try: "Ironclad fights <Enemy> for <Bounty> gold" */
        if(match_name(tokens, num_tokens, &pos, &enemy, stops, 1) && match_keyword(tokens, num_tokens, &pos, "for")) {
            int bounty;
            if(match_integer(tokens, num_tokens, &pos, &bounty) && match_keyword(tokens, num_tokens, &pos, "gold") && pos == num_tokens) {
                handle_fight_enemy_bounty(state, enemy, bounty); free(enemy); return 1;
            }
        }
        if(enemy) free(enemy);

        /* Backtrack and try: "Ironclad fights <Enemy>" (no bounty) */
        pos = start;
        const char* stops2[] = {"dummy"};
        if(match_name(tokens, num_tokens, &pos, &enemy, stops2, 0)) {
            if(pos == num_tokens) { handle_fight_enemy(state, enemy); free(enemy); return 1; }
        }
        if(enemy) free(enemy);
    }

    /* ---- "Ironclad heals <N> hp" ---- */
    pos = 0;
    if(match_keyword(tokens, num_tokens, &pos, "Ironclad") && match_keyword(tokens, num_tokens, &pos, "heals")) {
        int amount;
        if(match_integer(tokens, num_tokens, &pos, &amount) && match_keyword(tokens, num_tokens, &pos, "hp") && pos == num_tokens) {
            handle_heal(state, amount); return 1;
        }
    }

    /* ---- "Ironclad takes <N> damage" ---- */
    pos = 0;
    if(match_keyword(tokens, num_tokens, &pos, "Ironclad") && match_keyword(tokens, num_tokens, &pos, "takes")) {
        int amount;
        if(match_integer(tokens, num_tokens, &pos, &amount) && match_keyword(tokens, num_tokens, &pos, "damage") && pos == num_tokens) {
            handle_take_damage(state, amount); return 1;
        }
    }

    /* ---- "Ironclad discards potion <Name>" ---- */
    pos = 0;
    if(match_keyword(tokens, num_tokens, &pos, "Ironclad") && match_keyword(tokens, num_tokens, &pos, "discards") && match_keyword(tokens, num_tokens, &pos, "potion")) {
        char* name = NULL;
        const char* stops[] = {"dummy"};
        if(match_name(tokens, num_tokens, &pos, &name, stops, 0)) {
            if(pos == num_tokens) { handle_discard_potion(state, name); free(name); return 1; }
        }
        if(name) free(name);
    }

    /* ---- "Ironclad sells ..." rules ---- */
    pos = 0;
    if(match_keyword(tokens, num_tokens, &pos, "Ironclad") && match_keyword(tokens, num_tokens, &pos, "sells")) {
        if(match_keyword(tokens, num_tokens, &pos, "card")) {
            /* "Ironclad sells card <Name> for <Price> gold" */
            char* name = NULL;
            const char* stops[] = {"for"};
            if(match_name(tokens, num_tokens, &pos, &name, stops, 1) && match_keyword(tokens, num_tokens, &pos, "for")) {
                int price;
                if(match_integer(tokens, num_tokens, &pos, &price) && match_keyword(tokens, num_tokens, &pos, "gold") && pos == num_tokens) {
                    handle_sell_card(state, name, price); free(name); return 1;
                }
            }
            if(name) free(name);
        } else if(match_keyword(tokens, num_tokens, &pos, "upgraded") && match_keyword(tokens, num_tokens, &pos, "card")) {
            /* "Ironclad sells upgraded card <Name> for <Price> gold" */
            char* name = NULL;
            const char* stops[] = {"for"};
            if(match_name(tokens, num_tokens, &pos, &name, stops, 1) && match_keyword(tokens, num_tokens, &pos, "for")) {
                int price;
                if(match_integer(tokens, num_tokens, &pos, &price) && match_keyword(tokens, num_tokens, &pos, "gold") && pos == num_tokens) {
                    handle_sell_upgraded_card(state, name, price); free(name); return 1;
                }
            }
            if(name) free(name);
        } else if(match_keyword(tokens, num_tokens, &pos, "potion")) {
            /* "Ironclad sells potion <Name> for <Price> gold" */
            char* name = NULL;
            const char* stops[] = {"for"};
            if(match_name(tokens, num_tokens, &pos, &name, stops, 1) && match_keyword(tokens, num_tokens, &pos, "for")) {
                int price;
                if(match_integer(tokens, num_tokens, &pos, &price) && match_keyword(tokens, num_tokens, &pos, "gold") && pos == num_tokens) {
                    handle_sell_potion(state, name, price); free(name); return 1;
                }
            }
            if(name) free(name);
        }
    }

    /* ---- "Ironclad marks card <Name> as exhaust" ---- */
    pos = 0;
    if(match_keyword(tokens, num_tokens, &pos, "Ironclad") && match_keyword(tokens, num_tokens, &pos, "marks") && match_keyword(tokens, num_tokens, &pos, "card")) {
        char* name = NULL;
        const char* stops[] = {"as"};
        if(match_name(tokens, num_tokens, &pos, &name, stops, 1) && match_keyword(tokens, num_tokens, &pos, "as") && match_keyword(tokens, num_tokens, &pos, "exhaust")) {
            if(pos == num_tokens) { handle_mark_exhaust(state, name); free(name); return 1; }
        }
        if(name) free(name);
    }

    /* ==== QUERY COMMANDS ==== */

    /* ---- "Total gold ?", "Total card <Name> ?", "Total upgraded card <Name> ?" ---- */
    pos = 0;
    if(match_keyword(tokens, num_tokens, &pos, "Total")) {
        if(match_keyword(tokens, num_tokens, &pos, "gold")) {
            if(match_question_mark(tokens, num_tokens, &pos) && pos == num_tokens) {
                handle_gold_query(state); return 1;
            }
        } else if(match_keyword(tokens, num_tokens, &pos, "card")) {
            char* name = NULL;
            const char* stops[] = {"?"};
            if(match_name(tokens, num_tokens, &pos, &name, stops, 1) && match_question_mark(tokens, num_tokens, &pos)) {
                if(pos == num_tokens) { handle_total_card_query(state, name); free(name); return 1; }
            }
            if(name) free(name);
        } else if(match_keyword(tokens, num_tokens, &pos, "upgraded") && match_keyword(tokens, num_tokens, &pos, "card")) {
            char* name = NULL;
            const char* stops[] = {"?"};
            if(match_name(tokens, num_tokens, &pos, &name, stops, 1) && match_question_mark(tokens, num_tokens, &pos)) {
                if(pos == num_tokens) { handle_total_upgraded_card_query(state, name); free(name); return 1; }
            }
            if(name) free(name);
        }
    }

    /* ---- "Floor ?" ---- */
    pos = 0;
    if(match_keyword(tokens, num_tokens, &pos, "Floor") && match_question_mark(tokens, num_tokens, &pos) && pos == num_tokens) {
        handle_floor_query(state); return 1;
    }

    /* ---- "Where ?" ---- */
    pos = 0;
    if(match_keyword(tokens, num_tokens, &pos, "Where") && match_question_mark(tokens, num_tokens, &pos) && pos == num_tokens) {
        handle_where_query(state); return 1;
    }

    /* ---- "Deck ?" and "Deck size ?" ---- */
    pos = 0;
    if(match_keyword(tokens, num_tokens, &pos, "Deck")) {
        if(match_question_mark(tokens, num_tokens, &pos) && pos == num_tokens) {
            handle_deck_query(state); return 1;
        } else if(match_keyword(tokens, num_tokens, &pos, "size") && match_question_mark(tokens, num_tokens, &pos) && pos == num_tokens) {
            handle_deck_size_query(state); return 1;
        }
    }

    /* ---- "Relics ?" ---- */
    pos = 0;
    if(match_keyword(tokens, num_tokens, &pos, "Relics") && match_question_mark(tokens, num_tokens, &pos) && pos == num_tokens) {
        handle_relics_query(state); return 1;
    }

    /* ---- "Potions ?" ---- */
    pos = 0;
    if(match_keyword(tokens, num_tokens, &pos, "Potions") && match_question_mark(tokens, num_tokens, &pos) && pos == num_tokens) {
        handle_potions_query(state); return 1;
    }

    /* ---- "What is effective against <Enemy> ?" ---- */
    pos = 0;
    if(match_keyword(tokens, num_tokens, &pos, "What") && match_keyword(tokens, num_tokens, &pos, "is") && match_keyword(tokens, num_tokens, &pos, "effective") && match_keyword(tokens, num_tokens, &pos, "against")) {
        char* name = NULL;
        const char* stops[] = {"?"};
        if(match_name(tokens, num_tokens, &pos, &name, stops, 1) && match_question_mark(tokens, num_tokens, &pos)) {
            if(pos == num_tokens) { handle_effective_query(state, name); free(name); return 1; }
        }
        if(name) free(name);
    }

    /* ---- "Defeated <Enemy> ?" ---- */
    pos = 0;
    if(match_keyword(tokens, num_tokens, &pos, "Defeated")) {
        char* name = NULL;
        const char* stops[] = {"?"};
        if(match_name(tokens, num_tokens, &pos, &name, stops, 1) && match_question_mark(tokens, num_tokens, &pos)) {
            if(pos == num_tokens) { handle_defeated_query(state, name); free(name); return 1; }
        }
        if(name) free(name);
    }

    /* ---- "Health ?" ---- */
    pos = 0;
    if(match_keyword(tokens, num_tokens, &pos, "Health") && match_question_mark(tokens, num_tokens, &pos) && pos == num_tokens) {
        handle_health_query(state); return 1;
    }

    /* ---- "Exhausts ?" ---- */
    pos = 0;
    if(match_keyword(tokens, num_tokens, &pos, "Exhausts") && match_question_mark(tokens, num_tokens, &pos) && pos == num_tokens) {
        handle_exhausts_query(state); return 1;
    }

    /* No grammar rule matched - caller (main) will print "INVALID" */
    return 0;
}
