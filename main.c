#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

/*
 * CMPE230 - Assignment 1: Spireslayer Command Interpreter
 * Authors: 2023400048, 2023400210
 * 
 * This program implements a command-line interpreter for the "Spireslayer" game,
 * a text-based simulation inspired by Slay the Spire. The interpreter follows a
 * strict Read-Parse-Respond loop architecture:
 *
 *   1. Print the prompt character (» followed by a space)
 *   2. Read one line of input from stdin
 *   3. If the line is "Exit", terminate gracefully
 *   4. Otherwise, delegate to execute_line() for grammar matching
 *   5. If execute_line() returns 0 (no grammar match), print "INVALID"
 *
 * The main function follows the official starter template structure provided
 * by the course. All game logic is decoupled into parser.c (syntax) and
 * state.c (semantics).
 *
 * Build:  make
 * Run:    ./spireslayer
 * Test:   make grade
 */

/*
 * chomp_newline - Strip trailing newline and carriage return characters.
 *
 * This function handles three common line-ending formats:
 *   - Unix/Linux: \n
 *   - Windows:    \r\n
 *   - Old Mac:    \r
 *
 * A while-loop is used instead of two separate if-checks to gracefully
 * handle exotic edge cases like \r\r\n from piped foreign shell inputs.
 *
 * @param line  Mutable string buffer to sanitize in-place.
 */
static void chomp_newline(char *line) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[len - 1] = '\0';
        len--;
    }
}

/*
 * main - Entry point for the Spireslayer interpreter.
 *
 * Initializes the game state (80 HP, 0 gold, floor 0, room NONE),
 * then enters the Read-Parse-Respond loop. The loop continues until
 * either EOF is reached on stdin or the exact command "Exit" is entered.
 *
 * After the loop ends, all dynamically allocated memory is freed via
 * cleanup_world_state() to prevent memory leaks.
 *
 * @return 0 on successful termination.
 */
int main(void) {
    /* Buffer sized for the maximum line length (1024) plus newline and null terminator */
    char line[MAX_LINE_LENGTH + 2];
    WorldState state;

    /* Set all game state fields to their initial values as defined in the spec */
    init_world_state(&state);

    /* Main Read-Parse-Respond loop */
    while (1) {
        /* Print the prompt: Unicode right-pointing double angle quotation mark (U+00BB)
         * followed by exactly one ASCII space, as required by the assignment specification.
         * fflush ensures the prompt appears even when stdout is line-buffered or piped. */
        printf("» ");
        fflush(stdout);

        /* Read one line from stdin. fgets returns NULL on EOF or read error,
         * which cleanly breaks us out of the loop. The buffer is sized to
         * accommodate MAX_LINE_LENGTH characters plus a newline and null byte. */
        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }

        /* Remove trailing newline/carriage-return characters */
        chomp_newline(line);

        /* The "Exit" command is handled directly in main, before any parsing.
         * It must be an exact match with no leading/trailing spaces.
         * This follows the official starter template design. */
        if (strcmp(line, "Exit") == 0) {
            break;
        }

        /* Delegate to the parser/executor. execute_line() attempts to match
         * the input against all valid grammar rules. If a rule matches, it
         * performs the corresponding state mutation and prints the response,
         * returning 1. If no rule matches, it returns 0 and we print INVALID. */
        if (!execute_line(&state, line)) {
            printf("INVALID\n");
        }
    }

    /* Free all dynamically allocated memory (deck, relics, codex, potions, exhausts)
     * to prevent memory leaks. This is called on both Exit and EOF paths. */
    cleanup_world_state(&state);

    return 0;
}
