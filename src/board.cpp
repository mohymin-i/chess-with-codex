#include "chess.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace chess {
namespace {

constexpr Bitboard Empty = 0ULL;

int colorIndex(Color color) {
    return static_cast<int>(color);
}

int pieceIndex(PieceType type) {
    return static_cast<int>(type);
}

Bitboard bit(Square square) {
    return 1ULL << square;
}

bool validSquare(Square square) {
    return square >= 0 && square < 64;
}

int fileOf(Square square) {
    return square & 7;
}

int rankOf(Square square) {
    return square >> 3;
}

int popCount(Bitboard board) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(board);
#else
    int count = 0;
    while (board != 0) {
        board &= board - 1;
        ++count;
    }
    return count;
#endif
}

Square popLsb(Bitboard& board) {
#if defined(__GNUC__) || defined(__clang__)
    const Square square = __builtin_ctzll(board);
#else
    Square square = 0;
    Bitboard copy = board;
    while ((copy & 1ULL) == 0) {
        copy >>= 1;
        ++square;
    }
#endif
    board &= board - 1;
    return square;
}

bool isPromotionPiece(PieceType type) {
    return type == PieceType::Queen || type == PieceType::Rook ||
           type == PieceType::Bishop || type == PieceType::Knight;
}

Square step(Square square, int df, int dr) {
    const int file = fileOf(square) + df;
    const int rank = rankOf(square) + dr;
    if (file < 0 || file > 7 || rank < 0 || rank > 7) {
        return NoSquare;
    }
    return rank * 8 + file;
}

Bitboard knightAttacks(Square square) {
    static constexpr std::array<std::array<int, 2>, 8> offsets{{
        {{1, 2}}, {{2, 1}}, {{2, -1}}, {{1, -2}},
        {{-1, -2}}, {{-2, -1}}, {{-2, 1}}, {{-1, 2}},
    }};

    Bitboard attacks = Empty;
    for (const auto& offset : offsets) {
        const Square target = step(square, offset[0], offset[1]);
        if (target != NoSquare) {
            attacks |= bit(target);
        }
    }
    return attacks;
}

Bitboard kingAttacks(Square square) {
    Bitboard attacks = Empty;
    for (int df = -1; df <= 1; ++df) {
        for (int dr = -1; dr <= 1; ++dr) {
            if (df == 0 && dr == 0) {
                continue;
            }
            const Square target = step(square, df, dr);
            if (target != NoSquare) {
                attacks |= bit(target);
            }
        }
    }
    return attacks;
}

char pieceToFen(Color color, PieceType type) {
    char letter = pieceLetter(type);
    if (type == PieceType::Pawn) {
        letter = 'P';
    }
    if (color == Color::Black) {
        letter = static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    }
    return letter;
}

std::string pieceToUnicode(Piece piece) {
    if (piece.isEmpty()) {
        return " ";
    }

    if (piece.color == Color::White) {
        switch (piece.type) {
        case PieceType::King:
            return "♔";
        case PieceType::Queen:
            return "♕";
        case PieceType::Rook:
            return "♖";
        case PieceType::Bishop:
            return "♗";
        case PieceType::Knight:
            return "♘";
        case PieceType::Pawn:
            return "♙";
        case PieceType::None:
            return " ";
        }
    }

    switch (piece.type) {
    case PieceType::King:
        return "♚";
    case PieceType::Queen:
        return "♛";
    case PieceType::Rook:
        return "♜";
    case PieceType::Bishop:
        return "♝";
    case PieceType::Knight:
        return "♞";
    case PieceType::Pawn:
        return "♟";
    case PieceType::None:
        return " ";
    }

    return " ";
}

std::string cleanNotation(std::string_view notation) {
    std::string text;
    text.reserve(notation.size());
    for (char ch : notation) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            text.push_back(ch == '0' ? 'O' : ch);
        }
    }

    auto stripSuffix = [&](std::string_view suffix) {
        if (text.size() < suffix.size()) {
            return false;
        }
        const auto start = text.size() - suffix.size();
        for (std::size_t i = 0; i < suffix.size(); ++i) {
            const char lhs = static_cast<char>(std::tolower(static_cast<unsigned char>(text[start + i])));
            const char rhs = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
            if (lhs != rhs) {
                return false;
            }
        }
        text.erase(start);
        return true;
    };

    stripSuffix("e.p.");
    stripSuffix("ep");

    while (!text.empty()) {
        const char ch = text.back();
        if (ch == '+' || ch == '#' || ch == '!' || ch == '?') {
            text.pop_back();
        } else {
            break;
        }
    }

    return text;
}

struct AlgebraicPattern {
    bool coordinate = false;
    Move coordinateMove;
    bool castleKingSide = false;
    bool castleQueenSide = false;
    PieceType piece = PieceType::Pawn;
    Square to = NoSquare;
    PieceType promotion = PieceType::None;
    bool hasCaptureMarker = false;
    bool hasFileHint = false;
    int fileHint = -1;
    bool hasRankHint = false;
    int rankHint = -1;
};

