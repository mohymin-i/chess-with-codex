# High Performance Chess Engine Specification

## Goal

Build a simple chess engine in C++ that can validate and execute legal chess moves using 64-bit bitboards. The first milestone is a correct, fast, terminal-, GUI-, and API-friendly engine core that accepts algebraic chess notation as input and emits algebraic chess notation as output.

The design must be suitable for continued expansion with stronger chess bots, search, evaluation, and richer interfaces. This specification covers the current playable CLI and starter GUI, plus the first static evaluation implementation. It does not cover opening books, time controls, UCI/XBoard protocols, or PGN database management.

The current executable provides a command-line two-player game loop on top of the engine core. The CLI is intentionally thin: rule validation, move execution, notation, and game status remain in the reusable engine layer.

## Scope

The initial engine must support:

- Standard chess from the normal starting position.
- Two human/player-controlled sides.
- Legal move generation and validation.
- Move execution and undo support.
- Algebraic chess notation input and output.
- Check, checkmate, stalemate, draw-state detection where required for legal play.
- A command-line two-player interface.
- A command-line bot opponent mode.
- A starter graphical interface for game setup and play.
- Optional saved-game move logs.
- Optional Unicode board printing in the terminal.
- Optional position evaluation display.
- Resignation from either player.
- All standard move types:
  - Quiet moves.
  - Captures.
  - En passant.
  - Kingside and queenside castling.
  - Pawn promotion.

The initial engine does not need to support:

- Chess variants.
- Chess960 castling rules.
- Engine-vs-engine play.
- UCI/XBoard protocols.
- PGN database management.

## Language and Performance Requirements

- Implementation language: C++.
- Build system: CMake.
- Use 64-bit bitboards as the primary board representation.
- Keep the engine core independent from UI, console input, filesystem, and networking.
- Prefer value types and compact structs for hot-path data.
- Avoid heap allocation in move generation and move execution hot paths where practical.
- The design should allow later search code to repeatedly make and unmake moves efficiently.

The repository should build these targets:

- `chess_engine`: reusable engine library.
- `chess_bots`: reusable bot library.
- `chess`: command-line game executable.
- `chess_tests`: focused engine test executable.
- `chess_gui`: native starter GUI target on macOS that outputs `Chess.app`.

## Board Representation

Represent the board using 64-bit bitboards:

- One bitboard per piece type and color:
  - White pawns, knights, bishops, rooks, queens, king.
  - Black pawns, knights, bishops, rooks, queens, king.
- Derived occupancy bitboards:
  - White occupancy.
  - Black occupancy.
  - Combined occupancy.

Square indexing must be documented and used consistently. Recommended convention:

- `a1 = 0`
- `b1 = 1`
- ...
- `h1 = 7`
- `a2 = 8`
- ...
- `h8 = 63`

The engine state must also track:

- Side to move.
- Castling rights for both sides.
- En passant target square, if available.
- Halfmove clock.
- Fullmove number.
- King square for each side, either stored directly or derived cheaply.
- Move history sufficient for undo.

## Core Data Types

The implementation should define compact core types similar to:

- `Color`
  - White or black.
- `PieceType`
  - Pawn, knight, bishop, rook, queen, king, none.
- `Piece`
  - Color plus piece type, or empty.
- `Square`
  - Integer-backed square index from 0 to 63.
- `Move`
  - Source square.
  - Destination square.
  - Move flags.
  - Promotion piece, when applicable.
- `Board`
  - Current bitboards and game-state metadata.
- `UndoState`
  - Minimal state required to reverse a move exactly.
- `MoveList`
  - Fixed-capacity or allocation-light container for generated moves.

Move flags should distinguish at least:

- Quiet move.
- Capture.
- Double pawn push.
- En passant capture.
- Kingside castle.
- Queenside castle.
- Promotion.
- Promotion capture.

## Move Input and Output

The engine must accept moves in algebraic chess notation and output moves in algebraic chess notation.

For implementation clarity, the notation layer should be separate from the engine core:

- The parser converts user input into a `Move`.
- The engine validates the parsed move against the current legal move list.
- The formatter converts executed moves back into notation.

