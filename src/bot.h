#pragma once

#include "chess.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chess {

// Stable bot boundary: future bots only need a name and a legal-move chooser.
// The engine owns legality, so bots should never mutate the passed Board.
class ChessBot {
public:
    virtual ~ChessBot() = default;

    virtual std::string_view name() const = 0;
    virtual std::string_view description() const = 0;
    virtual std::optional<Move> chooseMove(const Board& board) const = 0;
};

class JohnCheckers final : public ChessBot {
public:
    std::string_view name() const override;
    std::string_view description() const override;
    std::optional<Move> chooseMove(const Board& board) const override;
};

class LevelBot final : public ChessBot {
public:
    explicit LevelBot(int level = 2);

    int level() const;
    std::string_view name() const override;
    std::string_view description() const override;
    std::optional<Move> chooseMove(const Board& board) const override;

private:
    int level_ = 2;
    std::string name_;
    std::string description_;
};

class GaryChess final : public ChessBot {
public:
    std::string_view name() const override;
    std::string_view description() const override;
    std::optional<Move> chooseMove(const Board& board) const override;
};

class GaryChessJr final : public ChessBot {
public:
    std::string_view name() const override;
    std::string_view description() const override;
    std::optional<Move> chooseMove(const Board& board) const override;
};

std::vector<std::unique_ptr<ChessBot>> createDefaultBots();

} // namespace chess