std::optional<AlgebraicPattern> parseAlgebraicPattern(std::string_view notation) {
    std::string text = cleanNotation(notation);
    if (text.empty()) {
        return std::nullopt;
    }

    AlgebraicPattern pattern;

    if (text == "O-O" || text == "OO") {
        pattern.castleKingSide = true;
        return pattern;
    }
    if (text == "O-O-O" || text == "OOO") {
        pattern.castleQueenSide = true;
        return pattern;
    }

    if (text.size() == 4 || text.size() == 5) {
        const auto from = parseSquare(std::string_view(text).substr(0, 2));
        const auto to = parseSquare(std::string_view(text).substr(2, 2));
        if (from && to) {
            pattern.coordinate = true;
            pattern.coordinateMove.from = *from;
            pattern.coordinateMove.to = *to;
            if (text.size() == 5) {
                const auto promotion = pieceTypeFromLetter(text[4]);
                if (!promotion || !isPromotionPiece(*promotion)) {
                    return std::nullopt;
                }
                pattern.coordinateMove.promotion = *promotion;
            }
            return pattern;
        }
    }

    const std::size_t equals = text.find('=');
    if (equals != std::string::npos) {
        if (equals + 2 != text.size()) {
            return std::nullopt;
        }
        const auto promotion = pieceTypeFromLetter(text[equals + 1]);
        if (!promotion || !isPromotionPiece(*promotion)) {
            return std::nullopt;
        }
        pattern.promotion = *promotion;
        text.erase(equals);
    }

    if (text.size() < 2) {
        return std::nullopt;
    }

    const auto to = parseSquare(std::string_view(text).substr(text.size() - 2, 2));
    if (!to) {
        return std::nullopt;
    }
    pattern.to = *to;
    text.erase(text.size() - 2);

    const std::size_t capture = text.find('x');
    if (capture != std::string::npos) {
        if (text.find('x', capture + 1) != std::string::npos) {
            return std::nullopt;
        }
        pattern.hasCaptureMarker = true;
        text.erase(capture, 1);
    }

    if (!text.empty()) {
        const auto piece = pieceTypeFromLetter(text.front());
        if (piece && *piece != PieceType::Pawn) {
            pattern.piece = *piece;
            text.erase(text.begin());
        }
    }

    if (pattern.piece == PieceType::Pawn) {
        if (text.size() > 1) {
            return std::nullopt;
        }
        if (!text.empty()) {
            if (text[0] < 'a' || text[0] > 'h') {
                return std::nullopt;
            }
            pattern.hasFileHint = true;
            pattern.fileHint = text[0] - 'a';
        }
        return pattern;
    }

    if (text.size() > 2) {
        return std::nullopt;
    }
    if (text.size() == 1) {
        if (text[0] >= 'a' && text[0] <= 'h') {
            pattern.hasFileHint = true;
            pattern.fileHint = text[0] - 'a';
        } else if (text[0] >= '1' && text[0] <= '8') {
            pattern.hasRankHint = true;
            pattern.rankHint = text[0] - '1';
        } else {
            return std::nullopt;
        }
    } else if (text.size() == 2) {
        if (text[0] < 'a' || text[0] > 'h' || text[1] < '1' || text[1] > '8') {
            return std::nullopt;
        }
        pattern.hasFileHint = true;
        pattern.fileHint = text[0] - 'a';
        pattern.hasRankHint = true;
        pattern.rankHint = text[1] - '1';
    }

    return pattern;
}

bool matchesPattern(const Board& board, const Move& move, const AlgebraicPattern& pattern) {
    if (pattern.coordinate) {
        if (move.from != pattern.coordinateMove.from || move.to != pattern.coordinateMove.to) {
            return false;
        }
        if (move.isPromotion()) {
            return move.promotion == pattern.coordinateMove.promotion;
        }
        return pattern.coordinateMove.promotion == PieceType::None;
    }

    if (pattern.castleKingSide) {
        return move.flag == MoveFlag::KingCastle;
    }
    if (pattern.castleQueenSide) {
        return move.flag == MoveFlag::QueenCastle;
    }

    const Piece moved = board.pieceAt(move.from);
    if (moved.isEmpty() || moved.type != pattern.piece || move.to != pattern.to) {
        return false;
    }
    if (pattern.hasCaptureMarker && !move.isCapture()) {
        return false;
    }
    if (pattern.hasFileHint && fileOf(move.from) != pattern.fileHint) {
        return false;
    }
    if (pattern.hasRankHint && rankOf(move.from) != pattern.rankHint) {
        return false;
    }
    if (move.isPromotion()) {
        return move.promotion == pattern.promotion;
    }
    return pattern.promotion == PieceType::None;
}

void appendPromotionMoves(std::vector<Move>& moves, Square from, Square to, bool capture) {
    for (PieceType promotion : {PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight}) {
        moves.push_back(Move{
            from,
            to,
            promotion,
            capture ? MoveFlag::PromotionCapture : MoveFlag::Promotion,
        });
    }
}

void appendMoveIfValidTarget(
    const Board& board,
    std::vector<Move>& moves,
    Square from,
    Square to,
    Bitboard enemy,
    MoveFlag quietFlag = MoveFlag::Quiet
) {
    if (!validSquare(to)) {
        return;
    }
    const Piece target = board.pieceAt(to);
    if (target.type == PieceType::King) {
        return;
    }
    moves.push_back(Move{
        from,
        to,
        PieceType::None,
        (enemy & bit(to)) != 0 ? MoveFlag::Capture : quietFlag,
    });
}

std::string checkSuffixAfter(const Board& board, const Move& move) {
    Board copy = board;
    copy.makeMove(move);
    const Color defender = copy.sideToMove();
    if (!copy.isKingInCheck(defender)) {
        return "";
    }
    return copy.generateLegalMoves().empty() ? "#" : "+";
}

} // namespace

bool Piece::isEmpty() const {
    return type == PieceType::None;
}

bool Move::isCapture() const {
    return flag == MoveFlag::Capture || flag == MoveFlag::EnPassant ||
           flag == MoveFlag::PromotionCapture;
}

bool Move::isPromotion() const {
    return flag == MoveFlag::Promotion || flag == MoveFlag::PromotionCapture;
}

bool Move::isCastle() const {
    return flag == MoveFlag::KingCastle || flag == MoveFlag::QueenCastle;
}

bool operator==(const Move& lhs, const Move& rhs) {
    return lhs.from == rhs.from &&
           lhs.to == rhs.to &&
           lhs.promotion == rhs.promotion &&
           lhs.flag == rhs.flag;
}

bool operator!=(const Move& lhs, const Move& rhs) {
    return !(lhs == rhs);
}

Color opposite(Color color) {
    return color == Color::White ? Color::Black : Color::White;
}

std::string squareName(Square square) {
    if (!validSquare(square)) {
        return "-";
    }
    std::string name;
    name.push_back(static_cast<char>('a' + fileOf(square)));
    name.push_back(static_cast<char>('1' + rankOf(square)));
    return name;
}

std::optional<Square> parseSquare(std::string_view name) {
    if (name.size() != 2) {
        return std::nullopt;
    }
    const char file = name[0];
    const char rank = name[1];
    if (file < 'a' || file > 'h' || rank < '1' || rank > '8') {
        return std::nullopt;
    }
    return (rank - '1') * 8 + (file - 'a');
}

char pieceLetter(PieceType type) {
    switch (type) {
    case PieceType::Knight:
        return 'N';
    case PieceType::Bishop:
        return 'B';
    case PieceType::Rook:
        return 'R';
    case PieceType::Queen:
        return 'Q';
    case PieceType::King:
        return 'K';
    case PieceType::Pawn:
    case PieceType::None:
        return '\0';
    }
    return '\0';
}

