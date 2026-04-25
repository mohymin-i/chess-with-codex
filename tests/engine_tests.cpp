#include "bot.h"
#include "chess.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool contains(const std::vector<std::string>& moves, const std::string& move) {
    return std::find(moves.begin(), moves.end(), move) != moves.end();
}

chess::ChessGame gameFromFen(const std::string& fen) {
    std::string error;
    auto game = chess::ChessGame::fromFen(fen, &error);
    require(game.has_value(), "FEN should parse: " + error);
    return *game;
}

void testStartingPosition() {
    chess::ChessGame game = chess::ChessGame::standard();
    const auto moves = game.legalMovesAlgebraic();
    require(moves.size() == 20, "starting position has 20 legal moves");
    require(contains(moves, "e4"), "starting moves contain e4");
    require(contains(moves, "Nf3"), "starting moves contain Nf3");
}

void testBasicPlayAndFen() {
    chess::ChessGame game = chess::ChessGame::standard();
    require(game.playAlgebraic("e4").ok, "e4 is legal");
    require(game.playAlgebraic("e5").ok, "e5 is legal");
    require(game.playAlgebraic("Nf3").ok, "Nf3 is legal");
    require(game.fen() == "rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2",
            "FEN updates after normal moves");
}

void testCastling() {
    chess::ChessGame game = gameFromFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    const auto moves = game.legalMovesAlgebraic();
    require(contains(moves, "O-O"), "white can castle kingside");
    require(contains(moves, "O-O-O"), "white can castle queenside");
    const auto result = game.playAlgebraic("O-O");
    require(result.ok, "O-O plays");
    require(game.fen() == "r3k2r/8/8/8/8/8/8/R4RK1 b kq - 1 1",
            "castling moves king and rook");
}

void testEnPassant() {
    chess::ChessGame game = gameFromFen("8/8/8/3pP3/8/8/8/4K2k w - d6 0 1");
    require(contains(game.legalMovesAlgebraic(), "exd6"), "en passant is formatted as a pawn capture");
    const auto result = game.playAlgebraic("exd6");
    require(result.ok, "en passant plays");
    require(game.fen() == "8/8/3P4/8/8/8/8/4K2k b - - 0 1",
            "en passant removes the captured pawn");
}

