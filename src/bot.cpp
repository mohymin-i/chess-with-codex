#include "bot.h"

#include "evaluation.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace chess {
namespace {

using Clock = std::chrono::steady_clock;

constexpr int Infinity = 1'000'000'000;
constexpr int MateScore = 100'000'000;
constexpr int GaryMaxDepth = 10;
constexpr auto GaryTimeLimit = std::chrono::milliseconds(9500);
constexpr int GaryJrMaxDepth = 10;
constexpr auto GaryJrTimeLimit = std::chrono::milliseconds(1200);

struct MoveTactics {
    bool givesCheck = false;
    bool givesMate = false;
};

Piece capturedPiece(const Board& board, const Move& move) {
    if (!move.isCapture()) {
        return {};
    }

    if (move.flag == MoveFlag::EnPassant) {
        const Square capturedSquare = board.sideToMove() == Color::White ? move.to - 8 : move.to + 8;
        return board.pieceAt(capturedSquare);
    }

    return board.pieceAt(move.to);
}

MoveTactics tacticsAfterMove(const Board& afterMove) {
    const bool givesCheck = afterMove.isKingInCheck(afterMove.sideToMove());
    return {givesCheck, givesCheck && afterMove.generateLegalMoves().empty()};
}

MoveTactics moveTactics(const Board& board, const Move& move) {
    Board copy = board;
    copy.makeMove(move);
    return tacticsAfterMove(copy);
}

int quickMoveScore(const Board& board, const Move& move, const MoveTactics& tactics) {
    if (tactics.givesMate) {
        return MateScore;
    }

    int score = centerBonus(move.to);

    const Piece captured = capturedPiece(board, move);
    if (!captured.isEmpty()) {
        const Piece moved = board.pieceAt(move.from);
        score += 10 * pieceValue(captured.type) - pieceValue(moved.type);
    }

    if (move.isPromotion()) {
        score += pieceValue(move.promotion);
    }

    if (tactics.givesCheck) {
        score += 50;
    }

    if (move.isCastle()) {
        score += 30;
    }

    return score;
}

int quickMoveScore(const Board& board, const Move& move) {
    return quickMoveScore(board, move, moveTactics(board, move));
}

int searchMoveScore(const Board& board, const Move& move) {
    int score = centerBonus(move.to);

    const Piece captured = capturedPiece(board, move);
    if (!captured.isEmpty()) {
        const Piece moved = board.pieceAt(move.from);
        score += 100'000 + 10 * pieceValue(captured.type) - pieceValue(moved.type);
    }

    if (move.isPromotion()) {
        score += 80'000 + pieceValue(move.promotion);
    }

    if (move.isCastle()) {
        score += 800;
    }

    return score;
}

// Good move ordering matters more than raw depth in this small engine. Captures
// and promotions are searched first so alpha-beta can cut weaker branches early.
void orderMoves(const Board& board, std::vector<Move>& moves) {
    std::stable_sort(
        moves.begin(),
        moves.end(),
        [&](const Move& lhs, const Move& rhs) {
            return searchMoveScore(board, lhs) > searchMoveScore(board, rhs);
        }
    );
}

struct SearchState {
    Clock::time_point deadline;
    bool timedOut = false;
    int nodes = 0;
};

struct SearchResult {
    Move bestMove;
    bool timedOut = false;
};

struct SearchConfig {
    bool allowConclusiveEarlyExit = false;
    int quiescenceDepth = 4;
    int conclusiveMinDepth = 4;
    int winningScore = 2000;
    int winningMargin = 450;
    int advantageScore = 900;
    int advantageMargin = 700;
    int flatMargin = 1200;
    bool lateMoveReductions = false;
    int lateMoveReductionDepth = 3;
    int lateMoveReductionMoveIndex = 4;
    int lateMoveReductionAmount = 1;
    bool futilityPruning = false;
    int futilityDepth = 1;
    int futilityMargin = 180;
    bool rootMovePruning = false;
    int rootPruneAfterDepth = 2;
    std::size_t rootMinMoves = 6;
    std::size_t rootMaxMoves = 10;
    int rootScoreMargin = 350;
};

struct ScoredMove {
    Move move;
    int score = -Infinity;
};

SearchConfig searchConfig(bool allowConclusiveEarlyExit) {
    SearchConfig config;
    config.allowConclusiveEarlyExit = allowConclusiveEarlyExit;
    return config;
}

SearchConfig garyJrSearchConfig() {
    SearchConfig config = searchConfig(true);
    config.quiescenceDepth = 2;
    config.conclusiveMinDepth = 2;
    config.winningScore = 1200;
    config.winningMargin = 250;
    config.advantageScore = 350;
    config.advantageMargin = 300;
    config.flatMargin = 500;
    config.lateMoveReductions = true;
    config.lateMoveReductionMoveIndex = 3;
    config.lateMoveReductionAmount = 2;
    config.futilityPruning = true;
    config.futilityDepth = 2;
    config.futilityMargin = 220;
    config.rootMovePruning = true;
    config.rootMinMoves = 4;
    config.rootMaxMoves = 8;
    config.rootScoreMargin = 250;
    return config;
}

bool timeExpired(SearchState& state) {
    ++state.nodes;
    if ((state.nodes & 255) != 0) {
        return false;
    }

    if (Clock::now() >= state.deadline) {
        state.timedOut = true;
        return true;
    }
    return false;
}

int quiesce(Board& board,
            int alpha,
            int beta,
            int depth,
            SearchState& state,
            const SearchConfig& config) {
    if (timeExpired(state)) {
        return evaluateBoard(board, board.sideToMove());
    }

    const int standPat = evaluateBoard(board, board.sideToMove());
    if (standPat >= beta) {
        return beta;
    }
    if (standPat > alpha) {
        alpha = standPat;
    }
    if (depth == 0) {
        return alpha;
    }

    std::vector<Move> moves = board.generateLegalMoves();
    moves.erase(
        std::remove_if(
            moves.begin(),
            moves.end(),
            [](const Move& move) {
                return !move.isCapture() && !move.isPromotion();
            }
        ),
        moves.end()
    );
    orderMoves(board, moves);

    for (const Move& move : moves) {
        const UndoState undo = board.makeMove(move);
        const int score = -quiesce(board, -beta, -alpha, depth - 1, state, config);
        board.unmakeMove(move, undo);

        if (state.timedOut) {
            return alpha;
        }
        if (score >= beta) {
            return beta;
        }
        if (score > alpha) {
            alpha = score;
        }
    }

    return alpha;
}

bool isQuietReducibleMove(const Move& move) {
    return !move.isCapture() && !move.isPromotion() && !move.isCastle();
}

int negamax(Board& board,
            int depth,
            int alpha,
            int beta,
            int ply,
            SearchState& state,
            const SearchConfig& config) {
    if (timeExpired(state)) {
        return evaluateBoard(board, board.sideToMove());
    }

    if (board.hasInsufficientMaterial() || board.halfmoveClock() >= 100) {
        return 0;
    }

    std::vector<Move> legalMoves = board.generateLegalMoves();
    if (legalMoves.empty()) {
        return board.isKingInCheck(board.sideToMove()) ? -MateScore + ply : 0;
    }

    const bool inCheck = board.isKingInCheck(board.sideToMove());
    if (depth <= 0) {
        if (!inCheck) {
            return quiesce(board, alpha, beta, config.quiescenceDepth, state, config);
        }
        depth = 1;
    }

    orderMoves(board, legalMoves);

    const bool canUseFutility =
        config.futilityPruning && !inCheck && depth <= config.futilityDepth;
    const int staticEval = canUseFutility ? evaluateBoard(board, board.sideToMove()) : 0;
    int searchedMoves = 0;

    for (const Move& move : legalMoves) {
        const bool quietReducible = isQuietReducibleMove(move);
        if (canUseFutility && searchedMoves > 0 && quietReducible &&
            staticEval + config.futilityMargin * depth <= alpha) {
            ++searchedMoves;
            continue;
        }

        const UndoState undo = board.makeMove(move);
        const bool givesCheck = board.isKingInCheck(board.sideToMove());
        int childDepth = depth - 1;
        bool reduced = false;
        if (config.lateMoveReductions &&
            depth >= config.lateMoveReductionDepth &&
            searchedMoves >= config.lateMoveReductionMoveIndex &&
            quietReducible &&
            !inCheck &&
            !givesCheck) {
            childDepth = std::max(0, childDepth - config.lateMoveReductionAmount);
            reduced = childDepth < depth - 1;
        }

        int score = -negamax(board, childDepth, -beta, -alpha, ply + 1, state, config);
        if (reduced && !state.timedOut && score > alpha) {
            score = -negamax(board, depth - 1, -beta, -alpha, ply + 1, state, config);
        }
        board.unmakeMove(move, undo);

        if (state.timedOut) {
            return alpha;
        }
        ++searchedMoves;
        if (score >= beta) {
            return beta;
        }
        if (score > alpha) {
            alpha = score;
        }
    }

    return alpha;
}

int levelSearchDepth(int level) {
    switch (level) {
    case 4:
        return 2;
    case 5:
        return 3;
    case 6:
        return 4;
    case 7:
        return 5;
    case 8:
        return 7;
    case 9:
        return 8;
    default:
        return 1;
    }
}

std::chrono::milliseconds levelSearchLimit(int level) {
    switch (level) {
    case 4:
        return std::chrono::milliseconds(150);
    case 5:
        return std::chrono::milliseconds(350);
    case 6:
        return std::chrono::milliseconds(900);
    case 7:
        return std::chrono::milliseconds(2500);
    case 8:
        return std::chrono::milliseconds(5500);
    case 9:
        return std::chrono::milliseconds(8500);
    default:
        return std::chrono::milliseconds(75);
    }
}

bool scoreLooksConclusive(int bestScore,
                          int secondBestScore,
                          int depth,
                          const SearchConfig& config) {
    if (depth < config.conclusiveMinDepth || secondBestScore == -Infinity) {
        return false;
    }
    if (bestScore >= MateScore - 1000) {
        return true;
    }

    const int margin = bestScore - secondBestScore;
    if (bestScore >= config.winningScore && margin >= config.winningMargin) {
        return true;
    }
    if (bestScore >= config.advantageScore && margin >= config.advantageMargin) {
        return true;
    }
    return margin >= config.flatMargin;
}

bool moveSortsBefore(const Move& lhs, const Move& rhs) {
    if (lhs.from != rhs.from) {
        return lhs.from < rhs.from;
    }
    if (lhs.to != rhs.to) {
        return lhs.to < rhs.to;
    }
    if (lhs.promotion != rhs.promotion) {
        return static_cast<int>(lhs.promotion) < static_cast<int>(rhs.promotion);
    }
    return static_cast<int>(lhs.flag) < static_cast<int>(rhs.flag);
}

template <typename ScoreMove>
Move chooseHighestScoredMove(const std::vector<Move>& moves,
                             ScoreMove scoreMove,
                             bool breakTiesByMove = false) {
    Move bestMove = moves.front();
    int bestScore = scoreMove(bestMove);

    for (std::size_t i = 1; i < moves.size(); ++i) {
        const Move& move = moves[i];
        const int score = scoreMove(move);
        if (score > bestScore ||
            (breakTiesByMove && score == bestScore && moveSortsBefore(move, bestMove))) {
            bestMove = move;
            bestScore = score;
        }
    }

    return bestMove;
}

void promoteBestMove(const Board& board, std::vector<Move>& moves, const Move& bestMove) {
    std::stable_sort(
        moves.begin(),
        moves.end(),
        [&](const Move& lhs, const Move& rhs) {
            const bool lhsIsBest = lhs == bestMove;
            const bool rhsIsBest = rhs == bestMove;
            if (lhsIsBest != rhsIsBest) {
                return lhsIsBest;
            }
            if (lhsIsBest && rhsIsBest) {
                return false;
            }

            const int lhsScore = searchMoveScore(board, lhs);
            const int rhsScore = searchMoveScore(board, rhs);
            if (lhsScore != rhsScore) {
                return lhsScore > rhsScore;
            }
            return moveSortsBefore(lhs, rhs);
        }
    );
}

SearchResult searchBestMove(const Board& board,
                            std::vector<Move> legalMoves,
                            int maxDepth,
                            std::chrono::milliseconds timeLimit,
                            const SearchConfig& config) {
    if (legalMoves.empty()) {
        return {};
    }

    orderMoves(board, legalMoves);

    if (legalMoves.size() == 1) {
        return SearchResult{legalMoves.front()};
    }

    SearchResult result{legalMoves.front()};
    SearchState state{Clock::now() + timeLimit};

    for (int depth = 1; depth <= maxDepth; ++depth) {
        Move depthBestMove = result.bestMove;
        int depthBestScore = -Infinity;
        int depthSecondBestScore = -Infinity;
        bool completedDepth = true;
        std::vector<ScoredMove> depthScores;
        depthScores.reserve(legalMoves.size());

        for (const Move& move : legalMoves) {
            Board copy = board;
            copy.makeMove(move);
            const int score = -negamax(copy, depth - 1, -Infinity, Infinity, 1, state, config);

            if (state.timedOut) {
                completedDepth = false;
                break;
            }

            depthScores.push_back({move, score});
            if (score > depthBestScore) {
                depthSecondBestScore = depthBestScore;
                depthBestScore = score;
                depthBestMove = move;
            } else if (score > depthSecondBestScore) {
                depthSecondBestScore = score;
            }
        }

        if (!completedDepth) {
            result.timedOut = true;
            break;
        }

        result.bestMove = depthBestMove;

        if (depthBestScore >= MateScore - 1000 ||
            (config.allowConclusiveEarlyExit &&
             scoreLooksConclusive(depthBestScore, depthSecondBestScore, depth, config))) {
            break;
        }

        if (config.rootMovePruning &&
            depth >= config.rootPruneAfterDepth &&
            depthScores.size() > config.rootMinMoves) {
            std::stable_sort(
                depthScores.begin(),
                depthScores.end(),
                [&](const ScoredMove& lhs, const ScoredMove& rhs) {
                    if (lhs.score != rhs.score) {
                        return lhs.score > rhs.score;
                    }
                    return searchMoveScore(board, lhs.move) > searchMoveScore(board, rhs.move);
                }
            );

            std::vector<Move> prunedMoves;
            prunedMoves.reserve(std::min(config.rootMaxMoves, depthScores.size()));
            const int keepScore = depthBestScore - config.rootScoreMargin;
            for (const ScoredMove& scoredMove : depthScores) {
                if (prunedMoves.size() < config.rootMinMoves ||
                    (scoredMove.score >= keepScore && prunedMoves.size() < config.rootMaxMoves)) {
                    prunedMoves.push_back(scoredMove.move);
                }
            }
            legalMoves = std::move(prunedMoves);
        } else {
            promoteBestMove(board, legalMoves, result.bestMove);
        }

        if (Clock::now() >= state.deadline) {
            break;
        }
    }

    return result;
}

std::optional<Move> chooseSearchMove(const Board& board,
                                     int maxDepth,
                                     std::chrono::milliseconds timeLimit,
                                     const SearchConfig& config) {
    std::vector<Move> legalMoves = board.generateLegalMoves();
    if (legalMoves.empty()) {
        return std::nullopt;
    }

    return searchBestMove(board, std::move(legalMoves), maxDepth, timeLimit, config).bestMove;
}

int levelTwoOrThreeMoveScore(const Board& board, const Move& move, int level) {
    const Color us = board.sideToMove();
    Board afterMove = board;
    afterMove.makeMove(move);

    const MoveTactics tactics = tacticsAfterMove(afterMove);
    if (tactics.givesMate) {
        return MateScore;
    }

    int score = quickMoveScore(board, move, tactics) + evaluateBoard(afterMove, us);
    if (level >= 3) {
        for (const Move& reply : afterMove.generateLegalMoves()) {
            if (moveTactics(afterMove, reply).givesMate) {
                score -= 200'000;
            } else {
                const Piece captured = capturedPiece(afterMove, reply);
                if (!captured.isEmpty()) {
                    score -= pieceValue(captured.type) / 2;
                }
            }
        }
    }

    return score;
}

} // namespace