std::optional<PieceType> pieceTypeFromLetter(char letter) {
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(letter)))) {
    case 'N':
        return PieceType::Knight;
    case 'B':
        return PieceType::Bishop;
    case 'R':
        return PieceType::Rook;
    case 'Q':
        return PieceType::Queen;
    case 'K':
        return PieceType::King;
    case 'P':
        return PieceType::Pawn;
    default:
        return std::nullopt;
    }
}

std::string colorName(Color color) {
    return color == Color::White ? "White" : "Black";
}

std::string statusName(GameStatus status) {
    switch (status) {
    case GameStatus::Ongoing:
        return "ongoing";
    case GameStatus::Check:
        return "check";
    case GameStatus::Checkmate:
        return "checkmate";
    case GameStatus::Stalemate:
        return "stalemate";
    case GameStatus::FiftyMoveDraw:
        return "draw by fifty-move rule";
    case GameStatus::InsufficientMaterialDraw:
        return "draw by insufficient material";
    }
    return "unknown";
}

std::string moveErrorName(MoveError error) {
    switch (error) {
    case MoveError::None:
        return "none";
    case MoveError::InvalidNotation:
        return "invalid notation";
    case MoveError::AmbiguousNotation:
        return "ambiguous notation";
    case MoveError::IllegalMove:
        return "illegal move";
    case MoveError::GameOver:
        return "game over";
    }
    return "unknown";
}

Board Board::standard() {
    Board board;
    board.pieces_[colorIndex(Color::White)][pieceIndex(PieceType::Pawn)] = 0x000000000000FF00ULL;
    board.pieces_[colorIndex(Color::White)][pieceIndex(PieceType::Knight)] = bit(1) | bit(6);
    board.pieces_[colorIndex(Color::White)][pieceIndex(PieceType::Bishop)] = bit(2) | bit(5);
    board.pieces_[colorIndex(Color::White)][pieceIndex(PieceType::Rook)] = bit(0) | bit(7);
    board.pieces_[colorIndex(Color::White)][pieceIndex(PieceType::Queen)] = bit(3);
    board.pieces_[colorIndex(Color::White)][pieceIndex(PieceType::King)] = bit(4);

    board.pieces_[colorIndex(Color::Black)][pieceIndex(PieceType::Pawn)] = 0x00FF000000000000ULL;
    board.pieces_[colorIndex(Color::Black)][pieceIndex(PieceType::Knight)] = bit(57) | bit(62);
    board.pieces_[colorIndex(Color::Black)][pieceIndex(PieceType::Bishop)] = bit(58) | bit(61);
    board.pieces_[colorIndex(Color::Black)][pieceIndex(PieceType::Rook)] = bit(56) | bit(63);
    board.pieces_[colorIndex(Color::Black)][pieceIndex(PieceType::Queen)] = bit(59);
    board.pieces_[colorIndex(Color::Black)][pieceIndex(PieceType::King)] = bit(60);

    board.sideToMove_ = Color::White;
    board.castlingRights_ = WhiteKingCastle | WhiteQueenCastle | BlackKingCastle | BlackQueenCastle;
    board.enPassantSquare_ = NoSquare;
    board.halfmoveClock_ = 0;
    board.fullmoveNumber_ = 1;
    return board;
}

Board Board::empty(Color sideToMove) {
    Board board;
    board.sideToMove_ = sideToMove;
    return board;
}

std::optional<Board> Board::fromFen(std::string_view fen, std::string* error) {
    std::istringstream input{std::string(fen)};
    std::string placement;
    std::string side;
    std::string castling;
    std::string enPassant;
    int halfmove = 0;
    int fullmove = 1;

    if (!(input >> placement >> side >> castling >> enPassant)) {
        if (error != nullptr) {
            *error = "FEN requires at least piece placement, side, castling, and en passant fields";
        }
        return std::nullopt;
    }
    if (!(input >> halfmove)) {
        halfmove = 0;
    }
    if (!(input >> fullmove)) {
        fullmove = 1;
    }

    Board board;
    int rank = 7;
    int file = 0;
    for (char ch : placement) {
        if (ch == '/') {
            if (file != 8) {
                if (error != nullptr) {
                    *error = "FEN rank does not contain exactly eight squares";
                }
                return std::nullopt;
            }
            --rank;
            file = 0;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            if (ch == '0') {
                if (error != nullptr) {
                    *error = "FEN empty-square counts must be between 1 and 8";
                }
                return std::nullopt;
            }
            file += ch - '0';
            if (file > 8) {
                if (error != nullptr) {
                    *error = "FEN rank contains too many squares";
                }
                return std::nullopt;
            }
            continue;
        }

        const Color color = std::isupper(static_cast<unsigned char>(ch)) ? Color::White : Color::Black;
        const auto piece = pieceTypeFromLetter(ch);
        if (!piece || *piece == PieceType::None) {
            if (error != nullptr) {
                *error = "FEN contains an invalid piece";
            }
            return std::nullopt;
        }
        if (rank < 0 || file > 7) {
            if (error != nullptr) {
                *error = "FEN piece placement is out of bounds";
            }
            return std::nullopt;
        }
        board.addPiece(color, *piece, rank * 8 + file);
        ++file;
    }

    if (rank != 0 || file != 8) {
        if (error != nullptr) {
            *error = "FEN must contain exactly eight ranks";
        }
        return std::nullopt;
    }

    if (side == "w") {
        board.sideToMove_ = Color::White;
    } else if (side == "b") {
        board.sideToMove_ = Color::Black;
    } else {
        if (error != nullptr) {
            *error = "FEN side to move must be w or b";
        }
        return std::nullopt;
    }

    board.castlingRights_ = 0;
    if (castling != "-") {
        for (char ch : castling) {
            switch (ch) {
            case 'K':
                board.castlingRights_ |= WhiteKingCastle;
                break;
            case 'Q':
                board.castlingRights_ |= WhiteQueenCastle;
                break;
            case 'k':
                board.castlingRights_ |= BlackKingCastle;
                break;
            case 'q':
                board.castlingRights_ |= BlackQueenCastle;
                break;
            default:
                if (error != nullptr) {
                    *error = "FEN castling field contains an invalid right";
                }
                return std::nullopt;
            }
        }
    }

    if (enPassant == "-") {
        board.enPassantSquare_ = NoSquare;
    } else {
        const auto square = parseSquare(enPassant);
        if (!square) {
            if (error != nullptr) {
                *error = "FEN en passant square is invalid";
            }
            return std::nullopt;
        }
        board.enPassantSquare_ = *square;
    }

    if (halfmove < 0 || fullmove <= 0) {
        if (error != nullptr) {
            *error = "FEN move counters are invalid";
        }
        return std::nullopt;
    }

    board.halfmoveClock_ = halfmove;
    board.fullmoveNumber_ = fullmove;
    return board;
}

