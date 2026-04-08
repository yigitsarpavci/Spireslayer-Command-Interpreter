* Spireslayer Command Interpreter

*This project implements a fully functional command-line interpreter in C for the "Spireslayer" game, inspired by Slay the Spire. Designed for a systems programming course, the interpreter parses custom pseudo-English grammatical rules to manage the dynamic state of a player's items, relics, and progression within a terminal command loop. The system manages memory extensively via secure dynamic backing stores and features an autonomous backtracking lexical parser.

* Based on the project specification, the platform acts as an encapsulated REPL (Read-Eval-Print Loop), securely tokenizing user input, cross-referencing valid grammar states, tracking state mutations, scaling memory seamlessly via amortized doubling algorithms, and verifying syntactic formatting adherence.

* Dynamic Memory and Inventory Management
  * Autonomous doubling array algorithms spanning unbounded inventory capacities
  * Dynamic string internalization verifying identifier constraint invariants
  * Exhaustive memory cleanup preventing leaks across arbitrary execution faults

* Custom Syntax Parse Engine
  * Iterative backtracking token-matching engine mimicking recursive descent patterns
  * Lexical whitespace strictness enforcing precise identifier syntax integrity
  * Code decoupling delegating parsing logic from strict semantic operations

* State Simulation Subsystem
  * Monitors expansive progression layers including dynamic currencies, relics, and scaling logic
  * Secure constraint enforcement routines blocking invalid transactional query loops
  * Modular codex dynamically logging expansive elemental combat vulnerabilities

* End-To-End Interactive REPL
  * Unicode output stabilization via immediate standard output (fflush) buffering
  * CRLF sanitization defending buffer boundaries against arbitrary pipe redirections