The notation layer should support standard algebraic notation, including:

- Piece moves, such as `Nf3`, `Bb5`, `Qxd5`.
- Pawn moves, such as `e4`, `exd5`.
- Check and checkmate suffixes, such as `+` and `#`.
- Castling, using `O-O` and `O-O-O`.
- Promotion, such as `e8=Q` and `exd8=N`.
- Disambiguation, such as `Nbd2` and `R1e1`.
- Optional capture marker `x` where standard notation requires it.

The parser may also accept coordinate notation, such as `e2e4` and `e7e8Q`, as a convenience. Standard algebraic notation remains the required user-facing format.

Invalid, ambiguous, or illegal notation must produce a clear parse or validation failure without modifying board state.

## Command-Line Interface

The `chess` executable must provide a simple two-player terminal interface.

Startup behavior:

- Start from the standard chess position.
- Print a game-mode menu before the game starts.
- Offer two-player mode and bot mode.
- Default the game-mode selection to bot mode where the UI supports a default.
- In bot mode, show a bot selection screen before the game starts.
- Default the bot selection to `John Checkers` where the UI supports a default.
- Ask whether the player wants to save the game after the player colors are known.
- Ask whether the player wants to show evaluation during the game.
- Print a short prompt explaining accepted SAN examples after mode selection.
- Print the available commands after mode selection.
- If the `--printBoard` flag is present, print the current board before the first prompt.
- If evaluation is enabled, print an initial evaluation for the current position, then a second evaluation shortly after, then a final evaluation a while after that as long as no move has been made.

Supported launch forms:

```bash
./build/chess
./build/chess --printBoard
```

Required startup menu:

- `1. 2 player`
- `2. Bots`

Required bot selection menu:

- `1. John Checkers`
- `2. Level 2`
- `3. Level 3`
- `4. Level 4`
- `5. Level 5`
- `6. Level 6`
- `7. Level 7`
- `8. Level 8`
- `9. Level 9`
- `10. Gary Chess Jr`
- `11. Gary Chess`

The bot selection UI should show a short description for each bot, including the implementation approach.

For bot mode:

- After bot selection, the human player chooses White or Black.
- The selected bot plays the opposite color.
- If the bot is White, the bot moves before the first human prompt.
- After each legal human move, if the game has not ended, the bot should immediately choose and play a legal response.
- Bot moves must be printed in algebraic notation.

Required commands:

- `moves`
  - Print legal moves for the current player in algebraic notation.
- `fen`
  - Print the current FEN.
- `board`
  - Print the current Unicode board.
- `print`
  - Alias for `board`.
- `p`
  - Alias for `board`.
- `print toggle`
  - Toggle automatic board printing after each move.
  - Print a helpful confirmation such as `Print turned on.` or `Print turned off.`.
- `material`
  - Display the pieces captured by each side and the corresponding point totals.
  - If material sidebar display is disabled, this command must not print the board.
  - If material sidebar display is enabled, this command may print the board with material displayed to the right.
- `material toggle`
  - Toggle whether captured material is displayed to the right of board renders.
  - Print a helpful confirmation such as `Material display turned on.` or `Material display turned off.`.
- `resign`
  - End the game immediately. The side to move loses and the opponent wins.
- `quit`
  - Exit without declaring a winner.
- `exit`
  - Alias for `quit`.

Command handling requirements:

- Commands should be trimmed and case-insensitive.
- Move notation should preserve its original case because SAN uses case-significant piece letters.
- Illegal moves must print a warning that explicitly tells the user the move is illegal.
- Invalid, ambiguous, game-over, and other rejected move attempts must print a clear warning.
- At the start of every turn, the CLI must check for terminal draw or win conditions.
- After any move that ends the game, the CLI must print a game-over message and quit immediately.
- After resignation, the CLI must print a game-over message and quit immediately.

### Saved Games

The CLI must ask whether to save each game regardless of whether the game mode is two-player or bot play.

If saving is enabled:

- Create the `saved-games` folder if it does not already exist.
- Create a timestamped `.txt` file in that folder.
- Write the player assignment at the top of the file:
  - `White: <player>`
  - `Black: <player>`
- Write moves below that header in standard algebraic notation.
- Write one full move per line, for example:

```text
White: Human
Black: Gary Chess

Moves:
1. e4 e5
2. Nf3 Nc6
```

The save file should be updated after each successfully played move so quits, resignations, checkmates, and draws preserve the moves that were already played. The engine core must not depend on filesystem behavior; saved-game writing belongs in the CLI layer or another outer application layer.

### Board Rendering

The engine should expose board rendering helpers independent of the CLI.

Required renderers:

- ASCII board rendering for simple debugging.
- Unicode board rendering for terminal display.

The Unicode board must:

- Use standard Unicode chess symbols:
  - White: `♔`, `♕`, `♖`, `♗`, `♘`, `♙`.
  - Black: `♚`, `♛`, `♜`, `♝`, `♞`, `♟`.
- Use visible square separators with characters such as `+`, `-`, and `|`.
- Include file and rank labels.
- Not depend on terminal color support.

## Evaluation

The project must provide a reusable static evaluation module that depends on the engine core and not on any UI layer.

Required evaluation behavior:

- Return centipawn values from a caller-provided perspective.
- Positive values favor the perspective side.
- Negative values favor the opposing side.
- Use standard chess conventions when displaying the value: `+0.34`, `-1.20`, or `0.00`, where `1.00` is one pawn.
- When a forced mate is found in the active search horizon, display `Mate in <N>` instead of a centipawn score.
- Report the shortest mate the winning side can force against best defense within that horizon.
- Show mate-like terminal results as mate-style values where practical.
- Treat drawn terminal positions as equal.

The initial static evaluator should include:

- Material values.
- Light positional terms for development, center control, king safety, open files, passed pawns, bishop pair, and endgame king activity.
- A bounded forced-mate search that can be deepened by the UI over successive evaluation updates.

CLI evaluation requirements:

- Evaluation is enabled or disabled during startup.
- In bot games, evaluate from the human player's perspective.
- In two-player games, evaluate from White's perspective.
- For each position, print exactly three scheduled evaluations when enabled:
  - `Initial` immediately.
  - `Second` shortly after.
  - `Final` a while after that.
- Later scheduled evaluations may search for longer forced mates than the immediate evaluation.
- If a move is made before the second or final print, cancel the pending prints and restart the schedule from the new position.

GUI evaluation requirements:

- Game setup must include a `Show evaluation` option.
- The game screen must include an evaluation label, animated horizontal bar, numeric value, and `Hide Evaluation` / `Show Evaluation` toggle.
- The player's perspective color must be shown on the right side of the bar and the enemy color on the left.
- If a forced mate is found, the whole bar must become the winning side's color with no opposing-color segment visible.
- In bot games, evaluate from the human player's perspective.
- In two-player games, evaluate from White's perspective.
- When visible, the evaluation must update immediately after each move and then periodically for about 20 seconds.
- When a new move is made, any pending evaluation loop must be cancelled and restarted from the new board.
- When hidden, no evaluations should run.

## Graphical Interface

The first GUI milestone is intentionally shallow and should not replace the CLI yet.

On macOS, the project should build a native `chess_gui` app target. The starter GUI must:

- Show a game setup screen when launched.
- Let the player choose two-player mode or bot mode.
- In bot mode, let the player choose a bot and the human color.
- Let the player choose whether to save the game.
- Let the player choose whether to show evaluation.
- Provide a settings page from the setup screen.
- The settings page should expose functional board appearance, piece appearance,
  theme, autopromote, and default promotion-piece preferences.
- After setup, switch into a game screen with the current chess position.
- Render pieces from engine state using Unicode chess symbols.
- Move pieces by selecting a piece and then selecting a legal destination square.
- Highlight legal destinations for the selected piece with small translucent dots.
- Highlight the checked king's square in red.
- When a human pawn promotes, show an inline in-window promotion choice for
  Queen, Rook, Bishop, and Knight.