std::string Board::toFen() const {
    std::ostringstream output;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            const Piece piece = pieceAt(rank * 8 + file);
            if (piece.isEmpty()) {
                ++empty;
                continue;
            }
            if (empty != 0) {
                output << empty;
                empty = 0;
            }
            output << pieceToFen(piece.color, piece.type);
        }
        if (empty != 0) {
            output << empty;
        }
        if (rank != 0) {
            output << '/';
        }
    }

    output << ' ' << (sideToMove_ == Color::White ? 'w' : 'b') << ' ';
    std::string castling;
    if ((castlingRights_ & WhiteKingCastle) != 0) {
        castling.push_back('K');
    }
    if ((castlingRights_ & WhiteQueenCastle) != 0) {
        castling.push_back('Q');
    }
    if ((castlingRights_ & BlackKingCastle) != 0) {
        castling.push_back('k');
    }
    if ((castlingRights_ & BlackQueenCastle) != 0) {
        castling.push_back('q');
    }
    output << (castling.empty() ? "-" : castling) << ' ';
    output << (enPassantSquare_ == NoSquare ? "-" : squareName(enPassantSquare_)) << ' ';
    output << halfmoveClock_ << ' ' << fullmoveNumber_;
    return output.str();
}

Color Board::sideToMove() const {
    return sideToMove_;
}

std::uint8_t Board::castlingRights() const {
    return castlingRights_;
}

Square Board::enPassantSquare() const {
    return enPassantSquare_;
}

int Board::halfmoveClock() const {
    return halfmoveClock_;
}

int Board::fullmoveNumber() const {
    return fullmoveNumber_;
}

Bitboard Board::pieces(Color color, PieceType type) const {
    if (type == PieceType::None) {
        return Empty;
    }
    return pieces_[colorIndex(color)][pieceIndex(type)];
}

Bitboard Board::occupancy(Color color) const {
    Bitboard board = Empty;
    for (int piece = 0; piece < 6; ++piece) {
        board |= pieces_[colorIndex(color)][piece];
    }
    return board;
}

Bitboard Board::occupancy() const {
    return occupancy(Color::White) | occupancy(Color::Black);
}

Piece Board::pieceAt(Square square) const {
    if (!validSquare(square)) {
        return {};
    }
    const Bitboard mask = bit(square);
    for (Color color : {Color::White, Color::Black}) {
        for (PieceType type : {
                 PieceType::Pawn,
                 PieceType::Knight,
                 PieceType::Bishop,
                 PieceType::Rook,
                 PieceType::Queen,
                 PieceType::King,
             }) {
            if ((pieces(color, type) & mask) != 0) {
                return Piece{color, type};
            }
        }
    }
    return {};
}

void Board::clear() {
    pieces_ = {};
    castlingRights_ = 0;
    enPassantSquare_ = NoSquare;
    halfmoveClock_ = 0;
    fullmoveNumber_ = 1;
}

void Board::setSideToMove(Color color) {
    sideToMove_ = color;
}

void Board::setPieceAt(Square square, Piece piece) {
    if (!validSquare(square)) {
        return;
    }

    removePieceAt(square);
    if (!piece.isEmpty()) {
        addPiece(piece.color, piece.type, square);
    }

    castlingRights_ = 0;
    enPassantSquare_ = NoSquare;
    halfmoveClock_ = 0;
}

