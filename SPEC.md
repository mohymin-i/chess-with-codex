# High Performance Chess Engine Specification

## Goal

Build a simple two-player chess engine in C++ that can validate and execute legal chess moves using 64-bit bitboards. The first milestone is a correct, fast, terminal- or API-friendly engine core that accepts algebraic chess notation as input and emits algebraic chess notation as output.

The design must be suitable for later expansion with chess bots, search, and evaluation. This specification covers only the initial simple bot opponent; it does not cover full position evaluation, search algorithms, opening books, or time controls.

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
- Optional Unicode board printing in the terminal.
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
- Position evaluation.
- Search.
- UCI/XBoard protocols.
- PGN database management.
- Graphical UI.

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
- In bot mode, show a bot selection screen before the game starts.
- Print a short prompt explaining accepted SAN examples after mode selection.
- Print the available commands after mode selection.
- If the `--printBoard` flag is present, print the current board before the first prompt.

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
- `2. Gary Chess`

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
    virtual std::optional<Move> chooseMove(const Board& board) const = 0;
};
```

The initial easy bot should be named `John Checkers`.

`John Checkers` requirements:

- Always choose from the current legal move list.
- Return no move when no legal moves exist.
- Be deterministic for repeatable tests.
- Prefer checkmate when available.
- Prefer captures, promotions, checks, castling, and central moves using a simple heuristic.
- Avoid depending on search, static evaluation, opening books, or time controls.

The first difficult bot should be named `Gary Chess`.

`Gary Chess` requirements:

- Always choose from the current legal move list.
- Return no move when no legal moves exist.
- Be deterministic for repeatable tests.
- Use an efficient alpha-beta search with iterative deepening.
- Prefer forced mate when available.
- Use a static evaluation with material and positional terms.
- Use move ordering so forcing moves are searched early.
- Respect a hard move budget of less than 10 seconds.
- It is acceptable for Gary Chess to take longer than John Checkers.

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
- Static evaluation.
- Search.
- Transposition tables.
- Zobrist hashing.
- Move ordering.
- UCI protocol support.
- PGN import/export.
- Time controls.
- Opening books.

These features should not be implemented as part of the initial two-player spec, but the board representation, move encoding, and make/unmake flow should not block them.

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
- `John Checkers` can play legal moves against the human.
- `Gary Chess` can play legal moves against the human and should move within 10 seconds.
- `--printBoard` prints the Unicode board in addition to normal move/status output.
- `print` and `p` print the board during play.
- `print toggle` toggles automatic board printing during play and confirms the new state.
- `material` displays captured pieces and point totals.
- `material toggle` toggles a board sidebar that shows captured material to the right of the board.
- Illegal moves produce an explicit warning.
- Resignation ends the game and declares the opponent the winner.
- Checkmate and draw outcomes print a game-over message and quit.
