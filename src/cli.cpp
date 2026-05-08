#include "cli.h"

#include "bot.h"
#include "chess.h"
#include "evaluation.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

enum class GameMode {
    TwoPlayer,
    Bot,
};

// CLI-only terminal state. The engine keeps game rules and move legality
// separate from presentation concerns like board/material auto-printing.
struct DisplayOptions {
    bool printBoard = false;
    bool materialSidebar = false;
};

struct EvaluationSnapshot {
    chess::Board board;
    chess::GameStatus status = chess::GameStatus::Ongoing;
    chess::Color sideToMove = chess::Color::White;
    chess::Color perspective = chess::Color::White;
};

// Saved games are presentation/application state, not engine state. The CLI
// records SAN as moves are accepted and flushes a simple text log after each
// move so partial games are preserved if the user quits or resigns.
struct SaveState {
    bool enabled = false;
    std::filesystem::path path;
    std::string whitePlayer;
    std::string blackPlayer;
    std::vector<std::string> moves;
};

void printStatus(const chess::ChessGame& game) {
    const chess::GameStatus status = game.status();
    std::cout << "Status: " << chess::statusName(status) << '\n';
    if (status == chess::GameStatus::Ongoing || status == chess::GameStatus::Check) {
        std::cout << chess::colorName(game.sideToMove()) << " to move\n";
    }
}

bool isTerminal(chess::GameStatus status) {
    return status == chess::GameStatus::Checkmate ||
           status == chess::GameStatus::Stalemate ||
           status == chess::GameStatus::FiftyMoveDraw ||
           status == chess::GameStatus::InsufficientMaterialDraw;
}

std::string formatEvaluation(int centipawns) {
    const double pawns = std::abs(centipawns) < 5 ? 0.0 : static_cast<double>(centipawns) / 100.0;
    std::ostringstream output;
    output << std::showpos << std::fixed << std::setprecision(2) << pawns;
    return output.str();
}

std::string formatEvaluation(const chess::Evaluation& evaluation) {
    if (evaluation.forcedMate) {
        if (evaluation.forcedMate->moves == 0) {
            return std::string(chess::colorName(evaluation.forcedMate->winner)) + " checkmate";
        }
        return std::string(chess::colorName(evaluation.forcedMate->winner)) +
               " Mate in " + std::to_string(evaluation.forcedMate->moves);
    }

    return formatEvaluation(evaluation.centipawns);
}

void printEvaluation(std::string_view label, const EvaluationSnapshot& snapshot, int maxMateSearchPly) {
    const chess::Evaluation evaluation =
        chess::evaluatePosition(snapshot.board, snapshot.perspective, maxMateSearchPly);
    std::cout << '\n'
              << label << " evaluation (" << chess::colorName(snapshot.perspective)
              << "): " << formatEvaluation(evaluation) << '\n';
}

class EvaluationPrinter {
public:
    EvaluationPrinter(bool enabled, chess::Color perspective)
        : enabled_(enabled), perspective_(perspective) {}

    ~EvaluationPrinter() {
        stop();
    }

    void restart(const chess::ChessGame& game) {
        stop();
        if (!enabled_ || isTerminal(game.status())) {
            return;
        }

        EvaluationSnapshot snapshot{game.board(), game.status(), game.sideToMove(), perspective_};
        const int generation = nextGeneration();

        printEvaluation("Initial", snapshot, 3);
        worker_ = std::thread([this, generation, snapshot]() {
            if ([this, generation]() {
                    std::unique_lock<std::mutex> lock(mutex_);
                    return cancelSignal_.wait_for(lock, std::chrono::seconds(2), [&]() {
                        return generation != generation_;
                    });
                }()) {
                return;
            }
            printEvaluation("Second", snapshot, 5);

            std::unique_lock<std::mutex> lock(mutex_);
            if (cancelSignal_.wait_for(lock, std::chrono::seconds(6), [&]() {
                    return generation != generation_;
                })) {
                return;
            }
            printEvaluation("Final", snapshot, 7);
        });
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++generation_;
        }
        cancelSignal_.notify_all();

        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    int nextGeneration() {
        std::lock_guard<std::mutex> lock(mutex_);
        return ++generation_;
    }