std::string_view JohnCheckers::name() const {
    return "John Checkers";
}

std::string_view JohnCheckers::description() const {
    return "Easy. Deterministic one-ply heuristic that scores legal moves for mate, captures, promotions, checks, castling, and center control.";
}

std::optional<Move> JohnCheckers::chooseMove(const Board& board) const {
    const std::vector<Move> legalMoves = board.generateLegalMoves();
    if (legalMoves.empty()) {
        return std::nullopt;
    }

    return chooseHighestScoredMove(
        legalMoves,
        [&](const Move& move) {
            return quickMoveScore(board, move);
        });
}

LevelBot::LevelBot(int level)
    : level_(std::clamp(level, 2, 9)),
      name_("Level " + std::to_string(level_)) {
    switch (level_) {
    case 2:
        description_ = "Level 2. Simple material-and-position scorer with a static board check after each candidate move.";
        break;
    case 3:
        description_ = "Level 3. Adds basic blunder avoidance by checking for immediate mate and heavy capture replies.";
        break;
    case 4:
        description_ = "Level 4. Short fixed-depth alpha-beta search with quiescence on forcing captures.";
        break;
    case 5:
        description_ = "Level 5. Roughly 1300 Elo target: depth-limited alpha-beta with tactical captures and basic positional evaluation.";
        break;
    case 6:
        description_ = "Level 6. Searches several plies with quiescence and a larger move budget.";
        break;
    case 7:
        description_ = "Level 7. Roughly 2300 Elo target: deeper alpha-beta search with quiescence and a larger tactical budget.";
        break;
    case 8:
        description_ = "Level 8. Elite-human target: long alpha-beta search intended to be beatable only by the best human players.";
        break;
    case 9:
        description_ = "Level 9. Near-impossible target: very deep alpha-beta search with quiescence and early exit for clearly dominant moves.";
        break;
    default:
        description_ = "Numbered training bot.";
        break;
    }
}

