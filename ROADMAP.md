# Project Polish Roadmap

This project already has a playable engine, CLI, macOS GUI, evaluation, and a bot ladder. The next
work should make those strengths easier to verify, explain, and demo.

## Highest-impact resume improvements

1. Add CI that configures, builds, and runs `chess_tests` on every push.
2. Fix repository packaging before publishing: the root repo currently treats `src` as its own git
   worktree, and several source files are untracked inside it. A resume repo should have one clean
   source tree with all build inputs tracked.
3. Add screenshots or a short GIF of the GUI, plus one CLI transcript showing a bot game and
   evaluation output.
4. Add UCI support so the engine can be connected to common chess GUIs. This is more recognizable
   than a custom CLI when reviewers scan the project.
5. Add a `perft` command or benchmark target so move-generation correctness and speed are visible
   without reading the tests.

## Engine correctness

- Expand the perft suite with well-known castling, en passant, promotion, and check-evasion positions.
- Tighten FEN validation for duplicate castling flags, impossible en passant ranks, missing kings, and
  trailing fields.
- Add threefold repetition and seventy-five-move automatic draw detection.
- Add PGN import/export for saved games instead of the current text-only move log.

## Search and evaluation

- Add Zobrist hashing and a transposition table.
- Add history and killer-move heuristics for better move ordering.
- Add iterative deepening telemetry: nodes searched, depth completed, time used, and principal
  variation.
- Add opening-book support for early-game speed and variety.
- Add tactical test positions and expected best moves for each bot level.

## GUI and UX

- Save games from the GUI, not only the CLI.
- Add move history navigation with previous/next controls.
- Add keyboard shortcuts for promotion selection, board flipping, and common commands.
- Add analysis arrows, last-move highlights, and legal-move destination markers.
- Add a simple post-game review view that shows the final FEN, move list, and evaluation summary.

## Code health

- Split `src/board.cpp` into smaller files: board state, move generation, FEN, SAN, and game history.
- Split `src/gui.mm` into view/controller files once the GUI grows further.
- Replace the hand-rolled test runner with a small test framework only if the test surface grows enough
  to justify the dependency.
- Keep `.clang-format` and `.editorconfig` as the formatting contract for future edits.
