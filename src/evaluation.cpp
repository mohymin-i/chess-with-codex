#include "evaluation.h"

#include <array>
#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace chess {
namespace {

constexpr int DefaultMateSearchNodeLimit = 120'000;

int colorIndex(Color color) {
    return static_cast<int>(color);
}

int fileOf(Square square) {
    return square & 7;
}

int rankOf(Square square) {
    return square >> 3;
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

int advancement(Color color, Square square) {
    return color == Color::White ? rankOf(square) : 7 - rankOf(square);
}

Bitboard bit(Square square) {
    return 1ULL << square;
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

    Bitboard attacks = 0;
    for (const auto& offset : offsets) {
        const Square target = step(square, offset[0], offset[1]);
        if (target != NoSquare) {
            attacks |= bit(target);
        }
    }
    return attacks;
}

Bitboard slidingAttacks(const Board& board,
                        Square square,
                        const std::array<std::array<int, 2>, 4>& directions) {
    Bitboard attacks = 0;
    const Bitboard occupied = board.occupancy();
    for (const auto& direction : directions) {
        Square target = square;
        while (true) {
            target = step(target, direction[0], direction[1]);
            if (target == NoSquare) {
                break;
            }
            attacks |= bit(target);
            if ((occupied & bit(target)) != 0) {
                break;
            }
        }
    }
    return attacks;
}

static constexpr std::array<std::array<int, 2>, 4> BishopDirections{{
    {{1, 1}}, {{-1, 1}}, {{1, -1}}, {{-1, -1}},
}};

static constexpr std::array<std::array<int, 2>, 4> RookDirections{{
    {{1, 0}}, {{-1, 0}}, {{0, 1}}, {{0, -1}},
}};

struct PawnInfo {
    std::array<std::array<int, 8>, 2> fileCounts{};
    std::array<int, 2> nonPawnMaterial{};
};

PawnInfo collectPawnInfo(const Board& board) {
    PawnInfo info;
    for (Color color : {Color::White, Color::Black}) {
        Bitboard pawns = board.pieces(color, PieceType::Pawn);
        while (pawns != 0) {
            ++info.fileCounts[colorIndex(color)][fileOf(popLsb(pawns))];
        }

        info.nonPawnMaterial[colorIndex(color)] +=
            popCount(board.pieces(color, PieceType::Knight)) * pieceValue(PieceType::Knight);
        info.nonPawnMaterial[colorIndex(color)] +=
            popCount(board.pieces(color, PieceType::Bishop)) * pieceValue(PieceType::Bishop);
        info.nonPawnMaterial[colorIndex(color)] +=
            popCount(board.pieces(color, PieceType::Rook)) * pieceValue(PieceType::Rook);
        info.nonPawnMaterial[colorIndex(color)] +=
            popCount(board.pieces(color, PieceType::Queen)) * pieceValue(PieceType::Queen);
    }
    return info;
}

bool fileHasPawn(const PawnInfo& info, Color color, int file) {
    return file >= 0 && file < 8 && info.fileCounts[colorIndex(color)][file] > 0;
}

bool hasAdjacentPawnFile(const PawnInfo& info, Color color, int file) {
    return fileHasPawn(info, color, file - 1) || fileHasPawn(info, color, file + 1);
}

bool isEndgame(const PawnInfo& info) {
    return info.nonPawnMaterial[colorIndex(Color::White)] +
           info.nonPawnMaterial[colorIndex(Color::Black)] <= 2400;
}

int pawnStructureScore(const PawnInfo& info, const Board& board, Color color, Square square) {
    const int file = fileOf(square);
    const int adv = advancement(color, square);
    int score = adv * 12 + (4 - std::abs(file - 3)) * 3;

    if (info.fileCounts[colorIndex(color)][file] > 1) {
        score -= 13;
    }
    if (!hasAdjacentPawnFile(info, color, file)) {
        score -= 12;
    } else {
        score += 6;
    }

    bool passed = true;
    const Color enemy = opposite(color);
    Bitboard enemyPawns = board.pieces(enemy, PieceType::Pawn);
    while (enemyPawns != 0) {
        const Square enemyPawn = popLsb(enemyPawns);
        if (std::abs(fileOf(enemyPawn) - file) > 1) {
            continue;
        }
        if ((color == Color::White && rankOf(enemyPawn) > rankOf(square)) ||
            (color == Color::Black && rankOf(enemyPawn) < rankOf(square))) {
            passed = false;
            break;
        }
    }
    if (passed) {
        score += 18 + (adv * adv * 4);
    }

    return score;
}

int pieceSquareScore(const PawnInfo& info, PieceType type, Color color, Square square, bool endgame) {
    switch (type) {
    case PieceType::Knight:
        return centerBonus(square) * 12 - (advancement(color, square) < 2 ? 8 : 0);
    case PieceType::Bishop:
        return centerBonus(square) * 7 + (advancement(color, square) >= 2 ? 6 : 0);
    case PieceType::Rook:
        if (!fileHasPawn(info, color, fileOf(square)) &&
            !fileHasPawn(info, opposite(color), fileOf(square))) {
            return 28;
        }
        return !fileHasPawn(info, color, fileOf(square)) ? 14 : 0;
    case PieceType::Queen:
        return centerBonus(square) * 3;
    case PieceType::King:
        if (endgame) {
            return centerBonus(square) * 14;
        }
        if ((color == Color::White && (square == 6 || square == 2)) ||
            (color == Color::Black && (square == 62 || square == 58))) {
            return 45;
        }
        return -centerBonus(square) * 9;
    case PieceType::Pawn:
    case PieceType::None:
        return 0;
    }
    return 0;
}

int mobilityScore(const Board& board, Color color, PieceType type, Square square) {
    const Bitboard own = board.occupancy(color);
    Bitboard attacks = 0;
    switch (type) {
    case PieceType::Knight:
        attacks = knightAttacks(square);
        return popCount(attacks & ~own) * 4;
    case PieceType::Bishop:
        attacks = slidingAttacks(board, square, BishopDirections);
        return popCount(attacks & ~own) * 3;
    case PieceType::Rook:
        attacks = slidingAttacks(board, square, RookDirections);
        return popCount(attacks & ~own) * 2;
    case PieceType::Queen:
        attacks = slidingAttacks(board, square, BishopDirections) |
                  slidingAttacks(board, square, RookDirections);
        return popCount(attacks & ~own);
    case PieceType::King:
    case PieceType::Pawn:
    case PieceType::None:
        return 0;
    }
    return 0;
}

int kingSafetyScore(const PawnInfo& info, const Board& board, Color color, bool endgame) {
    if (endgame) {
        return 0;
    }

    Bitboard kings = board.pieces(color, PieceType::King);
    if (kings == 0) {
        return 0;
    }

    const Square king = popLsb(kings);
    const int file = fileOf(king);
    const int shieldRank = color == Color::White ? 1 : 6;
    int score = 0;

    for (int candidateFile = std::max(0, file - 1); candidateFile <= std::min(7, file + 1); ++candidateFile) {
        const Square shieldSquare = shieldRank * 8 + candidateFile;
        const Piece shield = board.pieceAt(shieldSquare);
        if (!shield.isEmpty() && shield.color == color && shield.type == PieceType::Pawn) {
            score += 12;
        } else {
            score -= 8;
        }
        if (!fileHasPawn(info, color, candidateFile)) {
            score -= 7;
        }
    }

    if ((color == Color::White && (king == 6 || king == 2)) ||
        (color == Color::Black && (king == 62 || king == 58))) {
        score += 18;
    }

    return score;
}

int mateMoveScore(const Board& board, const Move& move) {
    Board copy = board;
    copy.makeMove(move);
    if (copy.isKingInCheck(copy.sideToMove()) && copy.generateLegalMoves().empty()) {
        return 1'000'000;
    }

    int score = 0;
    if (copy.isKingInCheck(copy.sideToMove())) {
        score += 100'000;
    }
    if (move.isPromotion()) {
        score += 20'000 + pieceValue(move.promotion);
    }
    if (move.isCapture()) {
        const Piece captured = move.flag == MoveFlag::EnPassant
                                   ? board.pieceAt(board.sideToMove() == Color::White ? move.to - 8 : move.to + 8)
                                   : board.pieceAt(move.to);
        score += 10'000 + pieceValue(captured.type);
    }
    score += centerBonus(move.to);
    return score;
}

void orderMateMoves(const Board& board, std::vector<Move>& moves) {
    std::vector<std::pair<int, Move>> scored;
    scored.reserve(moves.size());
    for (const Move& move : moves) {
        scored.emplace_back(mateMoveScore(board, move), move);
    }

    std::stable_sort(
        scored.begin(),
        scored.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.first > rhs.first;
        }
    );

    for (std::size_t i = 0; i < scored.size(); ++i) {
        moves[i] = scored[i].second;
    }
}

struct MateSearchContext {
    int maxNodes = DefaultMateSearchNodeLimit;
    int nodes = 0;
    bool stopped = false;
    std::unordered_map<std::string, int> memo;
};

std::string mateMemoKey(const Board& board, Color winner, int remainingPlies) {
    return board.toFen() + "|" + std::to_string(static_cast<int>(winner)) + "|" + std::to_string(remainingPlies);
}

std::optional<int> forcedMatePlies(Board& board,
                                   Color winner,
                                   int remainingPlies,
                                   MateSearchContext& context) {
    if (++context.nodes > context.maxNodes) {
        context.stopped = true;
        return std::nullopt;
    }

    std::vector<Move> legalMoves = board.generateLegalMoves();
    if (legalMoves.empty()) {
        if (board.isKingInCheck(board.sideToMove()) && opposite(board.sideToMove()) == winner) {
            return 0;
        }
        return std::nullopt;
    }

    if (remainingPlies == 0 || board.hasInsufficientMaterial() || board.halfmoveClock() >= 100) {
        return std::nullopt;
    }

    const std::string key = mateMemoKey(board, winner, remainingPlies);
    if (const auto found = context.memo.find(key); found != context.memo.end()) {
        return found->second < 0 ? std::optional<int>{} : std::optional<int>{found->second};
    }

    orderMateMoves(board, legalMoves);

    if (board.sideToMove() == winner) {
        std::optional<int> shortest;
        for (const Move& move : legalMoves) {
            const UndoState undo = board.makeMove(move);
            const std::optional<int> child = forcedMatePlies(board, winner, remainingPlies - 1, context);
            board.unmakeMove(move, undo);

            if (context.stopped) {
                return std::nullopt;
            }
            if (!child) {
                continue;
            }

            const int distance = *child + 1;
            if (!shortest || distance < *shortest) {
                shortest = distance;
                if (distance == 1) {
                    break;
                }
            }
        }

        context.memo.emplace(key, shortest.value_or(-1));
        return shortest;
    }

    int longestDefense = 0;
    for (const Move& move : legalMoves) {
        const UndoState undo = board.makeMove(move);
        const std::optional<int> child = forcedMatePlies(board, winner, remainingPlies - 1, context);
        board.unmakeMove(move, undo);

        if (context.stopped) {
            return std::nullopt;
        }
        if (!child) {
            context.memo.emplace(key, -1);
            return std::nullopt;
        }

        longestDefense = std::max(longestDefense, *child + 1);
    }

    context.memo.emplace(key, longestDefense);
    return longestDefense;
}

int winningMovesForPlies(int plies, Color winner, Color rootSideToMove) {
    if (plies <= 0) {
        return 0;
    }
    return rootSideToMove == winner ? (plies + 1) / 2 : plies / 2;
}

} // namespace