    bool enabled_ = false;
    chess::Color perspective_ = chess::Color::White;
    std::mutex mutex_;
    std::condition_variable cancelSignal_;
    std::thread worker_;
    int generation_ = 0;
};

void printGameOver(const chess::ChessGame& game, chess::GameStatus status) {
    std::cout << "Game over: ";
    switch (status) {
    case chess::GameStatus::Checkmate:
        std::cout << chess::colorName(chess::opposite(game.sideToMove())) << " wins by checkmate.\n";
        break;
    case chess::GameStatus::Stalemate:
        std::cout << "draw by stalemate.\n";
        break;
    case chess::GameStatus::FiftyMoveDraw:
        std::cout << "draw by fifty-move rule.\n";
        break;
    case chess::GameStatus::InsufficientMaterialDraw:
        std::cout << "draw by insufficient material.\n";
        break;
    case chess::GameStatus::Ongoing:
    case chess::GameStatus::Check:
        std::cout << chess::statusName(status) << ".\n";
        break;
    }
}

void printResignation(chess::Color resigned) {
    std::cout << "Game over: " << chess::colorName(resigned)
              << " resigns. " << chess::colorName(chess::opposite(resigned))
              << " wins.\n";
}

void printMoveWarning(chess::MoveError error) {
    if (error == chess::MoveError::IllegalMove) {
        std::cout << "Warning: illegal move. Please enter a legal move.\n";
        return;
    }

    std::cout << "Warning: move rejected: " << chess::moveErrorName(error) << ".\n";
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [--printBoard]\n";
}

std::string trim(std::string_view text) {
    std::size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first]))) {
        ++first;
    }

    std::size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) {
        --last;
    }

    return std::string(text.substr(first, last - first));
}

std::string lower(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (char ch : text) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return result;
}

std::filesystem::path savedGamesDirectory() {
    return "saved-games";
}

std::string saveTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
    if (const std::tm* current = std::localtime(&time)) {
        localTime = *current;
    }

    std::ostringstream output;
    output << std::put_time(&localTime, "%Y%m%d_%H%M%S");
    return output.str();
}

std::filesystem::path nextSavePath() {
    const std::filesystem::path directory = savedGamesDirectory();
    const std::string baseName = "game_" + saveTimestamp();

    std::filesystem::path path = directory / (baseName + ".txt");
    for (int suffix = 1; std::filesystem::exists(path); ++suffix) {
        path = directory / (baseName + "_" + std::to_string(suffix) + ".txt");
    }

    return path;
}

bool writeSaveFile(const SaveState& save) {
    std::ofstream output(save.path);
    if (!output) {
        return false;
    }

    output << "White: " << save.whitePlayer << '\n';
    output << "Black: " << save.blackPlayer << "\n\n";
    output << "Moves:\n";

    for (std::size_t i = 0; i < save.moves.size(); i += 2) {
        output << ((i / 2) + 1) << ". " << save.moves[i];
        if (i + 1 < save.moves.size()) {
            output << ' ' << save.moves[i + 1];
        }
        output << '\n';
    }

    return true;
}

void recordMove(SaveState& save, std::string_view notation) {
    if (!save.enabled) {
        return;
    }

    save.moves.emplace_back(notation);
    if (!writeSaveFile(save)) {
        std::cout << "Warning: could not update saved game at " << save.path.string() << ".\n";
        save.enabled = false;
    }
}

std::optional<bool> promptSaveGame() {
    std::string input;
    while (true) {
        std::cout << "\nSave this game? (y/n)\n";
        std::cout << "> ";

        if (!std::getline(std::cin, input)) {
            return std::nullopt;
        }

        const std::string command = lower(trim(input));
        if (command == "y" || command == "yes") {
            return true;
        }
        if (command == "n" || command == "no") {
            return false;
        }
        if (command == "quit" || command == "exit") {
            return std::nullopt;
        }

        std::cout << "Warning: please answer yes or no.\n";
    }
}