std::vector<Move> Board::generatePseudoLegalMoves() const {
    std::vector<Move> moves;
    moves.reserve(128);

    const Color us = sideToMove_;
    const Color them = opposite(us);
    const Bitboard own = occupancy(us);
    const Bitboard enemy = occupancy(them);
    const Bitboard all = own | enemy;
    const auto canCaptureEnPassantPawn = [&](Square capturedSquare) {
        const Piece captured = pieceAt(capturedSquare);
        return captured.color == them && captured.type == PieceType::Pawn;
    };

    Bitboard pawns = pieces(us, PieceType::Pawn);
    while (pawns != 0) {
        const Square from = popLsb(pawns);
        const int rank = rankOf(from);
        const int file = fileOf(from);

        if (us == Color::White) {
            const Square one = from + 8;
            if (one < 64 && (all & bit(one)) == 0) {
                if (rank == 6) {
                    appendPromotionMoves(moves, from, one, false);
                } else {
                    moves.push_back(Move{from, one, PieceType::None, MoveFlag::Quiet});
                    const Square two = from + 16;
                    if (rank == 1 && (all & bit(two)) == 0) {
                        moves.push_back(Move{from, two, PieceType::None, MoveFlag::DoublePawnPush});
                    }
                }
            }

            if (file > 0) {
                const Square to = from + 7;
                if (to < 64 && (enemy & bit(to)) != 0 && pieceAt(to).type != PieceType::King) {
                    if (rank == 6) {
                        appendPromotionMoves(moves, from, to, true);
                    } else {
                        moves.push_back(Move{from, to, PieceType::None, MoveFlag::Capture});
                    }
                }
                if (to == enPassantSquare_ && (all & bit(to)) == 0 && canCaptureEnPassantPawn(to - 8)) {
                    moves.push_back(Move{from, to, PieceType::None, MoveFlag::EnPassant});
                }
            }
            if (file < 7) {
                const Square to = from + 9;
                if (to < 64 && (enemy & bit(to)) != 0 && pieceAt(to).type != PieceType::King) {
                    if (rank == 6) {
                        appendPromotionMoves(moves, from, to, true);
                    } else {
                        moves.push_back(Move{from, to, PieceType::None, MoveFlag::Capture});
                    }
                }
                if (to == enPassantSquare_ && (all & bit(to)) == 0 && canCaptureEnPassantPawn(to - 8)) {
                    moves.push_back(Move{from, to, PieceType::None, MoveFlag::EnPassant});
                }
            }
        } else {
            const Square one = from - 8;
            if (one >= 0 && (all & bit(one)) == 0) {
                if (rank == 1) {
                    appendPromotionMoves(moves, from, one, false);
                } else {
                    moves.push_back(Move{from, one, PieceType::None, MoveFlag::Quiet});
                    const Square two = from - 16;
                    if (rank == 6 && (all & bit(two)) == 0) {
                        moves.push_back(Move{from, two, PieceType::None, MoveFlag::DoublePawnPush});
                    }
                }
            }

            if (file > 0) {
                const Square to = from - 9;
                if (to >= 0 && (enemy & bit(to)) != 0 && pieceAt(to).type != PieceType::King) {
                    if (rank == 1) {
                        appendPromotionMoves(moves, from, to, true);
                    } else {
                        moves.push_back(Move{from, to, PieceType::None, MoveFlag::Capture});
                    }
                }
                if (to == enPassantSquare_ && (all & bit(to)) == 0 && canCaptureEnPassantPawn(to + 8)) {
                    moves.push_back(Move{from, to, PieceType::None, MoveFlag::EnPassant});
                }
            }
            if (file < 7) {
                const Square to = from - 7;
                if (to >= 0 && (enemy & bit(to)) != 0 && pieceAt(to).type != PieceType::King) {
                    if (rank == 1) {
                        appendPromotionMoves(moves, from, to, true);
                    } else {
                        moves.push_back(Move{from, to, PieceType::None, MoveFlag::Capture});
                    }
                }
                if (to == enPassantSquare_ && (all & bit(to)) == 0 && canCaptureEnPassantPawn(to + 8)) {
                    moves.push_back(Move{from, to, PieceType::None, MoveFlag::EnPassant});
                }
            }
        }
    }

    Bitboard knights = pieces(us, PieceType::Knight);
    while (knights != 0) {
        const Square from = popLsb(knights);
        Bitboard targets = knightAttacks(from) & ~own;
        while (targets != 0) {
            appendMoveIfValidTarget(*this, moves, from, popLsb(targets), enemy);
        }
    }

    const auto addSliding = [&](PieceType type, const std::array<std::array<int, 2>, 4>& directions) {
        Bitboard sliders = pieces(us, type);
        while (sliders != 0) {
            const Square from = popLsb(sliders);
            for (const auto& direction : directions) {
                Square to = from;
                while (true) {
                    to = step(to, direction[0], direction[1]);
                    if (to == NoSquare) {
                        break;
                    }
                    if ((own & bit(to)) != 0) {
                        break;
                    }
                    const Piece target = pieceAt(to);
                    if (target.type == PieceType::King) {
                        break;
                    }
                    moves.push_back(Move{
                        from,
                        to,
                        PieceType::None,
                        (enemy & bit(to)) != 0 ? MoveFlag::Capture : MoveFlag::Quiet,
                    });
                    if ((enemy & bit(to)) != 0) {
                        break;
                    }
                }
            }
        }
    };

    static constexpr std::array<std::array<int, 2>, 4> bishopDirections{{
        {{1, 1}}, {{-1, 1}}, {{1, -1}}, {{-1, -1}},
    }};
    static constexpr std::array<std::array<int, 2>, 4> rookDirections{{
        {{1, 0}}, {{-1, 0}}, {{0, 1}}, {{0, -1}},
    }};

    addSliding(PieceType::Bishop, bishopDirections);
    addSliding(PieceType::Rook, rookDirections);

    Bitboard queens = pieces(us, PieceType::Queen);
    while (queens != 0) {
        const Square from = popLsb(queens);
        for (const auto& directions : {bishopDirections, rookDirections}) {
            for (const auto& direction : directions) {
                Square to = from;
                while (true) {
                    to = step(to, direction[0], direction[1]);
                    if (to == NoSquare || (own & bit(to)) != 0) {
                        break;
                    }
                    const Piece target = pieceAt(to);
                    if (target.type == PieceType::King) {
                        break;
                    }
                    moves.push_back(Move{
                        from,
                        to,
                        PieceType::None,
                        (enemy & bit(to)) != 0 ? MoveFlag::Capture : MoveFlag::Quiet,
                    });
                    if ((enemy & bit(to)) != 0) {
                        break;
                    }
                }
            }
        }
    }

    const Square king = kingSquare(us);
    if (king != NoSquare) {
        Bitboard targets = kingAttacks(king) & ~own;
        while (targets != 0) {
            appendMoveIfValidTarget(*this, moves, king, popLsb(targets), enemy);
        }

        if (us == Color::White && king == 4 && !isKingInCheck(us)) {
            if ((castlingRights_ & WhiteKingCastle) != 0 &&
                (pieces(Color::White, PieceType::Rook) & bit(7)) != 0 &&
                (all & (bit(5) | bit(6))) == 0 &&
                !isSquareAttacked(5, them) &&
                !isSquareAttacked(6, them)) {
                moves.push_back(Move{4, 6, PieceType::None, MoveFlag::KingCastle});
            }
            if ((castlingRights_ & WhiteQueenCastle) != 0 &&
                (pieces(Color::White, PieceType::Rook) & bit(0)) != 0 &&
                (all & (bit(1) | bit(2) | bit(3))) == 0 &&
                !isSquareAttacked(3, them) &&
                !isSquareAttacked(2, them)) {
                moves.push_back(Move{4, 2, PieceType::None, MoveFlag::QueenCastle});
            }
        } else if (us == Color::Black && king == 60 && !isKingInCheck(us)) {
            if ((castlingRights_ & BlackKingCastle) != 0 &&
                (pieces(Color::Black, PieceType::Rook) & bit(63)) != 0 &&
                (all & (bit(61) | bit(62))) == 0 &&
                !isSquareAttacked(61, them) &&
                !isSquareAttacked(62, them)) {
                moves.push_back(Move{60, 62, PieceType::None, MoveFlag::KingCastle});
            }
            if ((castlingRights_ & BlackQueenCastle) != 0 &&
                (pieces(Color::Black, PieceType::Rook) & bit(56)) != 0 &&
                (all & (bit(57) | bit(58) | bit(59))) == 0 &&
                !isSquareAttacked(59, them) &&
                !isSquareAttacked(58, them)) {
                moves.push_back(Move{60, 58, PieceType::None, MoveFlag::QueenCastle});
            }
        }
    }

    return moves;
}