int pieceValue(PieceType type) {
    switch (type) {
    case PieceType::Pawn:
        return 100;
    case PieceType::Knight:
        return 320;
    case PieceType::Bishop:
        return 330;
    case PieceType::Rook:
        return 500;
    case PieceType::Queen:
        return 900;
    case PieceType::King:
        return 20000;
    case PieceType::None:
        return 0;
    }
    return 0;
}

int centerBonus(Square square) {
    const int fileDistance = std::abs(fileOf(square) - 3) + std::abs(fileOf(square) - 4);
    const int rankDistance = std::abs(rankOf(square) - 3) + std::abs(rankOf(square) - 4);
    return 14 - fileDistance - rankDistance;
}

int evaluateBoard(const Board& board, Color perspective) {
    if (board.hasInsufficientMaterial() || board.halfmoveClock() >= 100) {
        return 0;
    }

    const PawnInfo pawnInfo = collectPawnInfo(board);
    const bool endgame = isEndgame(pawnInfo);
    int score[2] = {0, 0};

    for (Color color : {Color::White, Color::Black}) {
        for (PieceType type : {
                 PieceType::Pawn,
                 PieceType::Knight,
                 PieceType::Bishop,
                 PieceType::Rook,
                 PieceType::Queen,
                 PieceType::King,
             }) {
            Bitboard pieces = board.pieces(color, type);
            while (pieces != 0) {
                const Square square = popLsb(pieces);
                score[colorIndex(color)] += pieceValue(type);
                if (type == PieceType::Pawn) {
                    score[colorIndex(color)] += pawnStructureScore(pawnInfo, board, color, square);
                } else {
                    score[colorIndex(color)] += pieceSquareScore(pawnInfo, type, color, square, endgame);
                    score[colorIndex(color)] += mobilityScore(board, color, type, square);
                }
            }
        }

        if (popCount(board.pieces(color, PieceType::Bishop)) >= 2) {
            score[colorIndex(color)] += 35;
        }
        score[colorIndex(color)] += kingSafetyScore(pawnInfo, board, color, endgame);
    }

    return score[colorIndex(perspective)] - score[colorIndex(opposite(perspective))];
}

std::optional<ForcedMate> findForcedMate(const Board& board, int maxMateSearchPly) {
    if (maxMateSearchPly <= 0 || board.hasInsufficientMaterial() || board.halfmoveClock() >= 100) {
        return std::nullopt;
    }

    std::optional<ForcedMate> best;
    for (Color winner : {board.sideToMove(), opposite(board.sideToMove())}) {
        Board copy = board;
        MateSearchContext context;
        const std::optional<int> plies = forcedMatePlies(copy, winner, maxMateSearchPly, context);
        if (!plies) {
            continue;
        }

        ForcedMate mate{
            winner,
            *plies,
            winningMovesForPlies(*plies, winner, board.sideToMove()),
        };
        if (!best || mate.moves < best->moves || (mate.moves == best->moves && mate.plies < best->plies)) {
            best = mate;
        }
    }

    return best;
}

Evaluation evaluatePosition(const Board& board, Color perspective, int maxMateSearchPly) {
    Evaluation evaluation;
    evaluation.centipawns = evaluateBoard(board, perspective);
    evaluation.forcedMate = findForcedMate(board, maxMateSearchPly);
    return evaluation;
}

} // namespace chess