std::optional<bool> promptShowEvaluation() {
    std::string input;
    while (true) {
        std::cout << "\nShow evaluation? (y/n)\n";
        std::cout << "> ";

        if (!std::getline(std::cin, input)) {
            return std::nullopt;
        }

        const std::string command = lower(trim(input));
        if (command == "y" || command == "yes") {
            return true;
        }
        if (command == "n" || command == "no") {
            return false;
        }
        if (command == "quit" || command == "exit") {
            return std::nullopt;
        }

        std::cout << "Warning: please answer yes or no.\n";
    }
}

std::optional<SaveState> configureSaveGame(std::string whitePlayer, std::string blackPlayer) {
    const std::optional<bool> shouldSave = promptSaveGame();
    if (!shouldSave) {
        return std::nullopt;
    }

    SaveState save;
    save.whitePlayer = std::move(whitePlayer);
    save.blackPlayer = std::move(blackPlayer);

    if (!*shouldSave) {
        return save;
    }

    std::error_code error;
    std::filesystem::create_directories(savedGamesDirectory(), error);
    if (error) {
        std::cout << "Warning: could not create saved-games folder: " << error.message() << ".\n";
        return save;
    }

    save.enabled = true;
    save.path = nextSavePath();
    if (!writeSaveFile(save)) {
        std::cout << "Warning: could not create saved game at " << save.path.string() << ".\n";
        save.enabled = false;
        return save;
    }

    std::cout << "Saving game to " << save.path.string() << '\n';
    return save;
}

// The menu helpers deliberately accept a few text aliases so scripted tests and
// humans can use either numbers or obvious words without changing engine input.
std::optional<GameMode> selectGameMode() {
    std::string input;
    while (true) {
        std::cout << "Select game mode:\n";
        std::cout << "  1. 2 player\n";
        std::cout << "  2. Bots (default)\n";
        std::cout << "> ";

        if (!std::getline(std::cin, input)) {
            return std::nullopt;
        }

        const std::string command = lower(trim(input));
        if (command.empty()) {
            return GameMode::Bot;
        }
        if (command == "1" || command == "2 player" || command == "two player" ||
            command == "player" || command == "players") {
            return GameMode::TwoPlayer;
        }
        if (command == "2" || command == "bot" || command == "bots") {
            return GameMode::Bot;
        }
        if (command == "quit" || command == "exit") {
            return std::nullopt;
        }

        std::cout << "Warning: unknown menu option. Choose 1 for 2 player or 2 for bots.\n\n";
    }
}

std::optional<std::size_t> selectBot(const std::vector<const chess::ChessBot*>& bots) {
    std::string input;
    while (true) {
        std::cout << "\nSelect bot:\n";
        for (std::size_t i = 0; i < bots.size(); ++i) {
            std::cout << "  " << (i + 1) << ". " << bots[i]->name()
                      << (i == 0 ? " (default)" : "")
                      << " - " << bots[i]->description() << '\n';
        }
        std::cout << "> ";

        if (!std::getline(std::cin, input)) {
            return std::nullopt;
        }

        const std::string command = lower(trim(input));
        if (command.empty()) {
            return 0;
        }
        for (std::size_t i = 0; i < bots.size(); ++i) {
            if (command == std::to_string(i + 1) || command == lower(bots[i]->name())) {
                return i;
            }
        }
        if (command == "quit" || command == "exit" || command == "back") {
            return std::nullopt;
        }

        std::cout << "Warning: unknown bot option. Choose a listed bot number or name.\n";
    }
}

