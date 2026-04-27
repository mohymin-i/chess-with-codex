# Chess with Codex

A C++17 command-line chess game backed by a 64-bit bitboard engine.

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
moves, bot replies in bot mode, a show/hide evaluation bar, manual board
flipping, auto-flip in two-player games, and returning to the main menu from a
game.
Saved-game writing still lives in the CLI for now.

Game setup defaults to Bots mode with John Checkers selected.
The Settings page is currently a disabled placeholder for future visual and
promotion preferences.

Current bots:

- `John Checkers`: easy deterministic one-ply heuristic that scores mate, captures, promotions, checks, castling, and center control.
- `Level 2` through `Level 9`: numbered training bots with increasing search depth, tactical awareness, and move budgets. Level 5 targets roughly 1300 Elo, Level 7 roughly 2300 Elo, Level 8 targets elite-human strength, and Level 9 targets near-impossible human play.
- `Gary Chess`: level 10 search bot with deeper iterative-deepening alpha-beta, quiescence, move ordering, material/positional evaluation, and early exit when one move is clearly strongest.

Evaluation is a centipawn score from the player's perspective in bot games and
from White's perspective in two-player games. When the bounded mate search proves
a forced mate, the display switches to `Mate in N`; the GUI bar becomes the
winning side's color. The CLI prints initial, second, and final scheduled
evaluations for each position when enabled.

## Test

```bash
./build/chess_tests
```

## In-Game Commands

- `moves`: show legal moves.
- `fen`: show the current FEN.
- `board`, `print`, `p`: print the board.
- `print toggle`: toggle automatic board printing.
- `material`: show captured material and point totals.
- `material toggle`: show captured material beside board renders.
- `resign`: resign the current game.
- `quit` or `exit`: leave the program.
