#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chess {

using Bitboard = std::uint64_t;
using Square = int;

// Square convention is a1 = 0, h1 = 7, a8 = 56, h8 = 63.
constexpr Square NoSquare = -1;

enum class Color : std::uint8_t {
    White = 0,
    Black = 1,
};

enum class PieceType : std::uint8_t {
    Pawn = 0,
    Knight = 1,
    Bishop = 2,
    Rook = 3,
    Queen = 4,
    King = 5,
    None = 6,
};

struct Piece {
    Color color = Color::White;
    PieceType type = PieceType::None;

    bool isEmpty() const;
};

enum class MoveFlag : std::uint8_t {
    Quiet,
    Capture,
    DoublePawnPush,
    EnPassant,
    KingCastle,
    QueenCastle,
    Promotion,
    PromotionCapture,
};

struct Move {
    Square from = NoSquare;
    Square to = NoSquare;
    PieceType promotion = PieceType::None;
    MoveFlag flag = MoveFlag::Quiet;

    bool isCapture() const;
    bool isPromotion() const;
    bool isCastle() const;
};

bool operator==(const Move& lhs, const Move& rhs);
bool operator!=(const Move& lhs, const Move& rhs);

struct UndoState {
    Piece captured;
    std::uint8_t castlingRights = 0;
    Square enPassantSquare = NoSquare;
    int halfmoveClock = 0;
    int fullmoveNumber = 1;
};

enum class MoveError {
    None,
    InvalidNotation,
    AmbiguousNotation,
    IllegalMove,
    GameOver,
};

enum class GameStatus {
    Ongoing,
    Check,
    Checkmate,
    Stalemate,
    FiftyMoveDraw,
    InsufficientMaterialDraw,
};

struct MoveResult {
    bool ok = false;
    MoveError error = MoveError::None;
    Move move;
    std::string notation;
    GameStatus status = GameStatus::Ongoing;
};

struct CapturedMaterial {
    // Counts are indexed by PieceType. `byWhite` means pieces captured by White.
    std::array<int, 6> byWhite{};
    std::array<int, 6> byBlack{};
};

// Board is the reusable rules layer. It owns position state and legal move
// generation, but knows nothing about terminal prompts, bots, or UI commands.
class Board {
public:
    static Board standard();
    static Board empty(Color sideToMove = Color::White);
    static std::optional<Board> fromFen(std::string_view fen, std::string* error = nullptr);

    std::string toFen() const;

    Color sideToMove() const;
    std::uint8_t castlingRights() const;
    Square enPassantSquare() const;
    int halfmoveClock() const;
    int fullmoveNumber() const;

    Bitboard pieces(Color color, PieceType type) const;
    Bitboard occupancy(Color color) const;
    Bitboard occupancy() const;
    Piece pieceAt(Square square) const;

    void clear();
    void setSideToMove(Color color);
    void setPieceAt(Square square, Piece piece);

    std::vector<Move> generateLegalMoves() const;
    bool isLegal(const Move& move) const;

    UndoState makeMove(const Move& move);
    void unmakeMove(const Move& move, const UndoState& undo);

    bool isSquareAttacked(Square square, Color byColor) const;
    bool isKingInCheck(Color color) const;
    bool hasInsufficientMaterial() const;

    std::string ascii() const;
    std::string unicode() const;

private:
    friend class ChessGame;

    static constexpr std::uint8_t WhiteKingCastle = 1 << 0;
    static constexpr std::uint8_t WhiteQueenCastle = 1 << 1;
    static constexpr std::uint8_t BlackKingCastle = 1 << 2;
    static constexpr std::uint8_t BlackQueenCastle = 1 << 3;

    std::array<std::array<Bitboard, 6>, 2> pieces_{};
    Color sideToMove_ = Color::White;
    std::uint8_t castlingRights_ = 0;
    Square enPassantSquare_ = NoSquare;
    int halfmoveClock_ = 0;
    int fullmoveNumber_ = 1;

    std::vector<Move> generatePseudoLegalMoves() const;
    void addPiece(Color color, PieceType type, Square square);
    void removePiece(Color color, PieceType type, Square square);
    void removePieceAt(Square square);
    Square kingSquare(Color color) const;
};

// ChessGame is the orchestration layer used by the CLI and bots. It records
// move history, exposes SAN input/output, and preserves the Board API for lower
// level engine work.
class ChessGame {
public:
    static ChessGame standard();
    static std::optional<ChessGame> fromFen(std::string_view fen, std::string* error = nullptr);

    MoveResult playAlgebraic(std::string_view notation);
    MoveResult playMove(const Move& move);
    std::vector<std::string> legalMovesAlgebraic() const;
    GameStatus status() const;

    std::size_t moveCount() const;
    std::optional<Move> lastMove() const;
    Board boardAtPly(std::size_t ply) const;
    ChessGame snapshotAtPly(std::size_t ply) const;

    Color sideToMove() const;
    const Board& board() const;
    CapturedMaterial capturedMaterial() const;
    std::string fen() const;
    std::string ascii() const;
    std::string unicodeBoard() const;

private:
    explicit ChessGame(Board board);

    Board board_;
    std::vector<Move> moveHistory_;
    std::vector<UndoState> undoHistory_;
};

Color opposite(Color color);

std::string squareName(Square square);
std::optional<Square> parseSquare(std::string_view name);

char pieceLetter(PieceType type);
std::optional<PieceType> pieceTypeFromLetter(char letter);
std::string colorName(Color color);
std::string statusName(GameStatus status);
std::string moveErrorName(MoveError error);

std::string formatAlgebraic(const Board& board, const Move& move);

} // namespace chess