- Do not use modal dialogs, floating popup menus, or alert popups during
  gameplay; surface warnings and outcomes through in-window status/message text.
- Invalid human moves should display an in-window illegal-move message.
- In bot mode, move the selected bot's pieces after legal human moves.
- If evaluation is enabled, show the animated evaluation bar and value.
- Let the player hide and show evaluation during play.
- Provide a `Flip Board` button that swaps between White-at-bottom and
  Black-at-bottom orientation.
- In bot games, default the board to the human player's perspective, so human
  Black starts with Black at the bottom.
- In two-player games, enable `Auto-flip` by default so the side to move is
  shown at the bottom after each turn.
- Provide an `Auto-flip` option that disables turn-by-turn board flipping when unchecked.
- Place `Resign` and `Return to Main Menu` buttons to the right of the board.
- Only show the in-window `Quit` button on the main setup menu.

The starter GUI does not need to support previous-move highlighting or saved-game writing yet. Saved-game writing remains owned by the CLI until GUI persistence is wired into the existing game orchestration.

## Legal Move Generation

The engine must generate only legal moves for the side to move.

Pseudo-legal generation should cover:

- Pawn single pushes.
- Pawn double pushes from starting rank.
- Pawn captures.
- Pawn promotions.
- En passant candidates.
- Knight moves.
- Bishop moves.
- Rook moves.
- Queen moves.
- King moves.
- Castling candidates.

Legal move filtering must reject moves that leave the moving side's king in check.

The engine must correctly detect:

- Check.
- Double check.
- Pinned pieces.
- Attacked squares.
- Legal king moves.
- Legal evasions from check.

For performance, the implementation may start with pseudo-legal generation plus make/check/unmake filtering, then later optimize using pin masks, check masks, and specialized evasions.

## Special Move Rules

### Castling

Castling is legal only when:

- The relevant castling right is still available.
- The king and rook are on their expected starting squares.
- Squares between the king and rook are empty.
- The king is not currently in check.
- The king does not pass through an attacked square.
- The king does not end on an attacked square.

Executing castling must move both the king and rook and remove the relevant castling rights.

Castling rights must also be removed when:

- A king moves.
- A rook moves from its original square.
- A rook on its original square is captured.

### En Passant

After a legal double pawn push, the engine must set the en passant target square for the next ply only.

An en passant capture is legal only on the immediately following move and only if it does not leave the capturing side's king in check.

Executing en passant must:

- Move the capturing pawn to the en passant target square.
- Remove the captured pawn from its actual square.
- Clear the en passant target square.

### Promotion

A pawn reaching the final rank must promote.

Supported promotion pieces:

- Queen.
- Rook.
- Bishop.
- Knight.

Promotion to king or pawn is illegal.

If notation omits the promotion piece when promotion is required, parsing or validation must fail.

## Game State and Outcomes

The engine must expose enough state to determine:

- Side to move.
- Whether the current side is in check.
- Whether the current side has legal moves.
- Checkmate.
- Stalemate.
- Draw by fifty-move rule eligibility.
- Draw by insufficient material, at minimum for obvious cases:
  - King versus king.
  - King and bishop versus king.
  - King and knight versus king.

Threefold repetition support is desirable for future extensibility, but it does not need to be implemented in the first version unless a position hash is added early.

The CLI must report terminal outcomes as game-over messages:

- Checkmate: the side that delivered mate wins.
- Stalemate: draw.
- Fifty-move rule: draw.
- Insufficient material: draw.
- Resignation: the resigning side loses.

## Bot Architecture

The first bot implementation must provide a small reusable interface for future bots.

Required bot API shape:

```cpp
class ChessBot {
public:
    virtual ~ChessBot() = default;

    virtual std::string_view name() const = 0;
    virtual std::string_view description() const = 0;
    virtual std::optional<Move> chooseMove(const Board& board) const = 0;
};
```

The default bot roster must contain eleven bots ordered from weakest to strongest:

1. `John Checkers`
2. `Level 2`
3. `Level 3`
4. `Level 4`
5. `Level 5`
6. `Level 6`
7. `Level 7`
8. `Level 8`
9. `Level 9`
10. `Gary Chess Jr`
11. `Gary Chess`

