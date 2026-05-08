#pragma once

#include "chess.h"

#include <optional>

namespace chess {

struct ForcedMate {
    Color winner = Color::White;
    int plies = 0;
    int moves = 0;
};

struct Evaluation {
    int centipawns = 0;
    std::optional<ForcedMate> forcedMate;
};

// Values are centipawns. Positive evaluations favor `perspective`, negative
// evaluations favor the opposing side.
int evaluateBoard(const Board& board, Color perspective);

Evaluation evaluatePosition(const Board& board, Color perspective, int maxMateSearchPly = 0);
std::optional<ForcedMate> findForcedMate(const Board& board, int maxMateSearchPly);

int pieceValue(PieceType type);
int centerBonus(Square square);

} // namespace chess