std::vector<Move> Board::generateLegalMoves() const {
    Board copy = *this;
    const std::vector<Move> pseudo = copy.generatePseudoLegalMoves();
    std::vector<Move> legal;
    legal.reserve(pseudo.size());

    // Simpler and safer first-pass legality: make every pseudo-legal move,
    // reject it if our own king is still attacked, then restore the position.
    // This is slower than pinned-piece masks, but the make/unmake path is the
    // same one future search code will exercise heavily.
    for (const Move& move : pseudo) {
        const Color mover = copy.sideToMove_;
        const UndoState undo = copy.makeMove(move);
        if (!copy.isKingInCheck(mover)) {
            legal.push_back(move);
        }
        copy.unmakeMove(move, undo);
    }

    return legal;
}

bool Board::isLegal(const Move& move) const {
    const std::vector<Move> legal = generateLegalMoves();
    return std::find(legal.begin(), legal.end(), move) != legal.end();
}

UndoState Board::makeMove(const Move& move) {
    const Piece moved = pieceAt(move.from);
    const Color us = moved.color;
    const Color them = opposite(us);
    const Square capturedSquare = move.flag == MoveFlag::EnPassant
        ? (us == Color::White ? move.to - 8 : move.to + 8)
        : move.to;

    // UndoState stores only the mutable state that cannot be derived from the
    // move itself. That keeps search-friendly make/unmake cheap.
    UndoState undo;
    undo.castlingRights = castlingRights_;
    undo.enPassantSquare = enPassantSquare_;
    undo.halfmoveClock = halfmoveClock_;
    undo.fullmoveNumber = fullmoveNumber_;
    undo.captured = move.isCapture() ? pieceAt(capturedSquare) : Piece{};

    enPassantSquare_ = NoSquare;

    if (move.isCapture()) {
        removePieceAt(capturedSquare);
    }

    removePiece(us, moved.type, move.from);

    if (move.flag == MoveFlag::KingCastle) {
        addPiece(us, PieceType::King, move.to);
        if (us == Color::White) {
            removePiece(Color::White, PieceType::Rook, 7);
            addPiece(Color::White, PieceType::Rook, 5);
        } else {
            removePiece(Color::Black, PieceType::Rook, 63);
            addPiece(Color::Black, PieceType::Rook, 61);
        }
    } else if (move.flag == MoveFlag::QueenCastle) {
        addPiece(us, PieceType::King, move.to);
        if (us == Color::White) {
            removePiece(Color::White, PieceType::Rook, 0);
            addPiece(Color::White, PieceType::Rook, 3);
        } else {
            removePiece(Color::Black, PieceType::Rook, 56);
            addPiece(Color::Black, PieceType::Rook, 59);
        }
    } else if (move.isPromotion()) {
        addPiece(us, move.promotion, move.to);
    } else {
        addPiece(us, moved.type, move.to);
    }

    if (moved.type == PieceType::King) {
        if (us == Color::White) {
            castlingRights_ &= static_cast<std::uint8_t>(~(WhiteKingCastle | WhiteQueenCastle));
        } else {
            castlingRights_ &= static_cast<std::uint8_t>(~(BlackKingCastle | BlackQueenCastle));
        }
    }

    if (moved.type == PieceType::Rook) {
        if (move.from == 0) {
            castlingRights_ &= static_cast<std::uint8_t>(~WhiteQueenCastle);
        } else if (move.from == 7) {
            castlingRights_ &= static_cast<std::uint8_t>(~WhiteKingCastle);
        } else if (move.from == 56) {
            castlingRights_ &= static_cast<std::uint8_t>(~BlackQueenCastle);
        } else if (move.from == 63) {
            castlingRights_ &= static_cast<std::uint8_t>(~BlackKingCastle);
        }
    }

    if (!undo.captured.isEmpty() && undo.captured.type == PieceType::Rook) {
        if (capturedSquare == 0) {
            castlingRights_ &= static_cast<std::uint8_t>(~WhiteQueenCastle);
        } else if (capturedSquare == 7) {
            castlingRights_ &= static_cast<std::uint8_t>(~WhiteKingCastle);
        } else if (capturedSquare == 56) {
            castlingRights_ &= static_cast<std::uint8_t>(~BlackQueenCastle);
        } else if (capturedSquare == 63) {
            castlingRights_ &= static_cast<std::uint8_t>(~BlackKingCastle);
        }
    }

    if (move.flag == MoveFlag::DoublePawnPush) {
        enPassantSquare_ = us == Color::White ? move.from + 8 : move.from - 8;
    }

    halfmoveClock_ = moved.type == PieceType::Pawn || !undo.captured.isEmpty()
        ? 0
        : halfmoveClock_ + 1;

    if (us == Color::Black) {
        ++fullmoveNumber_;
    }
    sideToMove_ = them;

    return undo;
}

void Board::unmakeMove(const Move& move, const UndoState& undo) {
    const Color mover = opposite(sideToMove_);
    const Square capturedSquare = move.flag == MoveFlag::EnPassant
        ? (mover == Color::White ? move.to - 8 : move.to + 8)
        : move.to;

    sideToMove_ = mover;

    if (move.flag == MoveFlag::KingCastle) {
        removePiece(mover, PieceType::King, move.to);
        addPiece(mover, PieceType::King, move.from);
        if (mover == Color::White) {
            removePiece(Color::White, PieceType::Rook, 5);
            addPiece(Color::White, PieceType::Rook, 7);
        } else {
            removePiece(Color::Black, PieceType::Rook, 61);
            addPiece(Color::Black, PieceType::Rook, 63);
        }
    } else if (move.flag == MoveFlag::QueenCastle) {
        removePiece(mover, PieceType::King, move.to);
        addPiece(mover, PieceType::King, move.from);
        if (mover == Color::White) {
            removePiece(Color::White, PieceType::Rook, 3);
            addPiece(Color::White, PieceType::Rook, 0);
        } else {
            removePiece(Color::Black, PieceType::Rook, 59);
            addPiece(Color::Black, PieceType::Rook, 56);
        }
    } else if (move.isPromotion()) {
        removePiece(mover, move.promotion, move.to);
        addPiece(mover, PieceType::Pawn, move.from);
    } else {
        const Piece moved = pieceAt(move.to);
        removePiece(mover, moved.type, move.to);
        addPiece(mover, moved.type, move.from);
    }

    if (!undo.captured.isEmpty()) {
        addPiece(undo.captured.color, undo.captured.type, capturedSquare);
    }

    castlingRights_ = undo.castlingRights;
    enPassantSquare_ = undo.enPassantSquare;
    halfmoveClock_ = undo.halfmoveClock;
    fullmoveNumber_ = undo.fullmoveNumber;
}