void testPromotion() {
    chess::ChessGame game = gameFromFen("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
    require(contains(game.legalMovesAlgebraic(), "a8=Q+"), "promotion to queen is legal and gives check");
    require(game.playAlgebraic("a8=Q").ok, "promotion input may omit check suffix");
    require(game.fen() == "Q3k3/8/8/8/8/8/8/4K3 b - - 0 1",
            "promotion replaces the pawn");
}

void testCheckmate() {
    chess::ChessGame game = chess::ChessGame::standard();
    require(game.playAlgebraic("f3").ok, "f3 plays");
    require(game.playAlgebraic("e5").ok, "e5 plays");
    require(game.playAlgebraic("g4").ok, "g4 plays");
    const auto mate = game.playAlgebraic("Qh4");
    require(mate.ok, "Qh4 mate plays");
    require(mate.notation == "Qh4#", "mate notation includes #");
    require(game.status() == chess::GameStatus::Checkmate, "fool's mate is checkmate");
}

void testAmbiguousNotation() {
    chess::ChessGame game = gameFromFen("4k3/8/8/8/8/8/2N1N3/4K3 w - - 0 1");
    const auto result = game.playAlgebraic("Nd4");
    require(!result.ok && result.error == chess::MoveError::AmbiguousNotation,
            "ambiguous SAN is rejected as ambiguous");
    require(game.playAlgebraic("Ncd4").ok, "disambiguated SAN resolves the move");
}

void testMakeUnmake() {
    chess::Board board = chess::Board::standard();
    const std::string before = board.toFen();
    const auto moves = board.generateLegalMoves();
    auto it = std::find_if(moves.begin(), moves.end(), [](const chess::Move& move) {
        return chess::squareName(move.from) == "e2" && chess::squareName(move.to) == "e4";
    });
    require(it != moves.end(), "e2e4 exists as a generated move");
    const chess::UndoState undo = board.makeMove(*it);
    board.unmakeMove(*it, undo);
    require(board.toFen() == before, "make/unmake restores exact FEN");
}

void testUnicodeBoard() {
    chess::ChessGame game = chess::ChessGame::standard();
    const std::string board = game.unicodeBoard();
    require(board.find("+---+---+---+---+---+---+---+---+") != std::string::npos,
            "unicode board contains grid separators");
    require(board.find("♔") != std::string::npos, "unicode board contains white king");
    require(board.find("♛") != std::string::npos, "unicode board contains black queen");
    require(board.find("| ♜ |") != std::string::npos, "unicode board separates squares with pipes");
}

void testPlayMoveApi() {
    chess::ChessGame game = chess::ChessGame::standard();
    const auto legalMoves = game.board().generateLegalMoves();
    auto it = std::find_if(legalMoves.begin(), legalMoves.end(), [](const chess::Move& move) {
        return chess::squareName(move.from) == "e2" && chess::squareName(move.to) == "e4";
    });

    require(it != legalMoves.end(), "e2e4 exists for playMove");
    const chess::MoveResult result = game.playMove(*it);
    require(result.ok, "playMove accepts a generated legal move");
    require(result.notation == "e4", "playMove returns algebraic notation");
}

void testCapturedMaterial() {
    chess::ChessGame game = chess::ChessGame::standard();
    require(game.playAlgebraic("e4").ok, "e4 plays for material test");
    require(game.playAlgebraic("d5").ok, "d5 plays for material test");
    require(game.playAlgebraic("exd5").ok, "exd5 captures for material test");

    const chess::CapturedMaterial material = game.capturedMaterial();
    require(material.byWhite[static_cast<int>(chess::PieceType::Pawn)] == 1,
            "white captured one black pawn");
    require(material.byBlack[static_cast<int>(chess::PieceType::Pawn)] == 0,
            "black has not captured a pawn");
}

void testJohnCheckersChoosesLegalMove() {
    chess::JohnCheckers bot;
    chess::ChessGame game = chess::ChessGame::standard();
    const std::optional<chess::Move> move = bot.chooseMove(game.board());

    require(bot.name() == "John Checkers", "beginner bot is named John Checkers");
    require(move.has_value(), "John Checkers chooses a move from the starting position");
    require(game.board().isLegal(*move), "John Checkers chooses a legal move");
}

void testGaryChessFindsMateInOne() {
    chess::GaryChess bot;
    chess::ChessGame game = chess::ChessGame::standard();

    require(game.playAlgebraic("f3").ok, "f3 plays for Gary test");
    require(game.playAlgebraic("e5").ok, "e5 plays for Gary test");
    require(game.playAlgebraic("g4").ok, "g4 plays for Gary test");

    const std::optional<chess::Move> move = bot.chooseMove(game.board());
    require(bot.name() == "Gary Chess", "strong bot is named Gary Chess");
    require(move.has_value(), "Gary Chess chooses a move in a mate-in-one position");
    require(game.board().isLegal(*move), "Gary Chess chooses a legal move");
    require(chess::formatAlgebraic(game.board(), *move) == "Qh4#",
            "Gary Chess finds Fool's Mate");
}

} // namespace

int main() {
    testStartingPosition();
    testBasicPlayAndFen();
    testCastling();
    testEnPassant();
    testPromotion();
    testCheckmate();
    testAmbiguousNotation();
    testMakeUnmake();
    testUnicodeBoard();
    testPlayMoveApi();
    testCapturedMaterial();
    testJohnCheckersChoosesLegalMove();
    testGaryChessFindsMateInOne();

    std::cout << "All engine tests passed.\n";
    return 0;
}