std::optional<chess::Color> selectHumanColor() {
    std::string input;
    while (true) {
        std::cout << "\nChoose your color:\n";
        std::cout << "  1. White\n";
        std::cout << "  2. Black\n";
        std::cout << "> ";

        if (!std::getline(std::cin, input)) {
            return std::nullopt;
        }

        const std::string command = lower(trim(input));
        if (command == "1" || command == "white" || command == "w") {
            return chess::Color::White;
        }
        if (command == "2" || command == "black" || command == "b") {
            return chess::Color::Black;
        }
        if (command == "quit" || command == "exit" || command == "back") {
            return std::nullopt;
        }

        std::cout << "Warning: unknown color option. Choose 1 for White or 2 for Black.\n";
    }
}

bool shouldBotMove(GameMode mode, chess::Color sideToMove, chess::Color botColor) {
    return mode == GameMode::Bot && sideToMove == botColor;
}

void printCommands() {
    std::cout << "Commands: moves, fen, board, print, p, print toggle, material, material toggle, resign, quit\n\n";
}

int materialValue(chess::PieceType type) {
    switch (type) {
    case chess::PieceType::Pawn:
        return 1;
    case chess::PieceType::Knight:
    case chess::PieceType::Bishop:
        return 3;
    case chess::PieceType::Rook:
        return 5;
    case chess::PieceType::Queen:
        return 9;
    case chess::PieceType::King:
    case chess::PieceType::None:
        return 0;
    }
    return 0;
}

std::string pieceSymbol(chess::Color color, chess::PieceType type) {
    if (color == chess::Color::White) {
        switch (type) {
        case chess::PieceType::Queen:
            return "♕";
        case chess::PieceType::Rook:
            return "♖";
        case chess::PieceType::Bishop:
            return "♗";
        case chess::PieceType::Knight:
            return "♘";
        case chess::PieceType::Pawn:
            return "♙";
        case chess::PieceType::King:
        case chess::PieceType::None:
            return "";
        }
    }

    switch (type) {
    case chess::PieceType::Queen:
        return "♛";
    case chess::PieceType::Rook:
        return "♜";
    case chess::PieceType::Bishop:
        return "♝";
    case chess::PieceType::Knight:
        return "♞";
    case chess::PieceType::Pawn:
        return "♟";
    case chess::PieceType::King:
    case chess::PieceType::None:
        return "";
    }

    return "";
}

int capturedPoints(const std::array<int, 6>& counts) {
    int points = 0;
    for (chess::PieceType type : {
             chess::PieceType::Queen,
             chess::PieceType::Rook,
             chess::PieceType::Bishop,
             chess::PieceType::Knight,
             chess::PieceType::Pawn,
         }) {
        points += counts[static_cast<int>(type)] * materialValue(type);
    }
    return points;
}

std::string capturedPiecesText(const std::array<int, 6>& counts, chess::Color capturedColor) {
    std::ostringstream output;
    bool any = false;

    for (chess::PieceType type : {
             chess::PieceType::Queen,
             chess::PieceType::Rook,
             chess::PieceType::Bishop,
             chess::PieceType::Knight,
             chess::PieceType::Pawn,
         }) {
        const int count = counts[static_cast<int>(type)];
        if (count == 0) {
            continue;
        }

        if (any) {
            output << ' ';
        }
        output << pieceSymbol(capturedColor, type) << 'x' << count;
        any = true;
    }

    return any ? output.str() : "none";
}

std::vector<std::string> materialLines(const chess::ChessGame& game) {
    const chess::CapturedMaterial material = game.capturedMaterial();
    const int whitePoints = capturedPoints(material.byWhite);
    const int blackPoints = capturedPoints(material.byBlack);

    std::vector<std::string> lines;
    lines.push_back("Captured material");

    std::ostringstream white;
    white << "White: " << capturedPiecesText(material.byWhite, chess::Color::Black)
          << " | " << whitePoints << (whitePoints == 1 ? " point" : " points");
    lines.push_back(white.str());

    std::ostringstream black;
    black << "Black: " << capturedPiecesText(material.byBlack, chess::Color::White)
          << " | " << blackPoints << (blackPoints == 1 ? " point" : " points");
    lines.push_back(black.str());

    const int differential = whitePoints - blackPoints;
    std::ostringstream diff;
    diff << "Material: ";
    if (differential > 0) {
        diff << "White +" << differential;
    } else if (differential < 0) {
        diff << "Black +" << -differential;
    } else {
        diff << "even";
    }
    lines.push_back(diff.str());

    return lines;
}

