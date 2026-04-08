#ifndef PARSER_H
#define PARSER_H

#include "spireslayer.h"

/*
 * parser.h - Interface for the Spireslayer command parser/executor.
 *
 * The parser tokenizes each input line by ASCII spaces, then attempts to match
 * the token sequence against every valid grammar rule using a backtracking
 * pattern-matching approach. If a match is found AND all tokens are consumed,
 * the corresponding state mutation is performed and the response is printed.
 *
 * Design: The parser returns a success/failure flag rather than printing
 * "INVALID" itself, following the starter template pattern where main()
 * is responsible for printing "INVALID" on failure.
 */

/*
 * execute_line - Parse and execute a single command line.
 *
 * Tokenizes the input by spaces, then attempts to match it against all
 * valid grammar rules (gains, buys, sells, fights, enters, learns, etc.).
 * If a match is found and semantically valid, the corresponding output
 * is printed and the WorldState is updated accordingly.
 *
 * @param state  Pointer to the persistent game state to mutate.
 * @param line   Null-terminated input string (already newline-stripped).
 * @return       1 if the command was valid and handled (output was printed),
 *               0 if no grammar rule matched (caller should print "INVALID").
 */
int execute_line(WorldState* state, const char* line);

#endif
