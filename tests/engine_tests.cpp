#include "bot.h"
#include "chess.h"
#include "evaluation.h"

#include <algorithm>
#include <cstdint>
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

std::uint64_t perft(chess::Board& board, int depth) {
    if (depth == 0) {
        return 1;
    }

    const std::vector<chess::Move> moves = board.generateLegalMoves();
    if (depth == 1) {
        return moves.size();
    }

    std::uint64_t nodes = 0;
    for (const chess::Move& move : moves) {
        const chess::UndoState undo = board.makeMove(move);
        nodes += perft(board, depth - 1);
        board.unmakeMove(move, undo);
    }
    return nodes;
}

void requirePerft(const std::string& label,
                  const chess::Board& root,
                  int depth,
                  std::uint64_t expectedNodes) {
    chess::Board board = root;
    const std::string before = board.toFen();
    const std::uint64_t actualNodes = perft(board, depth);

    require(actualNodes == expectedNodes,
            label + " perft(" + std::to_string(depth) + ") expected " +
                std::to_string(expectedNodes) + " nodes, got " + std::to_string(actualNodes));
    require(board.toFen() == before, label + " perft leaves the board unchanged");
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

void testHistorySnapshots() {
    chess::ChessGame game = chess::ChessGame::standard();
    const std::string startFen = game.fen();
    require(game.playAlgebraic("e4").ok, "e4 is legal for history");
    const std::string afterE4Fen = game.fen();
    require(game.playAlgebraic("e5").ok, "e5 is legal for history");
    require(game.playAlgebraic("Nf3").ok, "Nf3 is legal for history");
    const std::string liveFen = game.fen();

    require(game.moveCount() == 3, "history tracks three plies");
    require(game.lastMove().has_value(), "history exposes the latest move");
    require(game.lastMove()->from == *chess::parseSquare("g1") &&
            game.lastMove()->to == *chess::parseSquare("f3"),
            "lastMove reports the latest played move");
    require(game.boardAtPly(0).toFen() == startFen, "boardAtPly can show the start position");
    require(game.boardAtPly(1).toFen() == afterE4Fen, "boardAtPly can show an intermediate position");
    require(game.snapshotAtPly(0).lastMove() == std::nullopt, "initial snapshot has no last move");
    require(game.snapshotAtPly(1).lastMove()->to == *chess::parseSquare("e4"),
            "intermediate snapshot reports its own last move");
    require(game.snapshotAtPly(1).fen() == afterE4Fen, "snapshotAtPly has the intermediate position");
    require(game.snapshotAtPly(99).fen() == liveFen, "snapshotAtPly clamps past the latest move");
    require(game.fen() == liveFen, "history snapshots do not mutate the live game");
    require(game.moveCount() == 3, "history snapshots do not change live history length");
}

void testBoardEditingForAnalysis() {
    chess::Board board = chess::Board::empty(chess::Color::Black);
    board.setPieceAt(*chess::parseSquare("e1"), chess::Piece{chess::Color::White, chess::PieceType::King});
    board.setPieceAt(*chess::parseSquare("e8"), chess::Piece{chess::Color::Black, chess::PieceType::King});
    board.setPieceAt(*chess::parseSquare("d1"), chess::Piece{chess::Color::White, chess::PieceType::Queen});

    require(board.sideToMove() == chess::Color::Black, "analysis board can set side to move");
    require(board.pieceAt(*chess::parseSquare("d1")).type == chess::PieceType::Queen,
            "analysis board can place pieces");
    require(board.castlingRights() == 0, "analysis editing clears castling rights");

    board.setPieceAt(*chess::parseSquare("d1"), {});
    require(board.pieceAt(*chess::parseSquare("d1")).isEmpty(), "analysis board can remove pieces");

    board.clear();
    require(board.occupancy() == 0, "analysis board can clear all pieces");
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

void testPlayMoveUnderpromotion() {
    chess::ChessGame game = gameFromFen("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
    const std::vector<chess::Move> legalMoves = game.board().generateLegalMoves();
    auto rookPromotion = std::find_if(legalMoves.begin(), legalMoves.end(), [](const chess::Move& move) {
        return chess::squareName(move.from) == "a7" &&
               chess::squareName(move.to) == "a8" &&
               move.promotion == chess::PieceType::Rook;
    });

    require(rookPromotion != legalMoves.end(), "rook underpromotion exists as a generated move");
    const chess::MoveResult result = game.playMove(*rookPromotion);
    require(result.ok, "playMove accepts selected underpromotion");
    require(game.fen() == "R3k3/8/8/8/8/8/8/4K3 b - - 0 1",
            "underpromotion replaces the pawn with the selected piece");
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

void testPerftPositions() {
    requirePerft("starting position", chess::Board::standard(), 1, 20);
    requirePerft("starting position", chess::Board::standard(), 2, 400);
    requirePerft("starting position", chess::Board::standard(), 3, 8902);

    const chess::ChessGame kiwipete =
        gameFromFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    requirePerft("kiwipete", kiwipete.board(), 1, 48);
    requirePerft("kiwipete", kiwipete.board(), 2, 2039);
    requirePerft("kiwipete", kiwipete.board(), 3, 97862);
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

void testEvaluation() {
    chess::ChessGame start = chess::ChessGame::standard();
    require(chess::evaluateBoard(start.board(), chess::Color::White) == 0,
            "starting position evaluates as equal");

    chess::ChessGame queenUp = gameFromFen("4k3/8/8/8/8/8/8/Q3K3 w - - 0 1");
    require(chess::evaluateBoard(queenUp.board(), chess::Color::White) > 800,
            "white queen advantage evaluates positively for White");
    require(chess::evaluateBoard(queenUp.board(), chess::Color::Black) < -800,
            "white queen advantage evaluates negatively for Black");
}

void testForcedMateEvaluation() {
    chess::ChessGame game = chess::ChessGame::standard();
    require(game.playAlgebraic("f3").ok, "f3 plays for forced mate evaluation");
    require(game.playAlgebraic("e5").ok, "e5 plays for forced mate evaluation");
    require(game.playAlgebraic("g4").ok, "g4 plays for forced mate evaluation");

    const chess::Evaluation evaluation = chess::evaluatePosition(game.board(), chess::Color::White, 3);
    require(evaluation.forcedMate.has_value(), "forced mate evaluation detects Fool's Mate");
    require(evaluation.forcedMate->winner == chess::Color::Black,
            "forced mate evaluation reports Black as the winning side");
    require(evaluation.forcedMate->moves == 1,
            "forced mate evaluation reports mate in one");
}

void testFenValidation() {
    std::string error;
    const auto badEmptyCount = chess::Board::fromFen("rnbqkbnr/pppppppp/00000000/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &error);
    require(!badEmptyCount.has_value(), "FEN rejects zero empty-square counts");

    const auto badMoveCounters = chess::Board::fromFen("8/8/8/8/8/8/8/4K2k w - - -1 0", &error);
    require(!badMoveCounters.has_value(), "FEN rejects invalid move counters");
}

void testJohnCheckersChoosesLegalMove() {
    chess::JohnCheckers bot;
    chess::ChessGame game = chess::ChessGame::standard();
    const std::optional<chess::Move> move = bot.chooseMove(game.board());

    require(bot.name() == "John Checkers", "beginner bot is named John Checkers");
    require(!bot.description().empty(), "John Checkers has a description");
    require(move.has_value(), "John Checkers chooses a move from the starting position");
    require(game.board().isLegal(*move), "John Checkers chooses a legal move");
}

void requireBotFindsFoolsMate(chess::ChessBot& bot, const std::string& expectedName) {
    chess::ChessGame game = chess::ChessGame::standard();

    require(game.playAlgebraic("f3").ok, "f3 plays for " + expectedName + " test");
    require(game.playAlgebraic("e5").ok, "e5 plays for " + expectedName + " test");
    require(game.playAlgebraic("g4").ok, "g4 plays for " + expectedName + " test");

    const std::optional<chess::Move> move = bot.chooseMove(game.board());
    require(bot.name() == expectedName, expectedName + " reports its expected name");
    require(!bot.description().empty(), expectedName + " has a description");
    require(move.has_value(), expectedName + " chooses a move in a mate-in-one position");
    require(game.board().isLegal(*move), expectedName + " chooses a legal move");
    require(chess::formatAlgebraic(game.board(), *move) == "Qh4#",
            expectedName + " finds Fool's Mate");
}

void testDefaultBotRoster() {
    const auto bots = chess::createDefaultBots();
    require(bots.size() == 11, "default bot roster contains eleven bots");

    const std::vector<std::string> expectedNames{
        "John Checkers",
        "Level 2",
        "Level 3",
        "Level 4",
        "Level 5",
        "Level 6",
        "Level 7",
        "Level 8",
        "Level 9",
        "Gary Chess Jr",
        "Gary Chess",
    };

    for (std::size_t i = 0; i < expectedNames.size(); ++i) {
        require(bots[i]->name() == expectedNames[i], "bot roster is ordered by difficulty");
        require(!bots[i]->description().empty(), "bot roster entries have descriptions");
    }

    chess::ChessGame game = chess::ChessGame::standard();
    require(game.playAlgebraic("f3").ok, "f3 plays for roster bot test");
    require(game.playAlgebraic("e5").ok, "e5 plays for roster bot test");
    require(game.playAlgebraic("g4").ok, "g4 plays for roster bot test");

    const std::optional<chess::Move> levelTwoMove = bots[1]->chooseMove(game.board());
    const std::optional<chess::Move> levelEightMove = bots[7]->chooseMove(game.board());
    const std::optional<chess::Move> levelNineMove = bots[8]->chooseMove(game.board());
    const std::optional<chess::Move> garyJrMove = bots[9]->chooseMove(game.board());
    require(levelTwoMove.has_value(), "Level 2 chooses a move from a mate-in-one position");
    require(levelEightMove.has_value(), "Level 8 chooses a move from a mate-in-one position");
    require(levelNineMove.has_value(), "Level 9 chooses a move from a mate-in-one position");
    require(garyJrMove.has_value(), "Gary Chess Jr chooses a move from a mate-in-one position");
    require(game.board().isLegal(*levelTwoMove), "Level 2 chooses a legal move");
    require(game.board().isLegal(*levelEightMove), "Level 8 chooses a legal move");
    require(game.board().isLegal(*levelNineMove), "Level 9 chooses a legal move");
    require(game.board().isLegal(*garyJrMove), "Gary Chess Jr chooses a legal move");
}

void testGaryChessFindsMateInOne() {
    chess::GaryChess bot;
    requireBotFindsFoolsMate(bot, "Gary Chess");
}

void testGaryChessJrFindsMateInOne() {
    chess::GaryChessJr bot;
    requireBotFindsFoolsMate(bot, "Gary Chess Jr");
}

} // namespace

int main() {
    testStartingPosition();
    testBasicPlayAndFen();
    testHistorySnapshots();
    testBoardEditingForAnalysis();
    testCastling();
    testEnPassant();
    testPromotion();
    testPlayMoveUnderpromotion();
    testCheckmate();
    testAmbiguousNotation();
    testMakeUnmake();
    testPerftPositions();
    testUnicodeBoard();
    testPlayMoveApi();
    testCapturedMaterial();
    testEvaluation();
    testForcedMateEvaluation();
    testFenValidation();
    testJohnCheckersChoosesLegalMove();
    testDefaultBotRoster();
    testGaryChessFindsMateInOne();
    testGaryChessJrFindsMateInOne();

    std::cout << "All engine tests passed.\n";
    return 0;
}