std::string materialText(const chess::ChessGame& game) {
    std::ostringstream output;
    for (const std::string& line : materialLines(game)) {
        output << line << '\n';
    }
    return output.str();
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    return lines;
}

// When material display is toggled on, the board stays visually unchanged and
// material is appended as a right-side sidebar. This keeps standalone
// `material` useful without forcing a board redraw.
std::string boardText(const chess::ChessGame& game, bool materialSidebar) {
    if (!materialSidebar) {
        return game.unicodeBoard();
    }

    std::vector<std::string> board = splitLines(game.unicodeBoard());
    const std::vector<std::string> material = materialLines(game);
    const std::size_t lineCount = std::max(board.size(), material.size());

    std::ostringstream output;
    for (std::size_t i = 0; i < lineCount; ++i) {
        if (i < board.size()) {
            output << board[i];
        }
        if (i < material.size()) {
            output << "   " << material[i];
        }
        output << '\n';
    }
    return output.str();
}

void printBoard(const chess::ChessGame& game, bool materialSidebar) {
    std::cout << boardText(game, materialSidebar);
}

bool playBotTurn(chess::ChessGame& game,
                 const chess::ChessBot& bot,
                 const DisplayOptions& display,
                 SaveState& save,
                 EvaluationPrinter& evaluation) {
    const std::optional<chess::Move> move = bot.chooseMove(game.board());
    if (!move) {
        evaluation.stop();
        printGameOver(game, game.status());
        return false;
    }

    const chess::MoveResult result = game.playMove(*move);
    if (!result.ok) {
        evaluation.stop();
        std::cout << "Warning: " << bot.name() << " could not play a legal move.\n";
        printMoveWarning(result.error);
        return false;
    }

    std::cout << bot.name() << " plays: " << result.notation << '\n';
    recordMove(save, result.notation);
    if (display.printBoard) {
        printBoard(game, display.materialSidebar);
    }
    if (isTerminal(result.status)) {
        evaluation.stop();
        printGameOver(game, result.status);
        return false;
    }

    evaluation.restart(game);
    printStatus(game);
    return true;
}