int LevelBot::level() const {
    return level_;
}

std::string_view LevelBot::name() const {
    return name_;
}

std::string_view LevelBot::description() const {
    return description_;
}

std::optional<Move> LevelBot::chooseMove(const Board& board) const {
    const std::vector<Move> legalMoves = board.generateLegalMoves();
    if (legalMoves.empty()) {
        return std::nullopt;
    }

    if (level_ <= 3) {
        return chooseHighestScoredMove(
            legalMoves,
            [&](const Move& move) {
                return levelTwoOrThreeMoveScore(board, move, level_);
            },
            true);
    }

    return chooseSearchMove(board, levelSearchDepth(level_), levelSearchLimit(level_), searchConfig(level_ >= 8));
}

std::string_view GaryChess::name() const {
    return "Gary Chess";
}

std::string_view GaryChess::description() const {
    return "Level 10. Strongest bot: deepest iterative-deepening alpha-beta with quiescence, move ordering, and early exit for clearly dominant moves under a 9.5 second cap.";
}

std::optional<Move> GaryChess::chooseMove(const Board& board) const {
    return chooseSearchMove(board, GaryMaxDepth, GaryTimeLimit, searchConfig(true));
}

std::string_view GaryChessJr::name() const {
    return "Gary Chess Jr";
}

std::string_view GaryChessJr::description() const {
    return "Fast level-10 variant. Uses Gary Chess's evaluator and iterative alpha-beta engine with aggressive early exit, root pruning, late-move reductions, and a shorter 1.2 second cap.";
}

std::optional<Move> GaryChessJr::chooseMove(const Board& board) const {
    return chooseSearchMove(board, GaryJrMaxDepth, GaryJrTimeLimit, garyJrSearchConfig());
}

std::vector<std::unique_ptr<ChessBot>> createDefaultBots() {
    std::vector<std::unique_ptr<ChessBot>> bots;
    bots.reserve(11);
    bots.emplace_back(std::make_unique<JohnCheckers>());
    for (int level = 2; level <= 9; ++level) {
        bots.emplace_back(std::make_unique<LevelBot>(level));
    }
    bots.emplace_back(std::make_unique<GaryChessJr>());
    bots.emplace_back(std::make_unique<GaryChess>());
    return bots;
}

} // namespace chess