bool Board::isSquareAttacked(Square square, Color byColor) const {
    if (!validSquare(square)) {
        return false;
    }

    const Bitboard pawns = pieces(byColor, PieceType::Pawn);
    const int file = fileOf(square);
    if (byColor == Color::White) {
        if (file < 7 && square >= 7 && (pawns & bit(square - 7)) != 0) {
            return true;
        }
        if (file > 0 && square >= 9 && (pawns & bit(square - 9)) != 0) {
            return true;
        }
    } else {
        if (file > 0 && square + 7 < 64 && (pawns & bit(square + 7)) != 0) {
            return true;
        }
        if (file < 7 && square + 9 < 64 && (pawns & bit(square + 9)) != 0) {
            return true;
        }
    }

    if ((knightAttacks(square) & pieces(byColor, PieceType::Knight)) != 0) {
        return true;
    }

    if ((kingAttacks(square) & pieces(byColor, PieceType::King)) != 0) {
        return true;
    }

    static constexpr std::array<std::array<int, 2>, 4> bishopDirections{{
        {{1, 1}}, {{-1, 1}}, {{1, -1}}, {{-1, -1}},
    }};
    static constexpr std::array<std::array<int, 2>, 4> rookDirections{{
        {{1, 0}}, {{-1, 0}}, {{0, 1}}, {{0, -1}},
    }};

    for (const auto& direction : bishopDirections) {
        Square current = square;
        while (true) {
            current = step(current, direction[0], direction[1]);
            if (current == NoSquare) {
                break;
            }
            const Piece piece = pieceAt(current);
            if (piece.isEmpty()) {
                continue;
            }
            if (piece.color == byColor &&
                (piece.type == PieceType::Bishop || piece.type == PieceType::Queen)) {
                return true;
            }
            break;
        }
    }

    for (const auto& direction : rookDirections) {
        Square current = square;
        while (true) {
            current = step(current, direction[0], direction[1]);
            if (current == NoSquare) {
                break;
            }
            const Piece piece = pieceAt(current);
            if (piece.isEmpty()) {
                continue;
            }
            if (piece.color == byColor &&
                (piece.type == PieceType::Rook || piece.type == PieceType::Queen)) {
                return true;
            }
            break;
        }
    }

    return false;
}

bool Board::isKingInCheck(Color color) const {
    const Square king = kingSquare(color);
    return king != NoSquare && isSquareAttacked(king, opposite(color));
}

bool Board::hasInsufficientMaterial() const {
    for (Color color : {Color::White, Color::Black}) {
        if (pieces(color, PieceType::Pawn) != 0 ||
            pieces(color, PieceType::Rook) != 0 ||
            pieces(color, PieceType::Queen) != 0) {
            return false;
        }
    }

    const int minorPieces =
        popCount(pieces(Color::White, PieceType::Bishop)) +
        popCount(pieces(Color::White, PieceType::Knight)) +
        popCount(pieces(Color::Black, PieceType::Bishop)) +
        popCount(pieces(Color::Black, PieceType::Knight));

    return minorPieces <= 1;
}

std::string Board::ascii() const {
    std::ostringstream output;
    for (int rank = 7; rank >= 0; --rank) {
        output << (rank + 1) << "  ";
        for (int file = 0; file < 8; ++file) {
            const Piece piece = pieceAt(rank * 8 + file);
            output << (piece.isEmpty() ? '.' : pieceToFen(piece.color, piece.type)) << ' ';
        }
        output << '\n';
    }
    output << "\n   a b c d e f g h\n";
    return output.str();
}

std::string Board::unicode() const {
    constexpr char Separator[] = "  +---+---+---+---+---+---+---+---+\n";

    std::ostringstream output;
    output << Separator;
    for (int rank = 7; rank >= 0; --rank) {
        output << (rank + 1) << " |";
        for (int file = 0; file < 8; ++file) {
            output << ' ' << pieceToUnicode(pieceAt(rank * 8 + file)) << " |";
        }
        output << '\n' << Separator;
    }
    output << "    a   b   c   d   e   f   g   h\n";
    return output.str();
}

void Board::addPiece(Color color, PieceType type, Square square) {
    if (type != PieceType::None && validSquare(square)) {
        pieces_[colorIndex(color)][pieceIndex(type)] |= bit(square);
    }
}

void Board::removePiece(Color color, PieceType type, Square square) {
    if (type != PieceType::None && validSquare(square)) {
        pieces_[colorIndex(color)][pieceIndex(type)] &= ~bit(square);
    }
}

void Board::removePieceAt(Square square) {
    const Piece piece = pieceAt(square);
    if (!piece.isEmpty()) {
        removePiece(piece.color, piece.type, square);
    }
}

Square Board::kingSquare(Color color) const {
    Bitboard king = pieces(color, PieceType::King);
    if (king == 0) {
        return NoSquare;
    }
    return popLsb(king);
}

ChessGame ChessGame::standard() {
    return ChessGame(Board::standard());
}

std::optional<ChessGame> ChessGame::fromFen(std::string_view fen, std::string* error) {
    auto board = Board::fromFen(fen, error);
    if (!board) {
        return std::nullopt;
    }
    return ChessGame(*board);
}

MoveResult ChessGame::playAlgebraic(std::string_view notation) {
    const GameStatus currentStatus = status();
    if (currentStatus == GameStatus::Checkmate ||
        currentStatus == GameStatus::Stalemate ||
        currentStatus == GameStatus::FiftyMoveDraw ||
        currentStatus == GameStatus::InsufficientMaterialDraw) {
        return MoveResult{false, MoveError::GameOver, {}, "", currentStatus};
    }

    const auto pattern = parseAlgebraicPattern(notation);
    if (!pattern) {
        return MoveResult{false, MoveError::InvalidNotation, {}, "", currentStatus};
    }

    const std::vector<Move> legal = board_.generateLegalMoves();
    std::vector<Move> matches;
    for (const Move& move : legal) {
        if (matchesPattern(board_, move, *pattern)) {
            matches.push_back(move);
        }
    }

    if (matches.empty()) {
        return MoveResult{false, MoveError::IllegalMove, {}, "", currentStatus};
    }
    if (matches.size() > 1) {
        return MoveResult{false, MoveError::AmbiguousNotation, {}, "", currentStatus};
    }

    const Move move = matches.front();
    const std::string algebraic = formatAlgebraic(board_, move);
    const UndoState undo = board_.makeMove(move);
    moveHistory_.push_back(move);
    undoHistory_.push_back(undo);

    return MoveResult{true, MoveError::None, move, algebraic, status()};
}