void runGame(GameMode mode,
             const chess::ChessBot* bot,
             chess::Color botColor,
             bool printBoardMode,
             bool showEvaluation,
             chess::Color evaluationPerspective,
             SaveState save) {
    chess::ChessGame game = chess::ChessGame::standard();
    DisplayOptions display{printBoardMode, false};
    EvaluationPrinter evaluation(showEvaluation, evaluationPerspective);

    if (mode == GameMode::Bot && bot != nullptr) {
        const chess::Color humanColor = chess::opposite(botColor);
        std::cout << "You are " << chess::colorName(humanColor) << ". "
                  << bot->name() << " plays " << chess::colorName(botColor) << ".\n";
    }

    std::cout << "Enter SAN moves such as e4, Nf3, O-O, exd8=Q.\n";
    printCommands();
    if (display.printBoard) {
        printBoard(game, display.materialSidebar);
        std::cout << '\n';
    }
    printStatus(game);
    evaluation.restart(game);

    std::string input;
    while (true) {
        const chess::GameStatus turnStatus = game.status();
        if (isTerminal(turnStatus)) {
            evaluation.stop();
            printGameOver(game, turnStatus);
            return;
        }

        if (bot != nullptr && shouldBotMove(mode, game.sideToMove(), botColor)) {
            if (!playBotTurn(game, *bot, display, save, evaluation)) {
                return;
            }
            continue;
        }

        std::cout << "\n" << chess::colorName(game.sideToMove()) << "> ";
        if (!std::getline(std::cin, input)) {
            return;
        }

        const std::string trimmedInput = trim(input);
        const std::string command = lower(trimmedInput);

        if (command == "quit" || command == "exit") {
            evaluation.stop();
            return;
        }
        if (command == "resign") {
            evaluation.stop();
            printResignation(game.sideToMove());
            return;
        }
        if (command == "print toggle") {
            display.printBoard = !display.printBoard;
            std::cout << "Print turned " << (display.printBoard ? "on" : "off") << ".\n";
            if (display.printBoard) {
                printBoard(game, display.materialSidebar);
            }
            continue;
        }
        if (command == "material toggle") {
            display.materialSidebar = !display.materialSidebar;
            std::cout << "Material display turned " << (display.materialSidebar ? "on" : "off") << ".\n";
            continue;
        }
        if (command == "material") {
            if (display.materialSidebar) {
                printBoard(game, true);
            } else {
                std::cout << materialText(game);
            }
            continue;
        }
        if (command == "board" || command == "print" || command == "p") {
            printBoard(game, display.materialSidebar);
            continue;
        }
        if (command == "fen") {
            std::cout << game.fen() << '\n';
            continue;
        }
        if (command == "moves") {
            const auto moves = game.legalMovesAlgebraic();
            for (const std::string& move : moves) {
                std::cout << move << ' ';
            }
            std::cout << '\n';
            continue;
        }

        const chess::MoveResult result = game.playAlgebraic(trimmedInput);
        if (!result.ok) {
            printMoveWarning(result.error);
            continue;
        }

        std::cout << "Played: " << result.notation << '\n';
        recordMove(save, result.notation);
        if (display.printBoard) {
            printBoard(game, display.materialSidebar);
        }
        if (isTerminal(result.status)) {
            evaluation.stop();
            printGameOver(game, result.status);
            return;
        }

        evaluation.restart(game);
        if (bot != nullptr && shouldBotMove(mode, game.sideToMove(), botColor)) {
            continue;
        }

        printStatus(game);
    }
}

} // namespace

int runCli(int argc, char* argv[]) {
    bool printBoard = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--printBoard" || arg == "printBoard") {
            printBoard = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << '\n';
            printUsage(argv[0]);
            return 1;
        }
    }

    std::cout << "Chess with Codex\n";

    const std::optional<GameMode> mode = selectGameMode();
    if (!mode) {
        return 0;
    }

    std::vector<std::unique_ptr<chess::ChessBot>> botStorage = chess::createDefaultBots();
    std::vector<const chess::ChessBot*> bots;
    bots.reserve(botStorage.size());
    for (const auto& bot : botStorage) {
        bots.push_back(bot.get());
    }

    if (*mode == GameMode::Bot) {
        const std::optional<std::size_t> selectedBot = selectBot(bots);
        if (!selectedBot) {
            return 0;
        }
        const std::optional<chess::Color> humanColor = selectHumanColor();
        if (!humanColor) {
            return 0;
        }
        const chess::ChessBot* bot = bots[*selectedBot];
        const chess::Color botColor = chess::opposite(*humanColor);
        const std::string whitePlayer =
            *humanColor == chess::Color::White ? "Human" : std::string(bot->name());
        const std::string blackPlayer =
            *humanColor == chess::Color::Black ? "Human" : std::string(bot->name());
        std::optional<SaveState> save = configureSaveGame(whitePlayer, blackPlayer);
        if (!save) {
            return 0;
        }
        const std::optional<bool> showEvaluation = promptShowEvaluation();
        if (!showEvaluation) {
            return 0;
        }
        runGame(*mode, bot, botColor, printBoard, *showEvaluation, *humanColor, std::move(*save));
    } else {
        std::optional<SaveState> save = configureSaveGame("Player 1", "Player 2");
        if (!save) {
            return 0;
        }
        const std::optional<bool> showEvaluation = promptShowEvaluation();
        if (!showEvaluation) {
            return 0;
        }
        runGame(*mode, nullptr, chess::Color::Black, printBoard, *showEvaluation, chess::Color::White, std::move(*save));
    }

    return 0;
}
