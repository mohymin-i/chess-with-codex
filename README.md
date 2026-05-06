# Chess with Codex

A C++17 chess project with a reusable 64-bit bitboard rules engine, CLI game,
native macOS GUI, static evaluation, analysis display, and a deterministic bot
ladder.

## Highlights

- Legal move generation, SAN input/output, FEN serialization, make/unmake, and
  draw/checkmate detection in a UI-independent engine layer.
- CLI two-player and bot modes with saved move logs, Unicode boards, material
  summaries, and scheduled evaluation output.
- Native macOS GUI with setup flow, legal move highlighting, bot replies,
  evaluation bar, board flipping, and visual preferences.
- Bot ladder from a beginner heuristic player through iterative-deepening
  alpha-beta bots with quiescence and move ordering.
- Focused engine tests, including perft coverage for known move-generation
  positions.

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Run

```bash
./build/chess
```

The game asks whether to save the move log and whether to show evaluation before
play begins. Saved games are written to `saved-games/` as timestamped `.txt`
files with the White/Black player assignments followed by one full move per line
in algebraic notation.

To automatically print the board after moves:

```bash
./build/chess --printBoard
```

On macOS, the build also produces a starter GUI app:

```bash
open build/Chess.app
```

The GUI supports game setup, engine-backed piece rendering, click-to-move legal
moves, bot replies in bot mode, display and promotion preferences, a show/hide
evaluation bar, manual board flipping, auto-flip in two-player games, and
returning to the main menu from a game.
Saved-game writing still lives in the CLI for now.

Game setup defaults to Bots mode with John Checkers selected.

Current bots:

- `John Checkers`: easy deterministic one-ply heuristic that scores mate,
  captures, promotions, checks, castling, and center control.
- `Level 2` through `Level 9`: numbered training bots with increasing search
  depth, tactical awareness, and move budgets. Level 5 targets roughly 1300 Elo,
  Level 7 roughly 2300 Elo, Level 8 targets elite-human strength, and Level 9
  targets near-impossible human play.
- `Gary Chess Jr`: fast level-10 variant that uses Gary Chess's evaluator and
  iterative alpha-beta engine with more aggressive early exit, root pruning,
  late-move reductions, and a shorter move cap.
- `Gary Chess`: level 10 search bot with deeper iterative-deepening alpha-beta,
  quiescence, move ordering, material/positional evaluation, and early exit when
  one move is clearly strongest.

Evaluation is a centipawn score from the player's perspective in bot games and
from White's perspective in two-player games. When the bounded mate search proves
a forced mate, the display switches to `Mate in N`; the GUI bar becomes the
winning side's color. The CLI prints initial, second, and final scheduled
evaluations for each position when enabled.

## Test

```bash
./build/chess_tests
ctest --test-dir build
```

## Project Layout

- `src/chess.h` and `src/board.cpp`: reusable board state, move generation, FEN,
  SAN, game history, and status logic.
- `src/evaluation.*`: static evaluation and bounded forced-mate search.
- `src/bot.*`: bot interface, heuristic bot, numbered training bots, and Gary
  Chess search variants.
- `src/cli.*`: terminal menus, commands, saved game logs, material display, and
  evaluation scheduling.
- `src/gui.mm`: native AppKit GUI.
- `tests/engine_tests.cpp`: engine, notation, bot, evaluation, and perft tests.
- `ROADMAP.md`: prioritized polish plan for turning the project into a stronger
  portfolio artifact.

## In-Game Commands

- `moves`: show legal moves.
- `fen`: show the current FEN.
- `board`, `print`, `p`: print the board.
- `print toggle`: toggle automatic board printing.
- `material`: show captured material and point totals.
- `material toggle`: show captured material beside board renders.
- `resign`: resign the current game.
- `quit` or `exit`: leave the program.