The initial easy bot should be named `John Checkers`.

`John Checkers` requirements:

- Always choose from the current legal move list.
- Return no move when no legal moves exist.
- Be deterministic for repeatable tests.
- Prefer checkmate when available.
- Prefer captures, promotions, checks, castling, and central moves using a simple heuristic.
- Avoid depending on search, static evaluation, opening books, or time controls.

The numbered bots from level 2 through level 9 should use increasing tactical awareness, search depth, and move budget. They must be named exactly `Level 2`, `Level 3`, `Level 4`, `Level 5`, `Level 6`, `Level 7`, `Level 8`, and `Level 9`.

Difficulty targets are approximate because the project does not yet run rated engine matches:

- `Level 5` should target roughly 1300 Elo.
- `Level 7` should target roughly 2300 Elo.
- `Level 8` should target elite-human strength and should only be beatable by the best humans.
- `Level 9` should be extremely strong and effectively impossible for normal human play.

The strongest bot should be named `Gary Chess`.

`Gary Chess Jr` requirements:

- Always choose from the current legal move list.
- Return no move when no legal moves exist.
- Use the same static evaluator and iterative alpha-beta search family as `Gary Chess`.
- Prefer much faster move selection in simple positions by using a shorter time
  cap, more aggressive early exit, root candidate pruning, late-move reductions,
  and shallower quiescence.
- Remain deterministic for repeatable tests.

`Gary Chess` requirements:

- Always choose from the current legal move list.
- Return no move when no legal moves exist.
- Be deterministic for repeatable tests.
- Use an efficient alpha-beta search with iterative deepening.
- Prefer forced mate when available.
- Use a static evaluation with material and positional terms.
- Use move ordering so forcing moves are searched early.
- Search deeper than the other bots when time permits.
- If a completed early search shows one move is conclusively stronger than the alternatives, stop early and play it.
- Respect a hard move budget of less than 10 seconds.
- It is acceptable for Gary Chess to take longer than John Checkers.

The level 9 bot should be named `Level 9`.

`Level 9` requirements:

- Always choose from the current legal move list.
- Return no move when no legal moves exist.
- Be deterministic for repeatable tests.
- Be stronger than `Level 8` but weaker than `Gary Chess`.
- Use iterative-deepening alpha-beta with quiescence and a shorter search budget than Gary Chess.
- If a completed early search shows one move is conclusively stronger than the alternatives, stop early and play it.

The bot layer should depend on the engine, but the engine core should not depend on bots.

## Move Execution and Undo

The engine must provide:

- `makeMove(move)`
- `unmakeMove(move, undoState)` or equivalent
- `isLegal(move)`
- `generateLegalMoves()`
- `playMove(move)` or equivalent at the game orchestration layer, so bots can play generated moves directly without converting through notation.

Move execution must update:

- Piece bitboards.
- Occupancy bitboards.
- Side to move.
- Castling rights.
- En passant target.
- Halfmove clock.
- Fullmove number.
- Captured piece information.
- Promotion replacement.
- Check/checkmate-related cached state, if any.

Undo must restore the exact previous board state.

This is required even for the first two-player version because later bots and search will depend on fast, reliable move make/unmake behavior.

## Position Setup

The engine must initialize the standard chess starting position.

FEN import/export is required because it makes testing and future bot development much easier. FEN support must include:

- Piece placement.
- Side to move.
- Castling rights.
- En passant target.
- Halfmove clock.
- Fullmove number.

## Error Handling

The engine should distinguish:

- Invalid notation.
- Ambiguous notation.
- Syntactically valid but illegal move.
- Move attempted after game over.
- Unsupported feature.

Errors should be returned as structured values or result types, not printed directly from the engine core.

The CLI is responsible for translating those structured errors into user-facing warnings.

## Testing Requirements

The implementation should include tests for:

- Initial board setup.
- Bitboard square mapping.
- Legal moves from the starting position.
- Each piece's basic movement.
- Captures.
- Checks and check evasions.
- Pins.
- Checkmate.
- Stalemate.
- Castling legality and execution.
- Castling rights removal.
- En passant legality and execution.
- Illegal en passant that exposes check.
- All promotion piece types.
- Promotion captures.
- Algebraic notation parsing.
- Algebraic notation formatting.
- Make/unmake round trips.
- FEN import/export.
- Captured material tracking.
- Static evaluation.
- Unicode board rendering.
- Bot move selection from legal moves.
- CLI smoke behavior for illegal moves, board printing, resignation, and game-over exit where practical.

Perft testing is strongly recommended for validating move generation. The engine should eventually expose a perft helper that counts legal move trees to a given depth from a position.

## Public Engine Interface

The first implementation should expose a small public API similar to:

```cpp
class ChessGame {
public:
    static ChessGame standard();
    static std::optional<ChessGame> fromFen(std::string_view fen, std::string* error = nullptr);

    MoveResult playAlgebraic(std::string_view notation);
    MoveResult playMove(const Move& move);
    std::vector<std::string> legalMovesAlgebraic() const;

    GameStatus status() const;
    Color sideToMove() const;
    const Board& board() const;
    CapturedMaterial capturedMaterial() const;
    std::string fen() const;
    std::string ascii() const;
    std::string unicodeBoard() const;
};
```

This interface is illustrative, not mandatory. The important boundary is that notation parsing and game orchestration are separate from bitboard move generation and board mutation.

The lower-level `Board` API should expose:

- `Board::standard()`
- `Board::fromFen(...)`
- `toFen()`
- `generateLegalMoves()`
- `isLegal(move)`
- `makeMove(move)`
- `unmakeMove(move, undoState)`
- attack/check helpers
- ASCII and Unicode renderers

## Future Extension Points

The initial architecture should leave room for:

- Stronger bot players.
- Additional bot personalities and strengths.
- Deeper evaluation/search features.
- Transposition tables.
- Zobrist hashing.
- Move ordering.
- UCI protocol support.
- PGN import/export.
- Time controls.
- Opening books.

The board representation, move encoding, evaluation boundary, and make/unmake flow should not block these later features.

## First Milestone Acceptance Criteria

The first implementation is complete when:

- A new standard game can be created.
- A player can submit algebraic notation for a legal move.
- The engine validates and applies the move.
- The engine rejects illegal or malformed moves without changing state.
- The engine outputs legal moves in algebraic notation.
- Turns alternate correctly.
- Check, checkmate, and stalemate are detected.
- En passant, castling, and promotion work correctly.
- The engine core has no dependency on a UI layer.
- Tests cover the required move rules and make/unmake behavior.
- The project builds with CMake.
- The CLI accepts SAN moves and the required commands.
- The CLI presents a game-mode menu on startup.
- The CLI presents a bot selection menu when bot mode is selected.
- The CLI lets the human choose White or Black after selecting a bot.
- The bot roster contains eleven ordered difficulty levels.
- `John Checkers` can play legal moves against the human.
- `Level 2` through `Level 9` can play legal moves against the human.
- `Gary Chess Jr` can play legal moves against the human and should move much
  faster than Gary Chess in simple positions.
- `Gary Chess` can play legal moves against the human and should move within 10 seconds.
- `--printBoard` prints the Unicode board in addition to normal move/status output.
- `print` and `p` print the board during play.
- `print toggle` toggles automatic board printing during play and confirms the new state.
- `material` displays captured pieces and point totals.
- `material toggle` toggles a board sidebar that shows captured material to the right of the board.
- Startup can enable evaluation.
- CLI evaluation prints initial, second, and final values for a position, and cancels pending values after a move.
- GUI evaluation can be shown or hidden, updates after each move, and stops while hidden.
- The GUI app bundle is named `Chess.app` and uses the project icon.
- The CLI executable keeps the name `chess` and uses the project icon where the platform supports file icons.
- The GUI supports manual board flipping and two-player auto-flip.
- Illegal moves produce an explicit warning.
- Resignation ends the game and declares the opponent the winner.
- Checkmate and draw outcomes print a game-over message and quit.