MoveResult ChessGame::playMove(const Move& move) {
    const GameStatus currentStatus = status();
    if (currentStatus == GameStatus::Checkmate ||
        currentStatus == GameStatus::Stalemate ||
        currentStatus == GameStatus::FiftyMoveDraw ||
        currentStatus == GameStatus::InsufficientMaterialDraw) {
        return MoveResult{false, MoveError::GameOver, {}, "", currentStatus};
    }

    const std::vector<Move> legal = board_.generateLegalMoves();
    const auto found = std::find(legal.begin(), legal.end(), move);
    if (found == legal.end()) {
        return MoveResult{false, MoveError::IllegalMove, move, "", currentStatus};
    }

    const std::string algebraic = formatAlgebraic(board_, *found);
    const UndoState undo = board_.makeMove(*found);
    moveHistory_.push_back(*found);
    undoHistory_.push_back(undo);

    return MoveResult{true, MoveError::None, *found, algebraic, status()};
}

std::vector<std::string> ChessGame::legalMovesAlgebraic() const {
    const std::vector<Move> legal = board_.generateLegalMoves();
    std::vector<std::string> formatted;
    formatted.reserve(legal.size());
    for (const Move& move : legal) {
        formatted.push_back(formatAlgebraic(board_, move));
    }
    std::sort(formatted.begin(), formatted.end());
    return formatted;
}

std::size_t ChessGame::moveCount() const {
    return moveHistory_.size();
}

std::optional<Move> ChessGame::lastMove() const {
    if (moveHistory_.empty()) {
        return std::nullopt;
    }
    return moveHistory_.back();
}

Board ChessGame::boardAtPly(std::size_t ply) const {
    ply = std::min(ply, moveHistory_.size());

    Board snapshot = board_;
    for (std::size_t index = moveHistory_.size(); index > ply; --index) {
        snapshot.unmakeMove(moveHistory_[index - 1], undoHistory_[index - 1]);
    }

    return snapshot;
}

ChessGame ChessGame::snapshotAtPly(std::size_t ply) const {
    ply = std::min(ply, moveHistory_.size());

    ChessGame snapshot(boardAtPly(ply));
    snapshot.moveHistory_.assign(moveHistory_.begin(), moveHistory_.begin() + ply);
    snapshot.undoHistory_.assign(undoHistory_.begin(), undoHistory_.begin() + ply);
    return snapshot;
}

GameStatus ChessGame::status() const {
    if (board_.halfmoveClock() >= 100) {
        return GameStatus::FiftyMoveDraw;
    }
    if (board_.hasInsufficientMaterial()) {
        return GameStatus::InsufficientMaterialDraw;
    }

    const bool inCheck = board_.isKingInCheck(board_.sideToMove());
    const bool hasMoves = !board_.generateLegalMoves().empty();
    if (!hasMoves && inCheck) {
        return GameStatus::Checkmate;
    }
    if (!hasMoves) {
        return GameStatus::Stalemate;
    }
    return inCheck ? GameStatus::Check : GameStatus::Ongoing;
}

Color ChessGame::sideToMove() const {
    return board_.sideToMove();
}

const Board& ChessGame::board() const {
    return board_;
}

CapturedMaterial ChessGame::capturedMaterial() const {
    CapturedMaterial material;

    // Public game play never rewinds history, so captured material can be
    // derived from the stored undo records instead of duplicating counters.
    for (const UndoState& undo : undoHistory_) {
        if (undo.captured.isEmpty()) {
            continue;
        }

        auto& counts = undo.captured.color == Color::Black
            ? material.byWhite
            : material.byBlack;
        counts[static_cast<int>(undo.captured.type)] += 1;
    }

    return material;
}

std::string ChessGame::fen() const {
    return board_.toFen();
}

std::string ChessGame::ascii() const {
    return board_.ascii();
}

std::string ChessGame::unicodeBoard() const {
    return board_.unicode();
}

ChessGame::ChessGame(Board board)
    : board_(board) {}

std::string formatAlgebraic(const Board& board, const Move& move) {
    if (move.flag == MoveFlag::KingCastle) {
        return "O-O" + checkSuffixAfter(board, move);
    }
    if (move.flag == MoveFlag::QueenCastle) {
        return "O-O-O" + checkSuffixAfter(board, move);
    }

    const Piece moved = board.pieceAt(move.from);
    if (moved.isEmpty()) {
        return "";
    }

    std::string notation;
    if (moved.type != PieceType::Pawn) {
        notation.push_back(pieceLetter(moved.type));

        bool needsDisambiguation = false;
        bool sameFile = false;
        bool sameRank = false;
        for (const Move& other : board.generateLegalMoves()) {
            if (other.from == move.from || other.to != move.to) {
                continue;
            }
            const Piece otherPiece = board.pieceAt(other.from);
            if (otherPiece.color == moved.color && otherPiece.type == moved.type) {
                needsDisambiguation = true;
                sameFile = sameFile || fileOf(other.from) == fileOf(move.from);
                sameRank = sameRank || rankOf(other.from) == rankOf(move.from);
            }
        }

        if (needsDisambiguation) {
            if (!sameFile) {
                notation.push_back(static_cast<char>('a' + fileOf(move.from)));
            } else if (!sameRank) {
                notation.push_back(static_cast<char>('1' + rankOf(move.from)));
            } else {
                notation += squareName(move.from);
            }
        }
    } else if (move.isCapture()) {
        notation.push_back(static_cast<char>('a' + fileOf(move.from)));
    }

    if (move.isCapture()) {
        notation.push_back('x');
    }

    notation += squareName(move.to);

    if (move.isPromotion()) {
        notation.push_back('=');
        notation.push_back(pieceLetter(move.promotion));
    }

    notation += checkSuffixAfter(board, move);
    return notation;
}

} // namespace chess
