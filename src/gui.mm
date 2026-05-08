#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <dispatch/dispatch.h>

#include "bot.h"
#include "chess.h"
#include "evaluation.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

NSString* const ThemePreferenceKey = @"ChessWithCodex.ThemePreference";
NSString* const AutopromoteEnabledPreferenceKey = @"ChessWithCodex.AutopromoteEnabled";
NSString* const AutopromotePiecePreferenceKey = @"ChessWithCodex.AutopromotePiece";
NSString* const BoardLightSquarePreferenceKey = @"ChessWithCodex.BoardLightSquare";
NSString* const BoardDarkSquarePreferenceKey = @"ChessWithCodex.BoardDarkSquare";
NSString* const BoardImagePreferenceKey = @"ChessWithCodex.BoardImagePath";
NSString* const StartModePreferenceKey = @"ChessWithCodex.Start.Mode";
NSString* const StartBotPreferenceKey = @"ChessWithCodex.Start.Bot";
NSString* const StartColorPreferenceKey = @"ChessWithCodex.Start.Color";
NSString* const StartSavePreferenceKey = @"ChessWithCodex.Start.Save";
NSString* const StartEvaluationPreferenceKey = @"ChessWithCodex.Start.Evaluation";
NSString* const DefaultLightSquareHex = @"#E0DCC7";
NSString* const DefaultDarkSquareHex = @"#6B856B";

NSString* toNSString(std::string_view text) {
    return [[NSString alloc] initWithBytes:text.data()
                                    length:text.size()
                                  encoding:NSUTF8StringEncoding];
}

NSString* colorToNSString(chess::Color color) {
    return toNSString(chess::colorName(color));
}

NSString* pieceSymbol(chess::Piece piece) {
    if (piece.isEmpty()) {
        return @"";
    }

    // Use filled Unicode silhouettes for both colors. White/black identity is
    // applied by the renderer instead of relying on the hollow white glyphs.
    switch (piece.type) {
    case chess::PieceType::King:
        return @"♚";
    case chess::PieceType::Queen:
        return @"♛";
    case chess::PieceType::Rook:
        return @"♜";
    case chess::PieceType::Bishop:
        return @"♝";
    case chess::PieceType::Knight:
        return @"♞";
    case chess::PieceType::Pawn:
        return @"♟";
    case chess::PieceType::None:
        return @"";
    }

    return @"";
}

NSFont* pieceFont(CGFloat size) {
    NSFont* font = [NSFont fontWithName:@"Apple Symbols" size:size];
    if (font == nil) {
        font = [NSFont systemFontOfSize:size weight:NSFontWeightRegular];
    }
    return font;
}

void drawSymbolCentered(NSString* symbol, NSFont* font, NSColor* color, NSRect rect) {
    NSDictionary<NSAttributedStringKey, id>* attributes = @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: color,
    };

    const NSSize symbolSize = [symbol sizeWithAttributes:attributes];
    const CGFloat x = NSMidX(rect) - (symbolSize.width / 2.0);
    const CGFloat y = NSMidY(rect) - (symbolSize.height / 2.0) - (NSHeight(rect) * 0.015);
    [symbol drawAtPoint:NSMakePoint(x, y) withAttributes:attributes];
}

NSString* colorSlug(chess::Color color) {
    return color == chess::Color::White ? @"white" : @"black";
}

NSString* pieceTypeSlug(chess::PieceType type) {
    switch (type) {
    case chess::PieceType::King:
        return @"king";
    case chess::PieceType::Queen:
        return @"queen";
    case chess::PieceType::Rook:
        return @"rook";
    case chess::PieceType::Bishop:
        return @"bishop";
    case chess::PieceType::Knight:
        return @"knight";
    case chess::PieceType::Pawn:
        return @"pawn";
    case chess::PieceType::None:
        return @"none";
    }
    return @"none";
}

NSString* pieceImagePreferenceKey(chess::Piece piece) {
    return [NSString stringWithFormat:@"ChessWithCodex.PieceImage.%@.%@",
                                      colorSlug(piece.color),
                                      pieceTypeSlug(piece.type)];
}

NSString* pieceImageStorageName(chess::Piece piece) {
    return [NSString stringWithFormat:@"%@-%@.png",
                                      colorSlug(piece.color),
                                      pieceTypeSlug(piece.type)];
}

NSMutableDictionary<NSString*, NSImage*>* imageCache() {
    static NSMutableDictionary<NSString*, NSImage*>* cache = [[NSMutableDictionary alloc] init];
    return cache;
}

void clearImageCache() {
    [imageCache() removeAllObjects];
}

NSURL* customAssetDirectory() {
    NSFileManager* fileManager = [NSFileManager defaultManager];
    NSURL* supportURL = [fileManager URLForDirectory:NSApplicationSupportDirectory
                                            inDomain:NSUserDomainMask
                                   appropriateForURL:nil
                                              create:YES
                                               error:nil];
    if (supportURL == nil) {
        supportURL = [NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES];
    }

    NSURL* directory = [supportURL URLByAppendingPathComponent:@"ChessWithCodex" isDirectory:YES];
    [fileManager createDirectoryAtURL:directory
          withIntermediateDirectories:YES
                           attributes:nil
                                error:nil];
    return directory;
}

NSImage* squareCroppedImage(NSImage* source) {
    if (source == nil) {
        return nil;
    }

    CGImageRef sourceImage = [source CGImageForProposedRect:nullptr context:nil hints:nil];
    if (sourceImage == nil) {
        return nil;
    }

    const size_t width = CGImageGetWidth(sourceImage);
    const size_t height = CGImageGetHeight(sourceImage);
    if (width == 0 || height == 0) {
        return nil;
    }

    const size_t side = std::min(width, height);
    const CGRect cropRect = CGRectMake(static_cast<CGFloat>((width - side) / 2),
                                       static_cast<CGFloat>((height - side) / 2),
                                       static_cast<CGFloat>(side),
                                       static_cast<CGFloat>(side));
    CGImageRef cropped = CGImageCreateWithImageInRect(sourceImage, cropRect);
    if (cropped == nil) {
        return nil;
    }

    NSBitmapImageRep* representation = [[NSBitmapImageRep alloc] initWithCGImage:cropped];
    CGImageRelease(cropped);
    NSImage* image = [[NSImage alloc] initWithSize:NSMakeSize(side, side)];
    [image addRepresentation:representation];
    return image;
}

NSData* PNGDataForImage(NSImage* image) {
    NSImage* squareImage = squareCroppedImage(image);
    if (squareImage == nil) {
        return nil;
    }

    CGImageRef cgImage = [squareImage CGImageForProposedRect:nullptr context:nil hints:nil];
    if (cgImage == nil) {
        return nil;
    }

    NSBitmapImageRep* representation = [[NSBitmapImageRep alloc] initWithCGImage:cgImage];
    return [representation representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
}

BOOL saveSquarePNGFromURL(NSURL* sourceURL, NSString* filename, NSString** outputPath) {
    NSImage* sourceImage = [[NSImage alloc] initWithContentsOfURL:sourceURL];
    NSData* pngData = PNGDataForImage(sourceImage);
    if (pngData == nil) {
        return NO;
    }

    NSURL* destinationURL = [customAssetDirectory() URLByAppendingPathComponent:filename isDirectory:NO];
    if (![pngData writeToURL:destinationURL atomically:YES]) {
        return NO;
    }

    if (outputPath != nullptr) {
        *outputPath = destinationURL.path;
    }
    return YES;
}

void drawDefaultPieceGlyph(chess::Piece piece, NSRect squareRect, CGFloat squareSize) {
    NSString* symbol = pieceSymbol(piece);
    if (symbol.length == 0) {
        return;
    }

    NSColor* outlineColor = piece.color == chess::Color::White
                                ? [NSColor blackColor]
                                : [NSColor colorWithCalibratedWhite:0.92 alpha:0.78];
    NSColor* fillColor = piece.color == chess::Color::White
                             ? [NSColor colorWithCalibratedWhite:0.98 alpha:1.0]
                             : [NSColor blackColor];

    drawSymbolCentered(symbol, pieceFont(squareSize * 0.70), outlineColor, squareRect);
    drawSymbolCentered(symbol, pieceFont(squareSize * 0.62), fillColor, squareRect);
}

NSImage* generatedDefaultPieceImage(chess::Piece piece) {
    const CGFloat imageSize = 256.0;
    NSImage* image = [[NSImage alloc] initWithSize:NSMakeSize(imageSize, imageSize)];
    [image lockFocus];
    [[NSColor clearColor] setFill];
    NSRectFill(NSMakeRect(0, 0, imageSize, imageSize));
    drawDefaultPieceGlyph(piece, NSMakeRect(0, 0, imageSize, imageSize), imageSize);
    [image unlockFocus];
    return image;
}

NSImage* pieceImage(chess::Piece piece) {
    if (piece.isEmpty()) {
        return nil;
    }

    NSString* customPath = [[NSUserDefaults standardUserDefaults] stringForKey:pieceImagePreferenceKey(piece)];
    NSString* cacheKey = customPath.length > 0
                             ? [NSString stringWithFormat:@"custom:%@", customPath]
                             : [NSString stringWithFormat:@"default:%@:%@",
                                                         colorSlug(piece.color),
                                                         pieceTypeSlug(piece.type)];
    NSImage* cached = imageCache()[cacheKey];
    if (cached != nil) {
        return cached;
    }

    NSImage* image = nil;
    if (customPath.length > 0) {
        image = [[NSImage alloc] initWithContentsOfFile:customPath];
    }
    if (image == nil) {
        image = generatedDefaultPieceImage(piece);
    }

    if (image != nil) {
        imageCache()[cacheKey] = image;
    }
    return image;
}

NSImage* boardBackgroundImage() {
    NSString* path = [[NSUserDefaults standardUserDefaults] stringForKey:BoardImagePreferenceKey];
    if (path.length == 0) {
        return nil;
    }

    NSString* cacheKey = [NSString stringWithFormat:@"board:%@", path];
    NSImage* cached = imageCache()[cacheKey];
    if (cached != nil) {
        return cached;
    }

    NSImage* image = [[NSImage alloc] initWithContentsOfFile:path];
    if (image != nil) {
        imageCache()[cacheKey] = image;
    }
    return image;
}

NSColor* colorFromHexString(NSString* value) {
    NSString* hex = [[value stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]] uppercaseString];
    if ([hex hasPrefix:@"#"]) {
        hex = [hex substringFromIndex:1];
    }
    if (hex.length == 3) {
        hex = [NSString stringWithFormat:@"%C%C%C%C%C%C",
                                         [hex characterAtIndex:0],
                                         [hex characterAtIndex:0],
                                         [hex characterAtIndex:1],
                                         [hex characterAtIndex:1],
                                         [hex characterAtIndex:2],
                                         [hex characterAtIndex:2]];
    }
    if (hex.length != 6) {
        return nil;
    }

    unsigned int rgb = 0;
    NSScanner* scanner = [NSScanner scannerWithString:hex];
    if (![scanner scanHexInt:&rgb] || scanner.scanLocation != hex.length) {
        return nil;
    }

    return [NSColor colorWithCalibratedRed:((rgb >> 16) & 0xFF) / 255.0
                                     green:((rgb >> 8) & 0xFF) / 255.0
                                      blue:(rgb & 0xFF) / 255.0
                                     alpha:1.0];
}

NSColor* colorFromUserString(NSString* value) {
    NSString* trimmed = [value stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (trimmed.length == 0) {
        return nil;
    }

    NSColor* hexColor = colorFromHexString(trimmed);
    if (hexColor != nil) {
        return hexColor;
    }

    NSString* cleaned = [trimmed lowercaseString];
    if ([cleaned hasPrefix:@"rgb("] && [cleaned hasSuffix:@")"]) {
        cleaned = [cleaned substringWithRange:NSMakeRange(4, cleaned.length - 5)];
    }

    NSArray<NSString*>* rawParts = [cleaned componentsSeparatedByCharactersInSet:
                                             [NSCharacterSet characterSetWithCharactersInString:@", "]];
    NSMutableArray<NSString*>* parts = [[NSMutableArray alloc] initWithCapacity:3];
    for (NSString* part in rawParts) {
        NSString* component = [part stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (component.length > 0) {
            [parts addObject:component];
        }
    }
    if (parts.count != 3) {
        return nil;
    }

    NSInteger channels[3] = {0, 0, 0};
    for (NSInteger index = 0; index < 3; ++index) {
        NSScanner* scanner = [NSScanner scannerWithString:parts[index]];
        int channel = 0;
        if (![scanner scanInt:&channel] || scanner.scanLocation != parts[index].length || channel < 0 || channel > 255) {
            return nil;
        }
        channels[index] = channel;
    }

    return [NSColor colorWithCalibratedRed:channels[0] / 255.0
                                     green:channels[1] / 255.0
                                      blue:channels[2] / 255.0
                                     alpha:1.0];
}

NSString* hexStringFromColor(NSColor* color) {
    NSColor* rgbColor = [color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
    if (rgbColor == nil) {
        rgbColor = color;
    }

    const NSInteger red = std::clamp<NSInteger>(std::lround(rgbColor.redComponent * 255.0), 0, 255);
    const NSInteger green = std::clamp<NSInteger>(std::lround(rgbColor.greenComponent * 255.0), 0, 255);
    const NSInteger blue = std::clamp<NSInteger>(std::lround(rgbColor.blueComponent * 255.0), 0, 255);
    return [NSString stringWithFormat:@"#%02lX%02lX%02lX",
                                      static_cast<long>(red),
                                      static_cast<long>(green),
                                      static_cast<long>(blue)];
}

NSColor* boardLightSquareColor() {
    NSString* value = [[NSUserDefaults standardUserDefaults] stringForKey:BoardLightSquarePreferenceKey];
    NSColor* color = colorFromHexString(value ?: DefaultLightSquareHex);
    return color ?: colorFromHexString(DefaultLightSquareHex);
}

NSColor* boardDarkSquareColor() {
    NSString* value = [[NSUserDefaults standardUserDefaults] stringForKey:BoardDarkSquarePreferenceKey];
    NSColor* color = colorFromHexString(value ?: DefaultDarkSquareHex);
    return color ?: colorFromHexString(DefaultDarkSquareHex);
}

void drawPiece(chess::Piece piece, NSRect squareRect, CGFloat squareSize) {
    NSImage* image = pieceImage(piece);
    if (image == nil) {
        return;
    }

    const CGFloat iconSize = std::floor(squareSize * 0.75);
    const NSRect iconRect = NSMakeRect(NSMidX(squareRect) - (iconSize / 2.0),
                                       NSMidY(squareRect) - (iconSize / 2.0),
                                       iconSize,
                                       iconSize);
    [image drawInRect:iconRect
             fromRect:NSZeroRect
            operation:NSCompositingOperationSourceOver
             fraction:1.0
       respectFlipped:YES
                hints:nil];
}

bool isTerminal(chess::GameStatus status) {
    return status == chess::GameStatus::Checkmate ||
           status == chess::GameStatus::Stalemate ||
           status == chess::GameStatus::FiftyMoveDraw ||
           status == chess::GameStatus::InsufficientMaterialDraw;
}

bool isCheckStatus(chess::GameStatus status) {
    return status == chess::GameStatus::Check ||
           status == chess::GameStatus::Checkmate;
}

chess::Square checkedKingSquare(const chess::ChessGame& game) {
    if (!isCheckStatus(game.status())) {
        return chess::NoSquare;
    }

    const chess::Color checkedColor = game.sideToMove();
    for (chess::Square square = 0; square < 64; ++square) {
        const chess::Piece piece = game.board().pieceAt(square);
        if (!piece.isEmpty() &&
            piece.color == checkedColor &&
            piece.type == chess::PieceType::King) {
            return square;
        }
    }

    return chess::NoSquare;
}

NSTextField* makeLabel(NSString* text, NSFont* font) {
    NSTextField* label = [NSTextField labelWithString:text];
    label.translatesAutoresizingMaskIntoConstraints = NO;
    label.font = font;
    label.lineBreakMode = NSLineBreakByWordWrapping;
    label.maximumNumberOfLines = 0;
    return label;
}

NSButton* makeButton(NSString* title, id target, SEL action) {
    NSButton* button = [NSButton buttonWithTitle:title target:target action:action];
    button.translatesAutoresizingMaskIntoConstraints = NO;
    button.bezelStyle = NSBezelStyleRounded;
    return button;
}

NSPopUpButton* makePopup(NSArray<NSString*>* items, BOOL enabled) {
    NSPopUpButton* popup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    popup.translatesAutoresizingMaskIntoConstraints = NO;
    [popup addItemsWithTitles:items];
    popup.enabled = enabled;
    return popup;
}

NSButton* makeCheckbox(NSString* title, BOOL checked, BOOL enabled) {
    NSButton* checkbox = [NSButton checkboxWithTitle:title target:nil action:nil];
    checkbox.translatesAutoresizingMaskIntoConstraints = NO;
    checkbox.state = checked ? NSControlStateValueOn : NSControlStateValueOff;
    checkbox.enabled = enabled;
    return checkbox;
}

NSArray<NSString*>* botNames(const std::vector<std::unique_ptr<chess::ChessBot>>& bots) {
    NSMutableArray<NSString*>* names = [[NSMutableArray alloc] initWithCapacity:bots.size()];
    for (const auto& bot : bots) {
        [names addObject:toNSString(bot->name())];
    }
    return names;
}

struct BoardMetrics {
    CGFloat squareSize = 0.0;
    CGFloat boardSize = 0.0;
    CGFloat originX = 0.0;
    CGFloat originY = 0.0;
};

BoardMetrics metricsForBounds(NSRect bounds) {
    constexpr CGFloat labelInset = 28.0;
    const CGFloat availableWidth = NSWidth(bounds) - (labelInset * 2.0);
    const CGFloat availableHeight = NSHeight(bounds) - (labelInset * 2.0);
    const CGFloat boardSize = std::floor(std::min(availableWidth, availableHeight));
    const CGFloat squareSize = std::floor(boardSize / 8.0);
    const CGFloat actualBoardSize = squareSize * 8.0;

    return BoardMetrics{
        squareSize,
        actualBoardSize,
        std::floor((NSWidth(bounds) - actualBoardSize) / 2.0),
        std::floor((NSHeight(bounds) - actualBoardSize) / 2.0),
    };
}

NSRect rectForSquare(chess::Square square, BOOL flipped, const BoardMetrics& metrics) {
    const int file = square & 7;
    const int rank = square >> 3;
    const int displayFile = flipped ? 7 - file : file;
    const int displayRank = flipped ? rank : 7 - rank;

    return NSMakeRect(metrics.originX + (displayFile * metrics.squareSize),
                      metrics.originY + (displayRank * metrics.squareSize),
                      metrics.squareSize,
                      metrics.squareSize);
}

chess::Square squareAtPoint(NSPoint point, BOOL flipped, const BoardMetrics& metrics) {
    if (point.x < metrics.originX || point.y < metrics.originY ||
        point.x >= metrics.originX + metrics.boardSize ||
        point.y >= metrics.originY + metrics.boardSize) {
        return chess::NoSquare;
    }

    const int displayFile = static_cast<int>((point.x - metrics.originX) / metrics.squareSize);
    const int displayRank = static_cast<int>((point.y - metrics.originY) / metrics.squareSize);
    const int file = flipped ? 7 - displayFile : displayFile;
    const int rank = flipped ? displayRank : 7 - displayRank;
    return (rank * 8) + file;
}

void drawCheckedKingSquare(const chess::ChessGame& game, BOOL flipped, const BoardMetrics& metrics) {
    const chess::Square square = checkedKingSquare(game);
    if (square == chess::NoSquare) {
        return;
    }

    [[NSColor colorWithCalibratedRed:0.82 green:0.10 blue:0.11 alpha:0.72] setFill];
    NSRectFill(rectForSquare(square, flipped, metrics));
}

void drawLastMoveHighlights(const chess::ChessGame& game, BOOL flipped, const BoardMetrics& metrics) {
    const std::optional<chess::Move> lastMove = game.lastMove();
    if (!lastMove) {
        return;
    }

    NSColor* fillColor = [NSColor colorWithCalibratedRed:0.95 green:0.78 blue:0.24 alpha:0.48];
    NSColor* strokeColor = [NSColor colorWithCalibratedRed:0.95 green:0.68 blue:0.12 alpha:0.85];
    for (const chess::Square square : {lastMove->from, lastMove->to}) {
        if (square == chess::NoSquare) {
            continue;
        }

        const NSRect squareRect = rectForSquare(square, flipped, metrics);
        [fillColor setFill];
        NSRectFill(squareRect);

        NSBezierPath* outline = [NSBezierPath bezierPathWithRect:NSInsetRect(squareRect, 2.0, 2.0)];
        [strokeColor setStroke];
        outline.lineWidth = 2.0;
        [outline stroke];
    }
}

void drawLegalMoveDots(const chess::ChessGame& game,
                       chess::Square selectedSquare,
                       BOOL flipped,
                       const BoardMetrics& metrics) {
    if (selectedSquare == chess::NoSquare || isTerminal(game.status())) {
        return;
    }

    const chess::Piece selectedPiece = game.board().pieceAt(selectedSquare);
    if (selectedPiece.isEmpty() || selectedPiece.color != game.sideToMove()) {
        return;
    }

    bool drawn[64] = {};
    [[NSColor colorWithCalibratedWhite:0.05 alpha:0.30] setFill];

    for (const chess::Move& move : game.board().generateLegalMoves()) {
        if (move.from != selectedSquare || move.to < 0 || move.to >= 64 || drawn[move.to]) {
            continue;
        }

        drawn[move.to] = true;
        const NSRect squareRect = rectForSquare(move.to, flipped, metrics);
        const CGFloat dotSize = metrics.squareSize * 0.22;
        const NSRect dotRect = NSMakeRect(NSMidX(squareRect) - (dotSize / 2.0),
                                          NSMidY(squareRect) - (dotSize / 2.0),
                                          dotSize,
                                          dotSize);
        [[NSBezierPath bezierPathWithOvalInRect:dotRect] fill];
    }
}

std::vector<chess::Move> legalMoveCandidates(const chess::Board& board,
                                             chess::Square from,
                                             chess::Square to) {
    std::vector<chess::Move> candidates;
    for (const chess::Move& move : board.generateLegalMoves()) {
        if (move.from == from && move.to == to) {
            candidates.push_back(move);
        }
    }
    return candidates;
}

bool requiresPromotionChoice(const std::vector<chess::Move>& candidates) {
    return std::any_of(candidates.begin(), candidates.end(), [](const chess::Move& move) {
        return move.isPromotion();
    });
}

std::optional<chess::Move> chooseCandidate(const std::vector<chess::Move>& candidates,
                                           chess::PieceType promotion) {
    std::optional<chess::Move> queenFallback;
    for (const chess::Move& move : candidates) {
        if (!move.isPromotion()) {
            return move;
        }
        if (move.promotion == promotion) {
            return move;
        }
        if (move.promotion == chess::PieceType::Queen) {
            queenFallback = move;
        }
    }
    return queenFallback;
}

NSString* promotionTitle(chess::PieceType type) {
    switch (type) {
    case chess::PieceType::Queen:
        return @"Queen";
    case chess::PieceType::Rook:
        return @"Rook";
    case chess::PieceType::Bishop:
        return @"Bishop";
    case chess::PieceType::Knight:
        return @"Knight";
    case chess::PieceType::Pawn:
    case chess::PieceType::King:
    case chess::PieceType::None:
        return @"Queen";
    }
    return @"Queen";
}

CGFloat evaluationShareForCentipawns(int centipawns) {
    return std::clamp<CGFloat>(0.5 + (0.5 * std::tanh(static_cast<double>(centipawns) / 600.0)),
                               0.04,
                               0.96);
}

NSString* evaluationTextForCentipawns(int centipawns) {
    const double pawns = std::abs(centipawns) < 5 ? 0.0 : static_cast<double>(centipawns) / 100.0;
    return [NSString stringWithFormat:@"%+.2f", pawns];
}

NSString* evaluationText(const chess::Evaluation& evaluation) {
    if (evaluation.forcedMate) {
        if (evaluation.forcedMate->moves == 0) {
            return [NSString stringWithFormat:@"%@ checkmate", colorToNSString(evaluation.forcedMate->winner)];
        }
        return [NSString stringWithFormat:@"Mate in %d", evaluation.forcedMate->moves];
    }

    return evaluationTextForCentipawns(evaluation.centipawns);
}

int mateSearchPlyForTick(int tick) {
    if (tick == 0) {
        return 3;
    }
    if (tick < 5) {
        return 5;
    }
    return 7;
}

NSColor* evaluationColor(chess::Color color) {
    return color == chess::Color::White
               ? [NSColor colorWithCalibratedWhite:0.96 alpha:1.0]
               : [NSColor colorWithCalibratedWhite:0.08 alpha:1.0];
}

constexpr auto MinimumBotMoveDelay = std::chrono::milliseconds(150);

int64_t delayNanosecondsUntil(Clock::time_point deadline) {
    const auto remaining = deadline - Clock::now();
    if (remaining <= Clock::duration::zero()) {
        return 0;
    }
    return std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count();
}

chess::PieceType promotionPieceForIndex(NSInteger index) {
    switch (index) {
    case 1:
        return chess::PieceType::Rook;
    case 2:
        return chess::PieceType::Bishop;
    case 3:
        return chess::PieceType::Knight;
    case 0:
    default:
        return chess::PieceType::Queen;
    }
}

NSInteger indexForPromotionPiece(chess::PieceType piece) {
    switch (piece) {
    case chess::PieceType::Rook:
        return 1;
    case chess::PieceType::Bishop:
        return 2;
    case chess::PieceType::Knight:
        return 3;
    case chess::PieceType::Queen:
    case chess::PieceType::Pawn:
    case chess::PieceType::King:
    case chess::PieceType::None:
        return 0;
    }
    return 0;
}

NSInteger normalizedThemePreference(NSInteger index) {
    if (index < 0 || index > 2) {
        return 0;
    }
    return index;
}

NSInteger normalizedStartModePreference(NSInteger index) {
    if (index < 0 || index > 2) {
        return 1;
    }
    return index;
}

NSInteger normalizedStartColorPreference(NSInteger index) {
    return index == 1 ? 1 : 0;
}

chess::PieceType customizablePieceTypeAtIndex(NSInteger index) {
    switch (index) {
    case 0:
        return chess::PieceType::King;
    case 1:
        return chess::PieceType::Queen;
    case 2:
        return chess::PieceType::Rook;
    case 3:
        return chess::PieceType::Bishop;
    case 4:
        return chess::PieceType::Knight;
    case 5:
    default:
        return chess::PieceType::Pawn;
    }
}

chess::Piece pieceForCustomizationIndex(NSInteger index) {
    const BOOL black = index >= 6;
    const NSInteger pieceIndex = std::clamp<NSInteger>(black ? index - 6 : index, 0, 5);
    return chess::Piece{
        black ? chess::Color::Black : chess::Color::White,
        customizablePieceTypeAtIndex(pieceIndex),
    };
}

NSInteger customizationIndexForPiece(chess::Piece piece) {
    NSInteger index = 5;
    switch (piece.type) {
    case chess::PieceType::King:
        index = 0;
        break;
    case chess::PieceType::Queen:
        index = 1;
        break;
    case chess::PieceType::Rook:
        index = 2;
        break;
    case chess::PieceType::Bishop:
        index = 3;
        break;
    case chess::PieceType::Knight:
        index = 4;
        break;
    case chess::PieceType::Pawn:
    case chess::PieceType::None:
        index = 5;
        break;
    }
    return piece.color == chess::Color::Black ? index + 6 : index;
}

int bitCount(chess::Bitboard board) {
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

NSString* pieceTypeName(chess::PieceType type) {
    switch (type) {
    case chess::PieceType::King:
        return @"King";
    case chess::PieceType::Queen:
        return @"Queen";
    case chess::PieceType::Rook:
        return @"Rook";
    case chess::PieceType::Bishop:
        return @"Bishop";
    case chess::PieceType::Knight:
        return @"Knight";
    case chess::PieceType::Pawn:
        return @"Pawn";
    case chess::PieceType::None:
        return @"None";
    }
    return @"None";
}

NSString* analysisPieceTitle(chess::Piece piece) {
    return [NSString stringWithFormat:@"%@ %@",
                                      colorToNSString(piece.color),
                                      pieceTypeName(piece.type)];
}

bool hasExactlyOneKingPerSide(const chess::Board& board) {
    return bitCount(board.pieces(chess::Color::White, chess::PieceType::King)) == 1 &&
           bitCount(board.pieces(chess::Color::Black, chess::PieceType::King)) == 1;
}

std::string formatAnalysisScore(int centipawns) {
    const double pawns = std::abs(centipawns) < 5 ? 0.0 : static_cast<double>(centipawns) / 100.0;
    std::ostringstream output;
    output << std::showpos << std::fixed << std::setprecision(2) << pawns;
    return output.str();
}

struct AnalysisMoveLine {
    std::string notation;
    int score = 0;
};

std::vector<AnalysisMoveLine> bestMoveLines(const chess::Board& board, std::size_t limit) {
    constexpr int MateScore = 100'000'000;
    const chess::Color perspective = board.sideToMove();
    const std::vector<chess::Move> legalMoves = board.generateLegalMoves();
    std::vector<AnalysisMoveLine> lines;
    lines.reserve(legalMoves.size());

    for (const chess::Move& move : legalMoves) {
        const std::string notation = chess::formatAlgebraic(board, move);
        chess::Board afterMove = board;
        afterMove.makeMove(move);

        int score = chess::evaluateBoard(afterMove, perspective);
        const std::vector<chess::Move> replies = afterMove.generateLegalMoves();
        if (replies.empty()) {
            score = afterMove.isKingInCheck(afterMove.sideToMove()) ? MateScore : 0;
        }

        lines.push_back(AnalysisMoveLine{notation, score});
    }

    std::stable_sort(
        lines.begin(),
        lines.end(),
        [](const AnalysisMoveLine& lhs, const AnalysisMoveLine& rhs) {
            if (lhs.score != rhs.score) {
                return lhs.score > rhs.score;
            }
            return lhs.notation < rhs.notation;
        }
    );

    if (lines.size() > limit) {
        lines.resize(limit);
    }
    return lines;
}

NSString* bestMovesText(const chess::Board& board) {
    if (!hasExactlyOneKingPerSide(board)) {
        return @"Place one king for each side.";
    }

    const std::vector<chess::Move> legalMoves = board.generateLegalMoves();
    if (legalMoves.empty()) {
        return board.isKingInCheck(board.sideToMove()) ? @"Checkmate." : @"Stalemate.";
    }

    const std::vector<AnalysisMoveLine> lines = bestMoveLines(board, 3);
    NSMutableArray<NSString*>* rows = [[NSMutableArray alloc] initWithCapacity:lines.size()];
    for (std::size_t index = 0; index < lines.size(); ++index) {
        NSString* score = lines[index].score > 10'000'000
                              ? @"Mate"
                              : toNSString(formatAnalysisScore(lines[index].score));
        [rows addObject:[NSString stringWithFormat:@"%lu. %@  %@",
                                                   static_cast<unsigned long>(index + 1),
                                                   toNSString(lines[index].notation),
                                                   score]];
    }
    return [rows componentsJoinedByString:@"\n"];
}

} // namespace

@class AnalysisPiecePaletteView;
@class ChessBoardView;

@protocol AnalysisPiecePaletteViewDelegate <NSObject>
- (void)analysisPaletteView:(AnalysisPiecePaletteView*)paletteView didSelectPiece:(chess::Piece)piece;
@end

@interface AnalysisPiecePaletteView : NSView
@property(nonatomic, weak) id<AnalysisPiecePaletteViewDelegate> delegate;
@property(nonatomic) chess::Piece selectedPiece;
@end

@implementation AnalysisPiecePaletteView

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self) {
        _delegate = nil;
        _selectedPiece = chess::Piece{chess::Color::White, chess::PieceType::Pawn};
    }
    return self;
}

- (BOOL)isFlipped {
    return YES;
}

- (chess::Piece)pieceAtColumn:(NSInteger)column row:(NSInteger)row {
    static constexpr chess::PieceType types[] = {
        chess::PieceType::King,
        chess::PieceType::Queen,
        chess::PieceType::Rook,
        chess::PieceType::Bishop,
        chess::PieceType::Knight,
        chess::PieceType::Pawn,
    };

    if (column < 0 || column >= 6 || row < 0 || row >= 2) {
        return {};
    }

    return chess::Piece{
        row == 0 ? chess::Color::White : chess::Color::Black,
        types[column],
    };
}

- (void)setSelectedPiece:(chess::Piece)selectedPiece {
    _selectedPiece = selectedPiece;
    self.needsDisplay = YES;
}

- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];

    [[NSColor windowBackgroundColor] setFill];
    NSRectFill(dirtyRect);

    const CGFloat cellWidth = NSWidth(self.bounds) / 6.0;
    const CGFloat cellHeight = NSHeight(self.bounds) / 2.0;
    const CGFloat pieceSize = std::min(cellWidth, cellHeight);
    NSColor* lightSquare = boardLightSquareColor();
    NSColor* darkSquare = boardDarkSquareColor();

    for (NSInteger row = 0; row < 2; ++row) {
        for (NSInteger column = 0; column < 6; ++column) {
            const NSRect cell = NSMakeRect(column * cellWidth, row * cellHeight, cellWidth, cellHeight);
            NSColor* squareColor = ((row + column) % 2 == 0) ? lightSquare : darkSquare;
            [squareColor setFill];
            NSRectFill(cell);

            const chess::Piece piece = [self pieceAtColumn:column row:row];
            drawPiece(piece, cell, pieceSize);

            if (piece.color == self.selectedPiece.color && piece.type == self.selectedPiece.type) {
                NSBezierPath* selection = [NSBezierPath bezierPathWithRect:NSInsetRect(cell, 3.0, 3.0)];
                [[NSColor keyboardFocusIndicatorColor] setStroke];
                selection.lineWidth = 3.0;
                [selection stroke];
            }
        }
    }

    NSBezierPath* border = [NSBezierPath bezierPathWithRect:NSInsetRect(self.bounds, 0.5, 0.5)];
    [[NSColor separatorColor] setStroke];
    border.lineWidth = 1.0;
    [border stroke];
}

- (void)mouseDown:(NSEvent*)event {
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    if (!NSPointInRect(point, self.bounds)) {
        return;
    }

    const CGFloat cellWidth = NSWidth(self.bounds) / 6.0;
    const CGFloat cellHeight = NSHeight(self.bounds) / 2.0;
    const NSInteger column = std::clamp<NSInteger>(static_cast<NSInteger>(point.x / cellWidth), 0, 5);
    const NSInteger row = std::clamp<NSInteger>(static_cast<NSInteger>(point.y / cellHeight), 0, 1);
    self.selectedPiece = [self pieceAtColumn:column row:row];
    [self.delegate analysisPaletteView:self didSelectPiece:self.selectedPiece];
}

@end

@protocol ChessBoardViewDelegate <NSObject>
- (BOOL)boardView:(ChessBoardView*)boardView canSelectSquare:(chess::Square)square;
- (BOOL)boardView:(ChessBoardView*)boardView tryMoveFrom:(chess::Square)from to:(chess::Square)to;
- (void)boardView:(ChessBoardView*)boardView placeAnalysisPieceAtSquare:(chess::Square)square;
- (void)boardView:(ChessBoardView*)boardView removeAnalysisPieceAtSquare:(chess::Square)square;
@end

@interface ChessBoardView : NSView
@property(nonatomic, assign) const chess::ChessGame* game;
@property(nonatomic, assign) const chess::Board* position;
@property(nonatomic, weak) id<ChessBoardViewDelegate> delegate;
@property(nonatomic) BOOL boardFlipped;
@property(nonatomic) chess::Square selectedSquare;
- (void)clearSelection;
@end

@implementation ChessBoardView

- (instancetype)initWithFrame:(NSRect)frameRect {
    self = [super initWithFrame:frameRect];
    if (self) {
        _game = nullptr;
        _position = nullptr;
        _delegate = nil;
        _boardFlipped = NO;
        _selectedSquare = chess::NoSquare;
    }
    return self;
}

- (BOOL)isFlipped {
    return YES;
}

- (void)clearSelection {
    self.selectedSquare = chess::NoSquare;
    self.needsDisplay = YES;
}

- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];

    [[NSColor windowBackgroundColor] setFill];
    NSRectFill(dirtyRect);

    const BoardMetrics metrics = metricsForBounds(self.bounds);
    const NSRect boardRect = NSMakeRect(metrics.originX, metrics.originY, metrics.boardSize, metrics.boardSize);

    NSBezierPath* border = [NSBezierPath bezierPathWithRect:NSInsetRect(boardRect, -1.0, -1.0)];
    [[NSColor controlTextColor] setStroke];
    border.lineWidth = 2.0;
    [border stroke];

    NSColor* lightSquare = boardLightSquareColor();
    NSColor* darkSquare = boardDarkSquareColor();
    NSColor* gridColor = [NSColor colorWithCalibratedWhite:0.16 alpha:1.0];
    const chess::Board* board = self.game != nullptr ? &self.game->board() : self.position;
    NSImage* boardImage = boardBackgroundImage();

    if (boardImage != nil) {
        [boardImage drawInRect:boardRect
                      fromRect:NSZeroRect
                     operation:NSCompositingOperationSourceOver
                      fraction:1.0
                respectFlipped:YES
                         hints:nil];
    } else {
        for (NSInteger rank = 0; rank < 8; ++rank) {
            for (NSInteger file = 0; file < 8; ++file) {
                NSColor* squareColor = ((rank + file) % 2 == 0) ? lightSquare : darkSquare;
                [squareColor setFill];
                NSRectFill(NSMakeRect(metrics.originX + (file * metrics.squareSize),
                                      metrics.originY + (rank * metrics.squareSize),
                                      metrics.squareSize,
                                      metrics.squareSize));
            }
        }
    }

    if (self.game != nullptr) {
        drawLastMoveHighlights(*self.game, self.boardFlipped, metrics);
        drawCheckedKingSquare(*self.game, self.boardFlipped, metrics);
    }

    [gridColor setStroke];
    for (NSInteger index = 0; index <= 8; ++index) {
        const CGFloat offset = index * metrics.squareSize;

        NSBezierPath* vertical = [NSBezierPath bezierPath];
        [vertical moveToPoint:NSMakePoint(metrics.originX + offset, metrics.originY)];
        [vertical lineToPoint:NSMakePoint(metrics.originX + offset, metrics.originY + metrics.boardSize)];
        vertical.lineWidth = 1.0;
        [vertical stroke];

        NSBezierPath* horizontal = [NSBezierPath bezierPath];
        [horizontal moveToPoint:NSMakePoint(metrics.originX, metrics.originY + offset)];
        [horizontal lineToPoint:NSMakePoint(metrics.originX + metrics.boardSize, metrics.originY + offset)];
        horizontal.lineWidth = 1.0;
        [horizontal stroke];
    }

    if (self.selectedSquare != chess::NoSquare) {
        NSRect selectedRect = NSInsetRect(rectForSquare(self.selectedSquare, self.boardFlipped, metrics), 3.0, 3.0);
        NSBezierPath* selection = [NSBezierPath bezierPathWithRect:selectedRect];
        [[NSColor keyboardFocusIndicatorColor] setStroke];
        selection.lineWidth = 4.0;
        [selection stroke];
    }

    if (board != nullptr) {
        for (chess::Square square = 0; square < 64; ++square) {
            const chess::Piece piece = board->pieceAt(square);
            drawPiece(piece, rectForSquare(square, self.boardFlipped, metrics), metrics.squareSize);
        }
    }

    if (self.game != nullptr) {
        drawLegalMoveDots(*self.game, self.selectedSquare, self.boardFlipped, metrics);
    }

    NSDictionary<NSAttributedStringKey, id>* labelAttributes = @{
        NSFontAttributeName: [NSFont monospacedDigitSystemFontOfSize:13.0 weight:NSFontWeightMedium],
        NSForegroundColorAttributeName: [NSColor secondaryLabelColor],
    };

    for (NSInteger file = 0; file < 8; ++file) {
        const NSInteger boardFile = self.boardFlipped ? 7 - file : file;
        NSString* fileLabel = [NSString stringWithFormat:@"%c", static_cast<char>('a' + boardFile)];
        const NSSize size = [fileLabel sizeWithAttributes:labelAttributes];
        const CGFloat x = metrics.originX + (file * metrics.squareSize) + ((metrics.squareSize - size.width) / 2.0);
        [fileLabel drawAtPoint:NSMakePoint(x, metrics.originY + metrics.boardSize + 7.0)
                withAttributes:labelAttributes];
    }

    for (NSInteger rank = 0; rank < 8; ++rank) {
        const NSInteger boardRank = self.boardFlipped ? rank + 1 : 8 - rank;
        NSString* rankLabel = [NSString stringWithFormat:@"%ld", boardRank];
        const NSSize size = [rankLabel sizeWithAttributes:labelAttributes];
        const CGFloat y = metrics.originY + (rank * metrics.squareSize) + ((metrics.squareSize - size.height) / 2.0);
        [rankLabel drawAtPoint:NSMakePoint(metrics.originX - size.width - 9.0, y)
                withAttributes:labelAttributes];
    }
}

- (void)mouseDown:(NSEvent*)event {
    if ((self.game == nullptr && self.position == nullptr) || self.delegate == nil) {
        return;
    }

    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    const chess::Square square = squareAtPoint(point, self.boardFlipped, metricsForBounds(self.bounds));
    if (square == chess::NoSquare) {
        return;
    }

    if (self.position != nullptr && self.game == nullptr) {
        if ((event.modifierFlags & NSEventModifierFlagControl) != 0) {
            [self.delegate boardView:self removeAnalysisPieceAtSquare:square];
        } else {
            [self.delegate boardView:self placeAnalysisPieceAtSquare:square];
        }
        return;
    }

    if (self.selectedSquare == chess::NoSquare) {
        if ([self.delegate boardView:self canSelectSquare:square]) {
            self.selectedSquare = square;
            self.needsDisplay = YES;
        }
        return;
    }

    if (square == self.selectedSquare) {
        [self clearSelection];
        return;
    }

    if ([self.delegate boardView:self tryMoveFrom:self.selectedSquare to:square]) {
        [self clearSelection];
        return;
    }

    if ([self.delegate boardView:self canSelectSquare:square]) {
        self.selectedSquare = square;
        self.needsDisplay = YES;
    }
}

- (void)rightMouseDown:(NSEvent*)event {
    if (self.position == nullptr || self.delegate == nil) {
        return;
    }

    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    const chess::Square square = squareAtPoint(point, self.boardFlipped, metricsForBounds(self.bounds));
    if (square != chess::NoSquare) {
        [self.delegate boardView:self removeAnalysisPieceAtSquare:square];
    }
}

@end

@interface ChessAppDelegate : NSObject <NSApplicationDelegate, AnalysisPiecePaletteViewDelegate, ChessBoardViewDelegate> {
    std::optional<chess::ChessGame> game_;
    std::optional<chess::ChessGame> viewedGame_;
    chess::Board analysisBoard_;
    chess::Piece selectedAnalysisPiece_;
    std::vector<std::unique_ptr<chess::ChessBot>> bots_;
    const chess::ChessBot* activeBot_;
    std::vector<chess::Move> pendingPromotionCandidates_;
    std::size_t historyCursor_;
    BOOL analysisMode_;
    BOOL botMode_;
    chess::Color humanColor_;
    chess::Color botColor_;
    BOOL evaluationVisible_;
    chess::Color evaluationPerspective_;
    int evaluationGeneration_;
    int botMoveGeneration_;
    BOOL autoFlipEnabled_;
    NSInteger appearanceMode_;
    BOOL autopromoteEnabled_;
    chess::PieceType autopromotePiece_;
    BOOL promotionPending_;
    BOOL gameEnded_;
}
@property(strong) NSWindow* window;
@property(strong) NSView* setupView;
@property(strong) NSView* settingsView;
@property(strong) NSView* gameView;
@property(strong) ChessBoardView* boardView;
@property(strong) NSTextField* statusLabel;
@property(strong) NSTextField* messageLabel;
@property(strong) NSView* promotionChoiceView;
@property(strong) NSLayoutConstraint* promotionChoiceHeightConstraint;
@property(strong) NSPopUpButton* modePopup;
@property(strong) NSPopUpButton* botPopup;
@property(strong) NSSegmentedControl* colorControl;
@property(strong) NSButton* saveCheckbox;
@property(strong) NSButton* evaluationCheckbox;
@property(strong) NSPopUpButton* themePopup;
@property(strong) NSButton* autopromoteCheckbox;
@property(strong) NSPopUpButton* autopromotePopup;
@property(strong) NSTextField* lightSquareField;
@property(strong) NSTextField* darkSquareField;
@property(strong) NSColorWell* lightSquareWell;
@property(strong) NSColorWell* darkSquareWell;
@property(strong) NSTextField* botLabel;
@property(strong) NSTextField* colorLabel;
@property(strong) NSTextField* botDescriptionLabel;
@property(strong) NSView* evaluationPanel;
@property(strong) NSTextField* evaluationTitleLabel;
@property(strong) NSTextField* evaluationValueLabel;
@property(strong) NSView* evaluationTrack;
@property(strong) NSView* evaluationPlayerFill;
@property(strong) NSLayoutConstraint* evaluationPlayerFillWidthConstraint;
@property(strong) NSLayoutConstraint* evaluationPanelHeightConstraint;
@property(strong) NSButton* evaluationToggleButton;
@property(strong) NSButton* flipBoardButton;
@property(strong) NSButton* autoFlipCheckbox;
@property(strong) NSButton* historyBackButton;
@property(strong) NSButton* historyForwardButton;
@property(strong) NSButton* historyLatestButton;
@property(strong) NSSegmentedControl* analysisSideControl;
@property(strong) AnalysisPiecePaletteView* analysisPaletteView;
@property(strong) NSTextField* bestMovesLabel;
@property(copy) NSString* currentSummary;
@property(copy) NSString* gameOverMessage;
@end

@implementation ChessAppDelegate

- (instancetype)init {
    self = [super init];
    if (self) {
        bots_ = chess::createDefaultBots();
        analysisBoard_ = chess::Board::standard();
        selectedAnalysisPiece_ = chess::Piece{chess::Color::White, chess::PieceType::Pawn};
        activeBot_ = nullptr;
        historyCursor_ = 0;
        analysisMode_ = NO;
        botMode_ = NO;
        humanColor_ = chess::Color::White;
        botColor_ = chess::Color::Black;
        evaluationVisible_ = NO;
        evaluationPerspective_ = chess::Color::White;
        evaluationGeneration_ = 0;
        botMoveGeneration_ = 0;
        autoFlipEnabled_ = NO;
        NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
        appearanceMode_ = normalizedThemePreference([defaults integerForKey:ThemePreferenceKey]);
        autopromoteEnabled_ = [defaults boolForKey:AutopromoteEnabledPreferenceKey];
        autopromotePiece_ = promotionPieceForIndex([defaults integerForKey:AutopromotePiecePreferenceKey]);
        promotionPending_ = NO;
        gameEnded_ = NO;
    }
    return self;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;

    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [self buildMenu];
    [self applyAppearancePreference];

    NSRect frame = NSMakeRect(0, 0, 940, 640);
    self.window = [[NSWindow alloc] initWithContentRect:frame
                                               styleMask:NSWindowStyleMaskTitled |
                                                         NSWindowStyleMaskClosable |
                                                         NSWindowStyleMaskMiniaturizable |
                                                         NSWindowStyleMaskResizable
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
    self.window.title = @"Chess";
    self.window.minSize = NSMakeSize(780, 560);
    [self.window center];
    [self showSetupView];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

- (void)buildMenu {
    NSMenu* mainMenu = [[NSMenu alloc] initWithTitle:@""];
    NSMenuItem* appMenuItem = [[NSMenuItem alloc] initWithTitle:@""
                                                         action:nil
                                                  keyEquivalent:@""];
    NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"Chess"];
    [appMenu addItemWithTitle:@"Quit Chess"
                       action:@selector(terminate:)
                keyEquivalent:@"q"];
    appMenuItem.submenu = appMenu;
    [mainMenu addItem:appMenuItem];
    NSApp.mainMenu = mainMenu;
}

- (void)applyAppearancePreference {
    NSAppearance* preferredAppearance = nil;
    switch (appearanceMode_) {
    case 1:
        preferredAppearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
        break;
    case 2:
        preferredAppearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
        break;
    case 0:
    default:
        preferredAppearance = nil;
        break;
    }

    NSApp.appearance = preferredAppearance;
    self.window.appearance = preferredAppearance;
    [self.window.contentView setNeedsDisplay:YES];
}

- (void)refreshAppearanceViews {
    clearImageCache();
    self.boardView.needsDisplay = YES;
    self.analysisPaletteView.needsDisplay = YES;
}

- (void)showSettingsAlertWithTitle:(NSString*)title message:(NSString*)message {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = title;
    alert.informativeText = message;
    [alert addButtonWithTitle:@"OK"];
    [alert beginSheetModalForWindow:self.window completionHandler:nil];
}

- (NSURL*)choosePNGURL {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    panel.allowedFileTypes = @[ @"png" ];
#pragma clang diagnostic pop
    return [panel runModal] == NSModalResponseOK ? panel.URL : nil;
}

- (const chess::ChessGame*)displayedGame {
    if (viewedGame_) {
        return &(*viewedGame_);
    }
    return game_ ? &(*game_) : nullptr;
}

- (BOOL)isViewingLatestPosition {
    return !game_ || historyCursor_ >= game_->moveCount();
}

- (void)clampHistoryCursor {
    historyCursor_ = game_ ? std::min(historyCursor_, game_->moveCount()) : 0;
}

- (NSString*)historyPositionMessage {
    if (!game_) {
        return @"";
    }
    if ([self isViewingLatestPosition]) {
        return @"Viewing latest position.";
    }

    return [NSString stringWithFormat:@"Viewing position %lu of %lu.",
                                      static_cast<unsigned long>(historyCursor_),
                                      static_cast<unsigned long>(game_->moveCount())];
}

- (void)updateHistoryButtons {
    [self clampHistoryCursor];

    const BOOL canBrowse = game_.has_value() && !promotionPending_;
    const std::size_t latest = game_ ? game_->moveCount() : 0;
    self.historyBackButton.enabled = canBrowse && historyCursor_ > 0;
    self.historyForwardButton.enabled = canBrowse && historyCursor_ < latest;
    self.historyLatestButton.enabled = canBrowse && historyCursor_ < latest;
}

- (void)syncHistoryBoardDisplay {
    [self clampHistoryCursor];

    if (game_ && ![self isViewingLatestPosition]) {
        viewedGame_.emplace(game_->snapshotAtPly(historyCursor_));
    } else {
        viewedGame_.reset();
    }

    if (self.boardView != nil) {
        self.boardView.game = [self displayedGame];
        [self.boardView clearSelection];
        [self applyAutoFlipForCurrentTurn];
        self.boardView.needsDisplay = YES;
    }

    [self updateHistoryButtons];
}

- (void)goToHistoryPly:(std::size_t)ply {
    if (!game_ || promotionPending_) {
        return;
    }

    historyCursor_ = std::min(ply, game_->moveCount());
    [self syncHistoryBoardDisplay];
    [self updateStatusLabel];
    [self setGameMessage:[self historyPositionMessage]];
    [self restartEvaluationLoop];
}

- (void)historyBack:(id)sender {
    (void)sender;
    if (!game_ || historyCursor_ == 0) {
        return;
    }

    [self goToHistoryPly:historyCursor_ - 1];
}

- (void)historyForward:(id)sender {
    (void)sender;
    if (!game_) {
        return;
    }

    [self goToHistoryPly:historyCursor_ + 1];
}

- (void)historyLatest:(id)sender {
    (void)sender;
    if (!game_) {
        return;
    }

    [self goToHistoryPly:game_->moveCount()];
}

- (const chess::ChessBot*)selectedBot {
    const NSInteger selectedIndex = self.botPopup.indexOfSelectedItem;
    if (selectedIndex >= 0 && static_cast<std::size_t>(selectedIndex) < bots_.size()) {
        return bots_[static_cast<std::size_t>(selectedIndex)].get();
    }

    return bots_.empty() ? nullptr : bots_.front().get();
}

- (NSInteger)savedStartModeIndex {
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    return [defaults objectForKey:StartModePreferenceKey] == nil
               ? 1
               : normalizedStartModePreference([defaults integerForKey:StartModePreferenceKey]);
}

- (NSInteger)savedStartBotIndex {
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    const NSInteger botCount = static_cast<NSInteger>(bots_.size());
    if ([defaults objectForKey:StartBotPreferenceKey] == nil || botCount <= 0) {
        return 0;
    }
    return std::clamp<NSInteger>([defaults integerForKey:StartBotPreferenceKey], 0, botCount - 1);
}

- (NSInteger)savedStartColorIndex {
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    return [defaults objectForKey:StartColorPreferenceKey] == nil
               ? 0
               : normalizedStartColorPreference([defaults integerForKey:StartColorPreferenceKey]);
}

- (void)saveStartMenuPreferences {
    if (self.modePopup == nil) {
        return;
    }

    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    [defaults setInteger:normalizedStartModePreference(self.modePopup.indexOfSelectedItem)
                  forKey:StartModePreferenceKey];

    if (self.botPopup != nil) {
        [defaults setInteger:std::max<NSInteger>(self.botPopup.indexOfSelectedItem, 0)
                      forKey:StartBotPreferenceKey];
    }
    if (self.colorControl != nil) {
        [defaults setInteger:normalizedStartColorPreference(self.colorControl.selectedSegment)
                      forKey:StartColorPreferenceKey];
    }
    if (self.saveCheckbox != nil) {
        [defaults setBool:self.saveCheckbox.state == NSControlStateValueOn
                   forKey:StartSavePreferenceKey];
    }
    if (self.evaluationCheckbox != nil) {
        [defaults setBool:self.evaluationCheckbox.state == NSControlStateValueOn
                   forKey:StartEvaluationPreferenceKey];
    }
}

- (void)updateBotDescription {
    if (self.botDescriptionLabel == nil) {
        return;
    }

    const chess::ChessBot* bot = [self selectedBot];
    self.botDescriptionLabel.stringValue = bot == nullptr ? @"" : toNSString(bot->description());
}

- (void)showSetupView {
    NSView* root = [[NSView alloc] initWithFrame:self.window.contentView.bounds];
    root.translatesAutoresizingMaskIntoConstraints = NO;
    self.setupView = root;
    self.window.contentView = root;

    NSTextField* title = makeLabel(@"Chess", [NSFont systemFontOfSize:30.0 weight:NSFontWeightSemibold]);
    NSTextField* subtitle = makeLabel(@"Set up a game", [NSFont systemFontOfSize:15.0 weight:NSFontWeightRegular]);
    subtitle.textColor = [NSColor secondaryLabelColor];

    NSTextField* modeLabel = makeLabel(@"Mode", [NSFont systemFontOfSize:13.0 weight:NSFontWeightMedium]);
    self.modePopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    self.modePopup.translatesAutoresizingMaskIntoConstraints = NO;
    [self.modePopup addItemsWithTitles:@[ @"2 player", @"Bots", @"Analysis" ]];
    [self.modePopup selectItemAtIndex:[self savedStartModeIndex]];
    self.modePopup.target = self;
    self.modePopup.action = @selector(modeChanged:);

    self.botLabel = makeLabel(@"Bot", [NSFont systemFontOfSize:13.0 weight:NSFontWeightMedium]);
    self.botPopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    self.botPopup.translatesAutoresizingMaskIntoConstraints = NO;
    [self.botPopup addItemsWithTitles:botNames(bots_)];
    [self.botPopup selectItemAtIndex:[self savedStartBotIndex]];
    self.botPopup.target = self;
    self.botPopup.action = @selector(botChanged:);

    self.botDescriptionLabel = makeLabel(@"", [NSFont systemFontOfSize:12.0 weight:NSFontWeightRegular]);
    self.botDescriptionLabel.textColor = [NSColor secondaryLabelColor];

    self.colorLabel = makeLabel(@"Your color", [NSFont systemFontOfSize:13.0 weight:NSFontWeightMedium]);
    self.colorControl = [[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
    self.colorControl.translatesAutoresizingMaskIntoConstraints = NO;
    self.colorControl.segmentCount = 2;
    [self.colorControl setLabel:@"White" forSegment:0];
    [self.colorControl setLabel:@"Black" forSegment:1];
    self.colorControl.selectedSegment = [self savedStartColorIndex];
    self.colorControl.segmentStyle = NSSegmentStyleRounded;
    self.colorControl.target = self;
    self.colorControl.action = @selector(startMenuPreferenceChanged:);

    self.saveCheckbox = [NSButton checkboxWithTitle:@"Save this game"
                                             target:self
                                             action:@selector(startMenuPreferenceChanged:)];
    self.saveCheckbox.translatesAutoresizingMaskIntoConstraints = NO;
    self.saveCheckbox.state = [[NSUserDefaults standardUserDefaults] boolForKey:StartSavePreferenceKey]
                                  ? NSControlStateValueOn
                                  : NSControlStateValueOff;

    self.evaluationCheckbox = [NSButton checkboxWithTitle:@"Show evaluation"
                                                   target:self
                                                   action:@selector(startMenuPreferenceChanged:)];
    self.evaluationCheckbox.translatesAutoresizingMaskIntoConstraints = NO;
    self.evaluationCheckbox.state = [[NSUserDefaults standardUserDefaults] boolForKey:StartEvaluationPreferenceKey]
                                        ? NSControlStateValueOn
                                        : NSControlStateValueOff;

    NSButton* startButton = makeButton(@"Start Game", self, @selector(startGame:));
    startButton.keyEquivalent = @"\r";
    NSButton* settingsButton = makeButton(@"Settings", self, @selector(showSettingsView:));
    NSButton* quitButton = makeButton(@"Quit", NSApp, @selector(terminate:));

    NSArray<NSView*>* views = @[
        title,
        subtitle,
        modeLabel,
        self.modePopup,
        self.botLabel,
        self.botPopup,
        self.botDescriptionLabel,
        self.colorLabel,
        self.colorControl,
        self.saveCheckbox,
        self.evaluationCheckbox,
        startButton,
        settingsButton,
        quitButton,
    ];

    for (NSView* view in views) {
        [root addSubview:view];
    }

    [NSLayoutConstraint activateConstraints:@[
        [title.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:52.0],
        [title.topAnchor constraintEqualToAnchor:root.topAnchor constant:48.0],

        [subtitle.leadingAnchor constraintEqualToAnchor:title.leadingAnchor],
        [subtitle.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:8.0],

        [modeLabel.leadingAnchor constraintEqualToAnchor:title.leadingAnchor],
        [modeLabel.topAnchor constraintEqualToAnchor:subtitle.bottomAnchor constant:42.0],
        [modeLabel.widthAnchor constraintEqualToConstant:120.0],

        [self.modePopup.leadingAnchor constraintEqualToAnchor:modeLabel.trailingAnchor constant:18.0],
        [self.modePopup.centerYAnchor constraintEqualToAnchor:modeLabel.centerYAnchor],
        [self.modePopup.widthAnchor constraintEqualToConstant:240.0],

        [self.botLabel.leadingAnchor constraintEqualToAnchor:modeLabel.leadingAnchor],
        [self.botLabel.topAnchor constraintEqualToAnchor:modeLabel.bottomAnchor constant:28.0],
        [self.botLabel.widthAnchor constraintEqualToAnchor:modeLabel.widthAnchor],

        [self.botPopup.leadingAnchor constraintEqualToAnchor:self.modePopup.leadingAnchor],
        [self.botPopup.centerYAnchor constraintEqualToAnchor:self.botLabel.centerYAnchor],
        [self.botPopup.widthAnchor constraintEqualToAnchor:self.modePopup.widthAnchor],

        [self.botDescriptionLabel.leadingAnchor constraintEqualToAnchor:self.botPopup.trailingAnchor constant:28.0],
        [self.botDescriptionLabel.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-52.0],
        [self.botDescriptionLabel.topAnchor constraintEqualToAnchor:self.botLabel.topAnchor constant:-1.0],

        [self.colorLabel.leadingAnchor constraintEqualToAnchor:modeLabel.leadingAnchor],
        [self.colorLabel.topAnchor constraintEqualToAnchor:self.botLabel.bottomAnchor constant:34.0],
        [self.colorLabel.widthAnchor constraintEqualToAnchor:modeLabel.widthAnchor],

        [self.colorControl.leadingAnchor constraintEqualToAnchor:self.modePopup.leadingAnchor],
        [self.colorControl.centerYAnchor constraintEqualToAnchor:self.colorLabel.centerYAnchor],
        [self.colorControl.widthAnchor constraintEqualToAnchor:self.modePopup.widthAnchor],

        [self.saveCheckbox.leadingAnchor constraintEqualToAnchor:self.modePopup.leadingAnchor],
        [self.saveCheckbox.topAnchor constraintEqualToAnchor:self.colorControl.bottomAnchor constant:34.0],

        [self.evaluationCheckbox.leadingAnchor constraintEqualToAnchor:self.modePopup.leadingAnchor],
        [self.evaluationCheckbox.topAnchor constraintEqualToAnchor:self.saveCheckbox.bottomAnchor constant:12.0],

        [startButton.leadingAnchor constraintEqualToAnchor:self.modePopup.leadingAnchor],
        [startButton.bottomAnchor constraintEqualToAnchor:settingsButton.topAnchor constant:-16.0],
        [startButton.widthAnchor constraintEqualToConstant:120.0],

        [settingsButton.leadingAnchor constraintEqualToAnchor:self.modePopup.leadingAnchor],
        [settingsButton.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-48.0],
        [settingsButton.widthAnchor constraintEqualToConstant:100.0],

        [quitButton.leadingAnchor constraintEqualToAnchor:settingsButton.trailingAnchor constant:12.0],
        [quitButton.centerYAnchor constraintEqualToAnchor:settingsButton.centerYAnchor],
        [quitButton.widthAnchor constraintEqualToConstant:90.0],
    ]];

    [self modeChanged:nil];
}

- (void)showSettingsView:(id)sender {
    (void)sender;

    NSView* root = [[NSView alloc] initWithFrame:self.window.contentView.bounds];
    root.translatesAutoresizingMaskIntoConstraints = NO;
    self.settingsView = root;
    self.window.contentView = root;

    NSTextField* title = makeLabel(@"Settings", [NSFont systemFontOfSize:30.0 weight:NSFontWeightSemibold]);
    NSTextField* subtitle = makeLabel(@"Display and promotion preferences", [NSFont systemFontOfSize:15.0 weight:NSFontWeightRegular]);
    subtitle.textColor = [NSColor secondaryLabelColor];

    NSTextField* themeLabel = makeLabel(@"Theme", [NSFont systemFontOfSize:13.0 weight:NSFontWeightMedium]);
    self.themePopup = makePopup(@[ @"System", @"Light", @"Dark" ], YES);
    [self.themePopup selectItemAtIndex:appearanceMode_];
    self.themePopup.target = self;
    self.themePopup.action = @selector(themeChanged:);

    NSTextField* promotionLabel = makeLabel(@"Promotion", [NSFont systemFontOfSize:13.0 weight:NSFontWeightMedium]);
    self.autopromoteCheckbox = makeCheckbox(@"Autopromote", autopromoteEnabled_, YES);
    self.autopromoteCheckbox.target = self;
    self.autopromoteCheckbox.action = @selector(autopromoteChanged:);
    self.autopromotePopup = makePopup(@[ @"Queen", @"Rook", @"Bishop", @"Knight" ], autopromoteEnabled_);
    [self.autopromotePopup selectItemAtIndex:indexForPromotionPiece(autopromotePiece_)];
    self.autopromotePopup.target = self;
    self.autopromotePopup.action = @selector(autopromotePieceChanged:);

    NSButton* customizePiecesButton = makeButton(@"Customize Pieces", self, @selector(showPieceCustomizationView:));
    NSButton* customizeBoardButton = makeButton(@"Customize Board", self, @selector(showBoardCustomizationView:));
    NSButton* returnButton = makeButton(@"Return to Main Menu", self, @selector(returnToMainMenu:));

    NSArray<NSView*>* views = @[
        title,
        subtitle,
        themeLabel,
        self.themePopup,
        promotionLabel,
        self.autopromoteCheckbox,
        self.autopromotePopup,
        customizePiecesButton,
        customizeBoardButton,
        returnButton,
    ];
    for (NSView* view in views) {
        [root addSubview:view];
    }

    [NSLayoutConstraint activateConstraints:@[
        [title.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:52.0],
        [title.topAnchor constraintEqualToAnchor:root.topAnchor constant:48.0],

        [subtitle.leadingAnchor constraintEqualToAnchor:title.leadingAnchor],
        [subtitle.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:8.0],

        [themeLabel.leadingAnchor constraintEqualToAnchor:title.leadingAnchor],
        [themeLabel.topAnchor constraintEqualToAnchor:subtitle.bottomAnchor constant:42.0],
        [themeLabel.widthAnchor constraintEqualToConstant:130.0],

        [self.themePopup.leadingAnchor constraintEqualToAnchor:themeLabel.trailingAnchor constant:18.0],
        [self.themePopup.centerYAnchor constraintEqualToAnchor:themeLabel.centerYAnchor],
        [self.themePopup.widthAnchor constraintEqualToConstant:260.0],

        [promotionLabel.leadingAnchor constraintEqualToAnchor:themeLabel.leadingAnchor],
        [promotionLabel.topAnchor constraintEqualToAnchor:themeLabel.bottomAnchor constant:34.0],
        [promotionLabel.widthAnchor constraintEqualToAnchor:themeLabel.widthAnchor],

        [self.autopromoteCheckbox.leadingAnchor constraintEqualToAnchor:self.themePopup.leadingAnchor],
        [self.autopromoteCheckbox.centerYAnchor constraintEqualToAnchor:promotionLabel.centerYAnchor],

        [self.autopromotePopup.leadingAnchor constraintEqualToAnchor:self.autopromoteCheckbox.trailingAnchor constant:28.0],
        [self.autopromotePopup.centerYAnchor constraintEqualToAnchor:self.autopromoteCheckbox.centerYAnchor],
        [self.autopromotePopup.widthAnchor constraintEqualToConstant:140.0],

        [customizePiecesButton.leadingAnchor constraintEqualToAnchor:self.themePopup.leadingAnchor],
        [customizePiecesButton.topAnchor constraintEqualToAnchor:promotionLabel.bottomAnchor constant:42.0],
        [customizePiecesButton.widthAnchor constraintEqualToConstant:160.0],

        [customizeBoardButton.leadingAnchor constraintEqualToAnchor:customizePiecesButton.trailingAnchor constant:14.0],
        [customizeBoardButton.centerYAnchor constraintEqualToAnchor:customizePiecesButton.centerYAnchor],
        [customizeBoardButton.widthAnchor constraintEqualToConstant:160.0],

        [returnButton.leadingAnchor constraintEqualToAnchor:self.themePopup.leadingAnchor],
        [returnButton.topAnchor constraintEqualToAnchor:customizePiecesButton.bottomAnchor constant:32.0],
        [returnButton.widthAnchor constraintEqualToConstant:170.0],
    ]];
}

- (void)themeChanged:(NSPopUpButton*)sender {
    appearanceMode_ = normalizedThemePreference(sender.indexOfSelectedItem);
    [[NSUserDefaults standardUserDefaults] setInteger:appearanceMode_ forKey:ThemePreferenceKey];
    [self applyAppearancePreference];
}

- (void)autopromoteChanged:(NSButton*)sender {
    autopromoteEnabled_ = sender.state == NSControlStateValueOn;
    [[NSUserDefaults standardUserDefaults] setBool:autopromoteEnabled_ forKey:AutopromoteEnabledPreferenceKey];
    self.autopromotePopup.enabled = autopromoteEnabled_;
}

- (void)autopromotePieceChanged:(NSPopUpButton*)sender {
    autopromotePiece_ = promotionPieceForIndex(sender.indexOfSelectedItem);
    [[NSUserDefaults standardUserDefaults] setInteger:indexForPromotionPiece(autopromotePiece_)
                                               forKey:AutopromotePiecePreferenceKey];
}

- (void)showPieceCustomizationView:(id)sender {
    (void)sender;

    NSView* root = [[NSView alloc] initWithFrame:self.window.contentView.bounds];
    root.translatesAutoresizingMaskIntoConstraints = NO;
    self.settingsView = root;
    self.window.contentView = root;

    NSTextField* title = makeLabel(@"Customize Pieces", [NSFont systemFontOfSize:30.0 weight:NSFontWeightSemibold]);
    NSTextField* subtitle = makeLabel(@"Upload square PNG artwork for each piece.", [NSFont systemFontOfSize:15.0 weight:NSFontWeightRegular]);
    subtitle.textColor = [NSColor secondaryLabelColor];

    NSView* whiteColumn = [[NSView alloc] initWithFrame:NSZeroRect];
    NSView* blackColumn = [[NSView alloc] initWithFrame:NSZeroRect];
    whiteColumn.translatesAutoresizingMaskIntoConstraints = NO;
    blackColumn.translatesAutoresizingMaskIntoConstraints = NO;
    NSTextField* whiteTitle = makeLabel(@"White", [NSFont systemFontOfSize:14.0 weight:NSFontWeightSemibold]);
    NSTextField* blackTitle = makeLabel(@"Black", [NSFont systemFontOfSize:14.0 weight:NSFontWeightSemibold]);

    [root addSubview:title];
    [root addSubview:subtitle];
    [root addSubview:whiteColumn];
    [root addSubview:blackColumn];
    [whiteColumn addSubview:whiteTitle];
    [blackColumn addSubview:blackTitle];

    NSMutableArray<NSLayoutConstraint*>* constraints = [[NSMutableArray alloc] init];
    [constraints addObjectsFromArray:@[
        [title.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:52.0],
        [title.topAnchor constraintEqualToAnchor:root.topAnchor constant:48.0],
        [subtitle.leadingAnchor constraintEqualToAnchor:title.leadingAnchor],
        [subtitle.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:8.0],

        [whiteColumn.leadingAnchor constraintEqualToAnchor:title.leadingAnchor],
        [whiteColumn.topAnchor constraintEqualToAnchor:subtitle.bottomAnchor constant:36.0],
        [whiteColumn.widthAnchor constraintEqualToConstant:310.0],

        [blackColumn.leadingAnchor constraintEqualToAnchor:whiteColumn.trailingAnchor constant:48.0],
        [blackColumn.topAnchor constraintEqualToAnchor:whiteColumn.topAnchor],
        [blackColumn.widthAnchor constraintEqualToAnchor:whiteColumn.widthAnchor],
        [blackColumn.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-52.0],

        [whiteTitle.leadingAnchor constraintEqualToAnchor:whiteColumn.leadingAnchor],
        [whiteTitle.topAnchor constraintEqualToAnchor:whiteColumn.topAnchor],
        [blackTitle.leadingAnchor constraintEqualToAnchor:blackColumn.leadingAnchor],
        [blackTitle.topAnchor constraintEqualToAnchor:blackColumn.topAnchor],
    ]];

    for (NSInteger columnIndex = 0; columnIndex < 2; ++columnIndex) {
        const chess::Color color = columnIndex == 0 ? chess::Color::White : chess::Color::Black;
        NSView* column = columnIndex == 0 ? whiteColumn : blackColumn;
        NSTextField* heading = columnIndex == 0 ? whiteTitle : blackTitle;
        NSView* previous = heading;

        for (NSInteger pieceIndex = 0; pieceIndex < 6; ++pieceIndex) {
            const chess::Piece piece{color, customizablePieceTypeAtIndex(pieceIndex)};
            NSTextField* label = makeLabel(analysisPieceTitle(piece), [NSFont systemFontOfSize:13.0 weight:NSFontWeightMedium]);
            NSString* customPath = [[NSUserDefaults standardUserDefaults] stringForKey:pieceImagePreferenceKey(piece)];
            NSButton* uploadButton = makeButton(customPath.length > 0 ? @"Replace PNG" : @"Upload PNG",
                                                self,
                                                @selector(uploadPiecePNG:));
            uploadButton.tag = customizationIndexForPiece(piece);

            [column addSubview:label];
            [column addSubview:uploadButton];
            [constraints addObjectsFromArray:@[
                [label.leadingAnchor constraintEqualToAnchor:column.leadingAnchor],
                [label.centerYAnchor constraintEqualToAnchor:uploadButton.centerYAnchor],
                [uploadButton.leadingAnchor constraintEqualToAnchor:label.trailingAnchor constant:14.0],
                [uploadButton.trailingAnchor constraintEqualToAnchor:column.trailingAnchor],
                [uploadButton.topAnchor constraintEqualToAnchor:previous.bottomAnchor constant:(pieceIndex == 0 ? 18.0 : 12.0)],
                [uploadButton.widthAnchor constraintEqualToConstant:118.0],
                [uploadButton.heightAnchor constraintEqualToConstant:30.0],
            ]];
            previous = uploadButton;
        }

        [constraints addObject:[previous.bottomAnchor constraintEqualToAnchor:column.bottomAnchor]];
    }

    NSButton* resetButton = makeButton(@"Reset Piece PNGs", self, @selector(resetPiecePNGs:));
    NSButton* backButton = makeButton(@"Back to Settings", self, @selector(showSettingsView:));
    [root addSubview:resetButton];
    [root addSubview:backButton];
    [constraints addObjectsFromArray:@[
        [resetButton.leadingAnchor constraintEqualToAnchor:title.leadingAnchor],
        [resetButton.topAnchor constraintEqualToAnchor:whiteColumn.bottomAnchor constant:36.0],
        [resetButton.widthAnchor constraintEqualToConstant:150.0],

        [backButton.leadingAnchor constraintEqualToAnchor:resetButton.trailingAnchor constant:14.0],
        [backButton.centerYAnchor constraintEqualToAnchor:resetButton.centerYAnchor],
        [backButton.widthAnchor constraintEqualToConstant:140.0],
    ]];

    [NSLayoutConstraint activateConstraints:constraints];
}

- (void)showBoardCustomizationView:(id)sender {
    (void)sender;

    NSView* root = [[NSView alloc] initWithFrame:self.window.contentView.bounds];
    root.translatesAutoresizingMaskIntoConstraints = NO;
    self.settingsView = root;
    self.window.contentView = root;

    NSTextField* title = makeLabel(@"Customize Board", [NSFont systemFontOfSize:30.0 weight:NSFontWeightSemibold]);
    NSTextField* subtitle = makeLabel(@"Set square colors with hex, RGB, or upload a square-cropped board image.",
                                      [NSFont systemFontOfSize:15.0 weight:NSFontWeightRegular]);
    subtitle.textColor = [NSColor secondaryLabelColor];

    NSTextField* lightLabel = makeLabel(@"Light Square", [NSFont systemFontOfSize:13.0 weight:NSFontWeightMedium]);
    NSTextField* darkLabel = makeLabel(@"Dark Square", [NSFont systemFontOfSize:13.0 weight:NSFontWeightMedium]);

    self.lightSquareField = [[NSTextField alloc] initWithFrame:NSZeroRect];
    self.lightSquareField.translatesAutoresizingMaskIntoConstraints = NO;
    self.lightSquareField.stringValue = hexStringFromColor(boardLightSquareColor());

    self.darkSquareField = [[NSTextField alloc] initWithFrame:NSZeroRect];
    self.darkSquareField.translatesAutoresizingMaskIntoConstraints = NO;
    self.darkSquareField.stringValue = hexStringFromColor(boardDarkSquareColor());

    self.lightSquareWell = [[NSColorWell alloc] initWithFrame:NSZeroRect];
    self.lightSquareWell.translatesAutoresizingMaskIntoConstraints = NO;
    self.lightSquareWell.color = boardLightSquareColor();
    self.lightSquareWell.target = self;
    self.lightSquareWell.action = @selector(boardColorWellChanged:);
    self.lightSquareWell.tag = 0;

    self.darkSquareWell = [[NSColorWell alloc] initWithFrame:NSZeroRect];
    self.darkSquareWell.translatesAutoresizingMaskIntoConstraints = NO;
    self.darkSquareWell.color = boardDarkSquareColor();
    self.darkSquareWell.target = self;
    self.darkSquareWell.action = @selector(boardColorWellChanged:);
    self.darkSquareWell.tag = 1;

    NSButton* applyButton = makeButton(@"Apply Colors", self, @selector(applyBoardColors:));
    NSButton* uploadButton = makeButton(@"Upload Board Image", self, @selector(uploadBoardImage:));
    NSButton* clearButton = makeButton(@"Clear Board Image", self, @selector(clearBoardImage:));
    NSButton* backButton = makeButton(@"Back to Settings", self, @selector(showSettingsView:));

    for (NSView* view in @[
             title,
             subtitle,
             lightLabel,
             self.lightSquareField,
             self.lightSquareWell,
             darkLabel,
             self.darkSquareField,
             self.darkSquareWell,
             applyButton,
             uploadButton,
             clearButton,
             backButton,
         ]) {
        [root addSubview:view];
    }

    [NSLayoutConstraint activateConstraints:@[
        [title.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:52.0],
        [title.topAnchor constraintEqualToAnchor:root.topAnchor constant:48.0],
        [subtitle.leadingAnchor constraintEqualToAnchor:title.leadingAnchor],
        [subtitle.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-52.0],
        [subtitle.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:8.0],

        [lightLabel.leadingAnchor constraintEqualToAnchor:title.leadingAnchor],
        [lightLabel.topAnchor constraintEqualToAnchor:subtitle.bottomAnchor constant:42.0],
        [lightLabel.widthAnchor constraintEqualToConstant:130.0],

        [self.lightSquareField.leadingAnchor constraintEqualToAnchor:lightLabel.trailingAnchor constant:18.0],
        [self.lightSquareField.centerYAnchor constraintEqualToAnchor:lightLabel.centerYAnchor],
        [self.lightSquareField.widthAnchor constraintEqualToConstant:180.0],

        [self.lightSquareWell.leadingAnchor constraintEqualToAnchor:self.lightSquareField.trailingAnchor constant:14.0],
        [self.lightSquareWell.centerYAnchor constraintEqualToAnchor:self.lightSquareField.centerYAnchor],
        [self.lightSquareWell.widthAnchor constraintEqualToConstant:42.0],
        [self.lightSquareWell.heightAnchor constraintEqualToConstant:28.0],

        [darkLabel.leadingAnchor constraintEqualToAnchor:lightLabel.leadingAnchor],
        [darkLabel.topAnchor constraintEqualToAnchor:lightLabel.bottomAnchor constant:32.0],
        [darkLabel.widthAnchor constraintEqualToAnchor:lightLabel.widthAnchor],

        [self.darkSquareField.leadingAnchor constraintEqualToAnchor:self.lightSquareField.leadingAnchor],
        [self.darkSquareField.centerYAnchor constraintEqualToAnchor:darkLabel.centerYAnchor],
        [self.darkSquareField.widthAnchor constraintEqualToAnchor:self.lightSquareField.widthAnchor],

        [self.darkSquareWell.leadingAnchor constraintEqualToAnchor:self.darkSquareField.trailingAnchor constant:14.0],
        [self.darkSquareWell.centerYAnchor constraintEqualToAnchor:self.darkSquareField.centerYAnchor],
        [self.darkSquareWell.widthAnchor constraintEqualToAnchor:self.lightSquareWell.widthAnchor],
        [self.darkSquareWell.heightAnchor constraintEqualToAnchor:self.lightSquareWell.heightAnchor],

        [applyButton.leadingAnchor constraintEqualToAnchor:self.lightSquareField.leadingAnchor],
        [applyButton.topAnchor constraintEqualToAnchor:darkLabel.bottomAnchor constant:30.0],
        [applyButton.widthAnchor constraintEqualToConstant:130.0],

        [uploadButton.leadingAnchor constraintEqualToAnchor:applyButton.leadingAnchor],
        [uploadButton.topAnchor constraintEqualToAnchor:applyButton.bottomAnchor constant:34.0],
        [uploadButton.widthAnchor constraintEqualToConstant:170.0],

        [clearButton.leadingAnchor constraintEqualToAnchor:uploadButton.trailingAnchor constant:14.0],
        [clearButton.centerYAnchor constraintEqualToAnchor:uploadButton.centerYAnchor],
        [clearButton.widthAnchor constraintEqualToConstant:150.0],

        [backButton.leadingAnchor constraintEqualToAnchor:uploadButton.leadingAnchor],
        [backButton.topAnchor constraintEqualToAnchor:uploadButton.bottomAnchor constant:32.0],
        [backButton.widthAnchor constraintEqualToConstant:140.0],
    ]];
}

- (void)uploadPiecePNG:(NSButton*)sender {
    const chess::Piece piece = pieceForCustomizationIndex(sender.tag);
    NSURL* sourceURL = [self choosePNGURL];
    if (sourceURL == nil) {
        return;
    }

    NSString* path = nil;
    if (!saveSquarePNGFromURL(sourceURL, pieceImageStorageName(piece), &path)) {
        [self showSettingsAlertWithTitle:@"Could Not Import PNG"
                                 message:@"Choose a valid PNG image that can be read by the app."];
        return;
    }

    [[NSUserDefaults standardUserDefaults] setObject:path forKey:pieceImagePreferenceKey(piece)];
    [self refreshAppearanceViews];
    [self showPieceCustomizationView:nil];
}

- (void)resetPiecePNGs:(id)sender {
    (void)sender;
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    for (NSInteger index = 0; index < 12; ++index) {
        [defaults removeObjectForKey:pieceImagePreferenceKey(pieceForCustomizationIndex(index))];
    }
    [self refreshAppearanceViews];
    [self showPieceCustomizationView:nil];
}

- (void)boardColorWellChanged:(NSColorWell*)sender {
    NSString* value = hexStringFromColor(sender.color);
    if (sender.tag == 0) {
        self.lightSquareField.stringValue = value;
    } else {
        self.darkSquareField.stringValue = value;
    }
}

- (void)applyBoardColors:(id)sender {
    (void)sender;
    NSColor* lightColor = colorFromUserString(self.lightSquareField.stringValue);
    NSColor* darkColor = colorFromUserString(self.darkSquareField.stringValue);
    if (lightColor == nil || darkColor == nil) {
        [self showSettingsAlertWithTitle:@"Invalid Board Color"
                                 message:@"Use #RRGGBB, RGB values like 224,220,199, or rgb(224, 220, 199)."];
        return;
    }

    NSString* lightHex = hexStringFromColor(lightColor);
    NSString* darkHex = hexStringFromColor(darkColor);
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    [defaults setObject:lightHex forKey:BoardLightSquarePreferenceKey];
    [defaults setObject:darkHex forKey:BoardDarkSquarePreferenceKey];
    self.lightSquareField.stringValue = lightHex;
    self.darkSquareField.stringValue = darkHex;
    self.lightSquareWell.color = lightColor;
    self.darkSquareWell.color = darkColor;
    [self refreshAppearanceViews];
}

- (void)uploadBoardImage:(id)sender {
    (void)sender;
    NSURL* sourceURL = [self choosePNGURL];
    if (sourceURL == nil) {
        return;
    }

    NSString* path = nil;
    if (!saveSquarePNGFromURL(sourceURL, @"board-background.png", &path)) {
        [self showSettingsAlertWithTitle:@"Could Not Import Board Image"
                                 message:@"Choose a valid PNG image that can be read by the app."];
        return;
    }

    [[NSUserDefaults standardUserDefaults] setObject:path forKey:BoardImagePreferenceKey];
    [self refreshAppearanceViews];
}

- (void)clearBoardImage:(id)sender {
    (void)sender;
    [[NSUserDefaults standardUserDefaults] removeObjectForKey:BoardImagePreferenceKey];
    [self refreshAppearanceViews];
}

- (void)modeChanged:(id)sender {
    (void)sender;
    const BOOL botMode = self.modePopup.indexOfSelectedItem == 1;
    const BOOL analysisMode = self.modePopup.indexOfSelectedItem == 2;
    self.botPopup.enabled = botMode;
    self.colorControl.enabled = botMode;
    self.saveCheckbox.enabled = !analysisMode;
    self.evaluationCheckbox.enabled = !analysisMode;
    self.botLabel.textColor = botMode ? [NSColor labelColor] : [NSColor tertiaryLabelColor];
    self.colorLabel.textColor = botMode ? [NSColor labelColor] : [NSColor tertiaryLabelColor];
    self.botDescriptionLabel.textColor = botMode ? [NSColor secondaryLabelColor] : [NSColor tertiaryLabelColor];
    [self saveStartMenuPreferences];
    [self updateBotDescription];
}

- (void)botChanged:(id)sender {
    (void)sender;
    [self saveStartMenuPreferences];
    [self updateBotDescription];
}

- (void)startMenuPreferenceChanged:(id)sender {
    (void)sender;
    [self saveStartMenuPreferences];
}

- (void)startGame:(id)sender {
    (void)sender;

    [self saveStartMenuPreferences];
    if (self.modePopup.indexOfSelectedItem == 2) {
        [self startAnalysisMode];
        return;
    }

    const BOOL botMode = self.modePopup.indexOfSelectedItem == 1;
    const BOOL saveGame = self.saveCheckbox.state == NSControlStateValueOn;
    const BOOL showEvaluation = self.evaluationCheckbox.state == NSControlStateValueOn;
    NSString* modeText = botMode ? @"Bots" : @"2 player";
    NSString* whitePlayer = @"Player 1";
    NSString* blackPlayer = @"Player 2";
    NSString* botLine = @"";

    botMode_ = botMode;
    analysisMode_ = NO;
    ++botMoveGeneration_;
    humanColor_ = chess::Color::White;
    botColor_ = chess::Color::Black;
    activeBot_ = nullptr;
    pendingPromotionCandidates_.clear();
    viewedGame_.reset();
    promotionPending_ = NO;
    gameEnded_ = NO;
    self.gameOverMessage = nil;
    game_.emplace(chess::ChessGame::standard());
    historyCursor_ = game_->moveCount();

    if (botMode) {
        NSString* botName = self.botPopup.titleOfSelectedItem ?: @"John Checkers";
        activeBot_ = [self selectedBot];
        const BOOL humanIsWhite = self.colorControl.selectedSegment != 1;
        humanColor_ = humanIsWhite ? chess::Color::White : chess::Color::Black;
        botColor_ = chess::opposite(humanColor_);
        whitePlayer = humanIsWhite ? @"Human" : botName;
        blackPlayer = humanIsWhite ? botName : @"Human";
        botLine = [NSString stringWithFormat:@"Bot: %@\n", botName];
    }

    evaluationVisible_ = showEvaluation;
    evaluationPerspective_ = botMode_ ? humanColor_ : chess::Color::White;
    autoFlipEnabled_ = !botMode_;

    self.currentSummary = [NSString stringWithFormat:@"Mode: %@\n%@White: %@\nBlack: %@\nSave game: %@\nEvaluation: %@\nAuto-flip: %@",
                                                     modeText,
                                                     botLine,
                                                     whitePlayer,
                                                     blackPlayer,
                                                     saveGame ? @"Yes" : @"No",
                                                     showEvaluation ? @"Shown" : @"Hidden",
                                                     autoFlipEnabled_ ? @"On" : @"Off"];

    [self showGameView];
    [self updateStatusLabel];
    [self restartEvaluationLoop];
    if (botMode_) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self playBotMoveIfNeeded];
        });
    }
}

- (void)updateAnalysisStatus {
    if (self.statusLabel == nil) {
        return;
    }

    NSString* side = colorToNSString(analysisBoard_.sideToMove());
    self.statusLabel.stringValue = [NSString stringWithFormat:@"%@ to move", side];
}

- (void)updateBestMoves {
    if (self.bestMovesLabel == nil) {
        return;
    }

    self.bestMovesLabel.stringValue = bestMovesText(analysisBoard_);
}

- (void)refreshAnalysisBoard {
    evaluationPerspective_ = analysisBoard_.sideToMove();

    if (self.boardView != nil) {
        self.boardView.position = &analysisBoard_;
        self.boardView.game = nullptr;
        [self.boardView clearSelection];
        self.boardView.needsDisplay = YES;
    }

    [self updateAnalysisStatus];
    [self updateBestMoves];
    self.analysisPaletteView.selectedPiece = selectedAnalysisPiece_;
    [self updateEvaluationDisplayAnimated:NO mateSearchPly:3];
}

- (void)startAnalysisMode {
    [self stopEvaluationLoop];
    analysisMode_ = YES;
    botMode_ = NO;
    ++botMoveGeneration_;
    gameEnded_ = NO;
    promotionPending_ = NO;
    activeBot_ = nullptr;
    self.gameOverMessage = nil;
    game_.reset();
    viewedGame_.reset();
    pendingPromotionCandidates_.clear();
    historyCursor_ = 0;
    evaluationVisible_ = YES;
    evaluationPerspective_ = analysisBoard_.sideToMove();
    autoFlipEnabled_ = NO;
    analysisBoard_ = chess::Board::standard();
    selectedAnalysisPiece_ = chess::Piece{chess::Color::White, chess::PieceType::Pawn};
    self.currentSummary = @"Mode: Analysis";

    [self showAnalysisView];
    [self refreshAnalysisBoard];
}

- (void)showAnalysisView {
    NSView* root = [[NSView alloc] initWithFrame:self.window.contentView.bounds];
    root.translatesAutoresizingMaskIntoConstraints = NO;
    self.gameView = root;
    self.window.contentView = root;

    ChessBoardView* board = [[ChessBoardView alloc] initWithFrame:NSZeroRect];
    board.translatesAutoresizingMaskIntoConstraints = NO;
    board.wantsLayer = YES;
    board.game = nullptr;
    board.position = &analysisBoard_;
    board.delegate = self;
    board.boardFlipped = NO;
    self.boardView = board;

    self.analysisPaletteView = [[AnalysisPiecePaletteView alloc] initWithFrame:NSZeroRect];
    self.analysisPaletteView.translatesAutoresizingMaskIntoConstraints = NO;
    self.analysisPaletteView.delegate = self;
    self.analysisPaletteView.selectedPiece = selectedAnalysisPiece_;

    NSView* sidePanel = [[NSView alloc] initWithFrame:NSZeroRect];
    sidePanel.translatesAutoresizingMaskIntoConstraints = NO;

    NSTextField* title = makeLabel(@"Analysis", [NSFont systemFontOfSize:22.0 weight:NSFontWeightSemibold]);
    self.statusLabel = makeLabel(@"White to move", [NSFont systemFontOfSize:15.0 weight:NSFontWeightMedium]);
    self.messageLabel = makeLabel(@"", [NSFont systemFontOfSize:13.0 weight:NSFontWeightRegular]);
    self.messageLabel.textColor = [NSColor secondaryLabelColor];

    NSTextField* sideLabel = makeLabel(@"Side to move", [NSFont systemFontOfSize:13.0 weight:NSFontWeightMedium]);
    self.analysisSideControl = [[NSSegmentedControl alloc] initWithFrame:NSZeroRect];
    self.analysisSideControl.translatesAutoresizingMaskIntoConstraints = NO;
    self.analysisSideControl.segmentCount = 2;
    [self.analysisSideControl setLabel:@"White" forSegment:0];
    [self.analysisSideControl setLabel:@"Black" forSegment:1];
    self.analysisSideControl.selectedSegment = analysisBoard_.sideToMove() == chess::Color::White ? 0 : 1;
    self.analysisSideControl.segmentStyle = NSSegmentStyleRounded;
    self.analysisSideControl.target = self;
    self.analysisSideControl.action = @selector(analysisSideChanged:);

    self.evaluationPanel = [[NSView alloc] initWithFrame:NSZeroRect];
    self.evaluationPanel.translatesAutoresizingMaskIntoConstraints = NO;
    self.evaluationPanelHeightConstraint = [self.evaluationPanel.heightAnchor constraintEqualToConstant:50.0];

    self.evaluationTitleLabel = makeLabel(@"Evaluation", [NSFont systemFontOfSize:13.0 weight:NSFontWeightMedium]);

    self.evaluationTrack = [[NSView alloc] initWithFrame:NSZeroRect];
    self.evaluationTrack.translatesAutoresizingMaskIntoConstraints = NO;
    self.evaluationTrack.wantsLayer = YES;
    self.evaluationTrack.layer.cornerRadius = 9.0;
    self.evaluationTrack.layer.masksToBounds = YES;
    self.evaluationTrack.layer.borderWidth = 1.0;
    self.evaluationTrack.layer.borderColor = [NSColor separatorColor].CGColor;
    self.evaluationTrack.layer.backgroundColor = evaluationColor(chess::opposite(evaluationPerspective_)).CGColor;

    self.evaluationPlayerFill = [[NSView alloc] initWithFrame:NSZeroRect];
    self.evaluationPlayerFill.translatesAutoresizingMaskIntoConstraints = NO;
    self.evaluationPlayerFill.wantsLayer = YES;
    self.evaluationPlayerFill.layer.backgroundColor = evaluationColor(evaluationPerspective_).CGColor;
    [self.evaluationTrack addSubview:self.evaluationPlayerFill];
    self.evaluationPlayerFillWidthConstraint = [self.evaluationPlayerFill.widthAnchor constraintEqualToConstant:0.0];
    [self.evaluationPanel addSubview:self.evaluationTitleLabel];
    [self.evaluationPanel addSubview:self.evaluationTrack];

    NSTextField* bestMovesTitle = makeLabel(@"Best Moves", [NSFont systemFontOfSize:15.0 weight:NSFontWeightSemibold]);
    self.bestMovesLabel = makeLabel(@"", [NSFont monospacedDigitSystemFontOfSize:13.0 weight:NSFontWeightRegular]);
    self.bestMovesLabel.textColor = [NSColor secondaryLabelColor];

    self.flipBoardButton = makeButton(@"Flip Board", self, @selector(flipBoard:));
    NSButton* clearButton = makeButton(@"Clear Board", self, @selector(clearAnalysisBoard:));
    NSButton* resetButton = makeButton(@"Reset Board", self, @selector(resetAnalysisBoard:));
    NSButton* returnButton = makeButton(@"Return to Main Menu", self, @selector(returnToMainMenu:));

    [root addSubview:board];
    [root addSubview:self.analysisPaletteView];
    [root addSubview:sidePanel];
    for (NSView* view in @[
             title,
             self.statusLabel,
             self.messageLabel,
             sideLabel,
             self.analysisSideControl,
             self.evaluationPanel,
             bestMovesTitle,
             self.bestMovesLabel,
             self.flipBoardButton,
             clearButton,
             resetButton,
             returnButton,
         ]) {
        [sidePanel addSubview:view];
    }

    [NSLayoutConstraint activateConstraints:@[
        [board.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:32.0],
        [board.centerYAnchor constraintEqualToAnchor:root.centerYAnchor constant:-25.0],
        [board.widthAnchor constraintEqualToAnchor:board.heightAnchor],
        [board.widthAnchor constraintGreaterThanOrEqualToConstant:420.0],
        [board.widthAnchor constraintLessThanOrEqualToConstant:540.0],
        [board.topAnchor constraintGreaterThanOrEqualToAnchor:root.topAnchor constant:28.0],

        [self.analysisPaletteView.leadingAnchor constraintEqualToAnchor:board.leadingAnchor],
        [self.analysisPaletteView.trailingAnchor constraintEqualToAnchor:board.trailingAnchor],
        [self.analysisPaletteView.topAnchor constraintEqualToAnchor:board.bottomAnchor constant:12.0],
        [self.analysisPaletteView.heightAnchor constraintEqualToConstant:92.0],
        [self.analysisPaletteView.bottomAnchor constraintLessThanOrEqualToAnchor:root.bottomAnchor constant:-28.0],

        [sidePanel.leadingAnchor constraintEqualToAnchor:board.trailingAnchor constant:28.0],
        [sidePanel.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-32.0],
        [sidePanel.topAnchor constraintEqualToAnchor:board.topAnchor],
        [sidePanel.bottomAnchor constraintEqualToAnchor:self.analysisPaletteView.bottomAnchor],
        [sidePanel.widthAnchor constraintGreaterThanOrEqualToConstant:220.0],
        [sidePanel.widthAnchor constraintLessThanOrEqualToConstant:360.0],

        [title.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [title.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [title.topAnchor constraintEqualToAnchor:sidePanel.topAnchor constant:10.0],

        [self.statusLabel.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [self.statusLabel.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [self.statusLabel.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:24.0],

        [self.messageLabel.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [self.messageLabel.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [self.messageLabel.topAnchor constraintEqualToAnchor:self.statusLabel.bottomAnchor constant:8.0],

        [sideLabel.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [sideLabel.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [sideLabel.topAnchor constraintEqualToAnchor:self.messageLabel.bottomAnchor constant:26.0],

        [self.analysisSideControl.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [self.analysisSideControl.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [self.analysisSideControl.topAnchor constraintEqualToAnchor:sideLabel.bottomAnchor constant:8.0],

        [self.evaluationPanel.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [self.evaluationPanel.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [self.evaluationPanel.topAnchor constraintEqualToAnchor:self.analysisSideControl.bottomAnchor constant:28.0],
        self.evaluationPanelHeightConstraint,

        [self.evaluationTitleLabel.leadingAnchor constraintEqualToAnchor:self.evaluationPanel.leadingAnchor],
        [self.evaluationTitleLabel.trailingAnchor constraintEqualToAnchor:self.evaluationPanel.trailingAnchor],
        [self.evaluationTitleLabel.topAnchor constraintEqualToAnchor:self.evaluationPanel.topAnchor],

        [self.evaluationTrack.leadingAnchor constraintEqualToAnchor:self.evaluationPanel.leadingAnchor],
        [self.evaluationTrack.trailingAnchor constraintEqualToAnchor:self.evaluationPanel.trailingAnchor],
        [self.evaluationTrack.topAnchor constraintEqualToAnchor:self.evaluationTitleLabel.bottomAnchor constant:8.0],
        [self.evaluationTrack.heightAnchor constraintEqualToConstant:18.0],

        [self.evaluationPlayerFill.trailingAnchor constraintEqualToAnchor:self.evaluationTrack.trailingAnchor],
        [self.evaluationPlayerFill.topAnchor constraintEqualToAnchor:self.evaluationTrack.topAnchor],
        [self.evaluationPlayerFill.bottomAnchor constraintEqualToAnchor:self.evaluationTrack.bottomAnchor],
        self.evaluationPlayerFillWidthConstraint,

        [bestMovesTitle.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [bestMovesTitle.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [bestMovesTitle.topAnchor constraintEqualToAnchor:self.evaluationPanel.bottomAnchor constant:30.0],

        [self.bestMovesLabel.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [self.bestMovesLabel.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [self.bestMovesLabel.topAnchor constraintEqualToAnchor:bestMovesTitle.bottomAnchor constant:10.0],

        [self.flipBoardButton.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [self.flipBoardButton.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [self.flipBoardButton.bottomAnchor constraintEqualToAnchor:clearButton.topAnchor constant:-12.0],
        [self.flipBoardButton.heightAnchor constraintEqualToConstant:32.0],

        [clearButton.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [clearButton.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [clearButton.bottomAnchor constraintEqualToAnchor:resetButton.topAnchor constant:-12.0],
        [clearButton.heightAnchor constraintEqualToConstant:32.0],

        [resetButton.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [resetButton.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [resetButton.bottomAnchor constraintEqualToAnchor:returnButton.topAnchor constant:-18.0],
        [resetButton.heightAnchor constraintEqualToConstant:32.0],

        [returnButton.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [returnButton.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [returnButton.bottomAnchor constraintEqualToAnchor:sidePanel.bottomAnchor constant:-10.0],
        [returnButton.heightAnchor constraintEqualToConstant:34.0],
    ]];

    [self updateEvaluationDisplayAnimated:NO mateSearchPly:3];
}

- (void)analysisPaletteView:(AnalysisPiecePaletteView*)paletteView didSelectPiece:(chess::Piece)piece {
    (void)paletteView;
    selectedAnalysisPiece_ = piece;
    [self setGameMessage:[NSString stringWithFormat:@"Selected %@.", analysisPieceTitle(piece)]];
}

- (void)analysisSideChanged:(id)sender {
    (void)sender;
    analysisBoard_.setSideToMove(self.analysisSideControl.selectedSegment == 1
                                     ? chess::Color::Black
                                     : chess::Color::White);
    [self refreshAnalysisBoard];
}

- (void)clearAnalysisBoard:(id)sender {
    (void)sender;
    const chess::Color side = analysisBoard_.sideToMove();
    analysisBoard_.clear();
    analysisBoard_.setSideToMove(side);
    [self refreshAnalysisBoard];
    [self setGameMessage:@"Board cleared."];
}

- (void)resetAnalysisBoard:(id)sender {
    (void)sender;
    analysisBoard_ = chess::Board::standard();
    if (self.analysisSideControl != nil) {
        self.analysisSideControl.selectedSegment = 0;
    }
    [self refreshAnalysisBoard];
    [self setGameMessage:@"Board reset."];
}

- (void)showGameView {
    NSView* root = [[NSView alloc] initWithFrame:self.window.contentView.bounds];
    root.translatesAutoresizingMaskIntoConstraints = NO;
    self.gameView = root;
    self.window.contentView = root;

    ChessBoardView* board = [[ChessBoardView alloc] initWithFrame:NSZeroRect];
    board.translatesAutoresizingMaskIntoConstraints = NO;
    board.wantsLayer = YES;
    board.game = game_ ? &(*game_) : nullptr;
    board.delegate = self;
    board.boardFlipped = [self shouldFlipBoardForCurrentTurn];
    self.boardView = board;

    NSView* sidePanel = [[NSView alloc] initWithFrame:NSZeroRect];
    sidePanel.translatesAutoresizingMaskIntoConstraints = NO;

    NSTextField* gameLabel = makeLabel(@"Game", [NSFont systemFontOfSize:22.0 weight:NSFontWeightSemibold]);
    NSTextField* summary = makeLabel(self.currentSummary ?: @"Mode: 2 player", [NSFont systemFontOfSize:13.0 weight:NSFontWeightRegular]);
    summary.textColor = [NSColor secondaryLabelColor];
    self.statusLabel = makeLabel(@"White to move", [NSFont systemFontOfSize:15.0 weight:NSFontWeightMedium]);
    self.messageLabel = makeLabel(@"", [NSFont systemFontOfSize:13.0 weight:NSFontWeightRegular]);
    self.messageLabel.textColor = [NSColor secondaryLabelColor];

    self.promotionChoiceView = [[NSView alloc] initWithFrame:NSZeroRect];
    self.promotionChoiceView.translatesAutoresizingMaskIntoConstraints = NO;
    self.promotionChoiceView.hidden = YES;
    NSTextField* promotionPrompt = makeLabel(@"Promote pawn to", [NSFont systemFontOfSize:13.0 weight:NSFontWeightMedium]);
    NSButton* queenButton = makeButton(promotionTitle(chess::PieceType::Queen), self, @selector(promotionPieceSelected:));
    NSButton* rookButton = makeButton(promotionTitle(chess::PieceType::Rook), self, @selector(promotionPieceSelected:));
    NSButton* bishopButton = makeButton(promotionTitle(chess::PieceType::Bishop), self, @selector(promotionPieceSelected:));
    NSButton* knightButton = makeButton(promotionTitle(chess::PieceType::Knight), self, @selector(promotionPieceSelected:));
    queenButton.tag = static_cast<NSInteger>(chess::PieceType::Queen);
    rookButton.tag = static_cast<NSInteger>(chess::PieceType::Rook);
    bishopButton.tag = static_cast<NSInteger>(chess::PieceType::Bishop);
    knightButton.tag = static_cast<NSInteger>(chess::PieceType::Knight);

    NSStackView* promotionButtons = [NSStackView stackViewWithViews:@[
        queenButton,
        rookButton,
        bishopButton,
        knightButton,
    ]];
    promotionButtons.translatesAutoresizingMaskIntoConstraints = NO;
    promotionButtons.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    promotionButtons.spacing = 6.0;
    promotionButtons.distribution = NSStackViewDistributionFillEqually;
    [self.promotionChoiceView addSubview:promotionPrompt];
    [self.promotionChoiceView addSubview:promotionButtons];

    self.flipBoardButton = makeButton(@"Flip Board", self, @selector(flipBoard:));
    self.autoFlipCheckbox = [NSButton checkboxWithTitle:@"Auto-flip"
                                                 target:self
                                                 action:@selector(autoFlipChanged:)];
    self.autoFlipCheckbox.translatesAutoresizingMaskIntoConstraints = NO;
    self.autoFlipCheckbox.state = autoFlipEnabled_ ? NSControlStateValueOn : NSControlStateValueOff;

    self.historyBackButton = makeButton(@"<", self, @selector(historyBack:));
    self.historyForwardButton = makeButton(@">", self, @selector(historyForward:));
    self.historyLatestButton = makeButton(@">>", self, @selector(historyLatest:));
    self.historyBackButton.toolTip = @"Back one move";
    self.historyForwardButton.toolTip = @"Forward one move";
    self.historyLatestButton.toolTip = @"Latest position";

    NSStackView* historyButtons = [NSStackView stackViewWithViews:@[
        self.historyBackButton,
        self.historyForwardButton,
        self.historyLatestButton,
    ]];
    historyButtons.translatesAutoresizingMaskIntoConstraints = NO;
    historyButtons.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    historyButtons.spacing = 8.0;
    historyButtons.distribution = NSStackViewDistributionFillEqually;

    self.evaluationPanel = [[NSView alloc] initWithFrame:NSZeroRect];
    self.evaluationPanel.translatesAutoresizingMaskIntoConstraints = NO;
    self.evaluationPanelHeightConstraint = [self.evaluationPanel.heightAnchor constraintEqualToConstant:82.0];

    self.evaluationTitleLabel = makeLabel(
        [NSString stringWithFormat:@"Evaluation (%@)", colorToNSString(evaluationPerspective_)],
        [NSFont systemFontOfSize:13.0 weight:NSFontWeightMedium]
    );
    self.evaluationValueLabel = makeLabel(@"0.00", [NSFont monospacedDigitSystemFontOfSize:18.0 weight:NSFontWeightSemibold]);
    self.evaluationValueLabel.alignment = NSTextAlignmentCenter;

    self.evaluationTrack = [[NSView alloc] initWithFrame:NSZeroRect];
    self.evaluationTrack.translatesAutoresizingMaskIntoConstraints = NO;
    self.evaluationTrack.wantsLayer = YES;
    self.evaluationTrack.layer.cornerRadius = 9.0;
    self.evaluationTrack.layer.masksToBounds = YES;
    self.evaluationTrack.layer.borderWidth = 1.0;
    self.evaluationTrack.layer.borderColor = [NSColor separatorColor].CGColor;
    self.evaluationTrack.layer.backgroundColor = evaluationColor(chess::opposite(evaluationPerspective_)).CGColor;

    self.evaluationPlayerFill = [[NSView alloc] initWithFrame:NSZeroRect];
    self.evaluationPlayerFill.translatesAutoresizingMaskIntoConstraints = NO;
    self.evaluationPlayerFill.wantsLayer = YES;
    self.evaluationPlayerFill.layer.backgroundColor = evaluationColor(evaluationPerspective_).CGColor;

    [self.evaluationTrack addSubview:self.evaluationPlayerFill];
    self.evaluationPlayerFillWidthConstraint = [self.evaluationPlayerFill.widthAnchor constraintEqualToConstant:0.0];

    self.evaluationToggleButton = makeButton(evaluationVisible_ ? @"Hide Evaluation" : @"Show Evaluation",
                                             self,
                                             @selector(toggleEvaluation:));

    NSButton* resignButton = makeButton(@"Resign", self, @selector(resignGame:));
    NSButton* returnButton = makeButton(@"Return to Main Menu", self, @selector(returnToMainMenu:));

    [self.evaluationPanel addSubview:self.evaluationTitleLabel];
    [self.evaluationPanel addSubview:self.evaluationTrack];
    [self.evaluationPanel addSubview:self.evaluationValueLabel];
    self.promotionChoiceHeightConstraint = [self.promotionChoiceView.heightAnchor constraintEqualToConstant:0.0];

    [root addSubview:board];
    [root addSubview:sidePanel];
    for (NSView* view in @[
             gameLabel,
             summary,
             self.statusLabel,
             self.messageLabel,
             self.promotionChoiceView,
             self.evaluationPanel,
             self.flipBoardButton,
             self.autoFlipCheckbox,
             historyButtons,
             self.evaluationToggleButton,
             resignButton,
             returnButton,
         ]) {
        [sidePanel addSubview:view];
    }

    [NSLayoutConstraint activateConstraints:@[
        [board.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:32.0],
        [board.centerYAnchor constraintEqualToAnchor:root.centerYAnchor],
        [board.widthAnchor constraintEqualToAnchor:board.heightAnchor],
        [board.widthAnchor constraintGreaterThanOrEqualToConstant:420.0],
        [board.widthAnchor constraintLessThanOrEqualToConstant:560.0],
        [board.topAnchor constraintGreaterThanOrEqualToAnchor:root.topAnchor constant:28.0],
        [board.bottomAnchor constraintLessThanOrEqualToAnchor:root.bottomAnchor constant:-28.0],

        [sidePanel.leadingAnchor constraintEqualToAnchor:board.trailingAnchor constant:28.0],
        [sidePanel.trailingAnchor constraintLessThanOrEqualToAnchor:root.trailingAnchor constant:-32.0],
        [sidePanel.topAnchor constraintEqualToAnchor:board.topAnchor],
        [sidePanel.bottomAnchor constraintEqualToAnchor:board.bottomAnchor],
        [sidePanel.widthAnchor constraintGreaterThanOrEqualToConstant:220.0],
        [sidePanel.widthAnchor constraintLessThanOrEqualToConstant:360.0],

        [gameLabel.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [gameLabel.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [gameLabel.topAnchor constraintEqualToAnchor:sidePanel.topAnchor constant:10.0],

        [summary.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [summary.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [summary.topAnchor constraintEqualToAnchor:gameLabel.bottomAnchor constant:16.0],

        [self.statusLabel.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [self.statusLabel.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [self.statusLabel.topAnchor constraintEqualToAnchor:summary.bottomAnchor constant:26.0],

        [self.messageLabel.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [self.messageLabel.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [self.messageLabel.topAnchor constraintEqualToAnchor:self.statusLabel.bottomAnchor constant:8.0],

        [self.promotionChoiceView.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [self.promotionChoiceView.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [self.promotionChoiceView.topAnchor constraintEqualToAnchor:self.messageLabel.bottomAnchor constant:14.0],
        self.promotionChoiceHeightConstraint,

        [promotionPrompt.leadingAnchor constraintEqualToAnchor:self.promotionChoiceView.leadingAnchor],
        [promotionPrompt.trailingAnchor constraintEqualToAnchor:self.promotionChoiceView.trailingAnchor],
        [promotionPrompt.topAnchor constraintEqualToAnchor:self.promotionChoiceView.topAnchor],

        [promotionButtons.leadingAnchor constraintEqualToAnchor:self.promotionChoiceView.leadingAnchor],
        [promotionButtons.trailingAnchor constraintEqualToAnchor:self.promotionChoiceView.trailingAnchor],
        [promotionButtons.topAnchor constraintEqualToAnchor:promotionPrompt.bottomAnchor constant:8.0],
        [promotionButtons.heightAnchor constraintEqualToConstant:30.0],

        [self.evaluationPanel.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [self.evaluationPanel.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [self.evaluationPanel.topAnchor constraintEqualToAnchor:self.promotionChoiceView.bottomAnchor constant:18.0],
        self.evaluationPanelHeightConstraint,

        [self.evaluationTitleLabel.leadingAnchor constraintEqualToAnchor:self.evaluationPanel.leadingAnchor],
        [self.evaluationTitleLabel.trailingAnchor constraintEqualToAnchor:self.evaluationPanel.trailingAnchor],
        [self.evaluationTitleLabel.topAnchor constraintEqualToAnchor:self.evaluationPanel.topAnchor],

        [self.evaluationTrack.leadingAnchor constraintEqualToAnchor:self.evaluationPanel.leadingAnchor],
        [self.evaluationTrack.trailingAnchor constraintEqualToAnchor:self.evaluationPanel.trailingAnchor],
        [self.evaluationTrack.topAnchor constraintEqualToAnchor:self.evaluationTitleLabel.bottomAnchor constant:8.0],
        [self.evaluationTrack.heightAnchor constraintEqualToConstant:18.0],

        [self.evaluationPlayerFill.trailingAnchor constraintEqualToAnchor:self.evaluationTrack.trailingAnchor],
        [self.evaluationPlayerFill.topAnchor constraintEqualToAnchor:self.evaluationTrack.topAnchor],
        [self.evaluationPlayerFill.bottomAnchor constraintEqualToAnchor:self.evaluationTrack.bottomAnchor],
        self.evaluationPlayerFillWidthConstraint,

        [self.evaluationValueLabel.leadingAnchor constraintEqualToAnchor:self.evaluationPanel.leadingAnchor],
        [self.evaluationValueLabel.trailingAnchor constraintEqualToAnchor:self.evaluationPanel.trailingAnchor],
        [self.evaluationValueLabel.topAnchor constraintEqualToAnchor:self.evaluationTrack.bottomAnchor constant:8.0],

        [self.flipBoardButton.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [self.flipBoardButton.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [self.flipBoardButton.topAnchor constraintEqualToAnchor:self.evaluationPanel.bottomAnchor constant:28.0],
        [self.flipBoardButton.heightAnchor constraintEqualToConstant:32.0],

        [self.autoFlipCheckbox.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [self.autoFlipCheckbox.topAnchor constraintEqualToAnchor:self.flipBoardButton.bottomAnchor constant:10.0],

        [historyButtons.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [historyButtons.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [historyButtons.topAnchor constraintEqualToAnchor:self.autoFlipCheckbox.bottomAnchor constant:18.0],
        [historyButtons.heightAnchor constraintEqualToConstant:30.0],

        [self.evaluationToggleButton.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [self.evaluationToggleButton.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [self.evaluationToggleButton.topAnchor constraintEqualToAnchor:historyButtons.bottomAnchor constant:22.0],
        [self.evaluationToggleButton.heightAnchor constraintEqualToConstant:32.0],
        [self.evaluationToggleButton.bottomAnchor constraintLessThanOrEqualToAnchor:resignButton.topAnchor constant:-36.0],

        [resignButton.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [resignButton.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [resignButton.bottomAnchor constraintEqualToAnchor:returnButton.topAnchor constant:-18.0],
        [resignButton.heightAnchor constraintEqualToConstant:34.0],

        [returnButton.leadingAnchor constraintEqualToAnchor:sidePanel.leadingAnchor],
        [returnButton.trailingAnchor constraintEqualToAnchor:sidePanel.trailingAnchor],
        [returnButton.bottomAnchor constraintEqualToAnchor:sidePanel.bottomAnchor constant:-10.0],
        [returnButton.heightAnchor constraintEqualToConstant:34.0],
    ]];

    [sidePanel layoutSubtreeIfNeeded];
    [self syncHistoryBoardDisplay];
    [self setEvaluationContentHidden:!evaluationVisible_];
    if (evaluationVisible_) {
        [self updateEvaluationDisplayAnimated:NO mateSearchPly:3];
    }
}

- (BOOL)shouldFlipBoardForCurrentTurn {
    const chess::ChessGame* displayGame = [self displayedGame];
    if (autoFlipEnabled_ && displayGame != nullptr) {
        return displayGame->sideToMove() == chess::Color::Black;
    }

    if (botMode_) {
        return humanColor_ == chess::Color::Black;
    }

    return NO;
}

- (void)applyAutoFlipForCurrentTurn {
    const chess::ChessGame* displayGame = [self displayedGame];
    if (!autoFlipEnabled_ || self.boardView == nil || displayGame == nullptr) {
        return;
    }

    self.boardView.boardFlipped = displayGame->sideToMove() == chess::Color::Black;
    self.boardView.needsDisplay = YES;
}

- (void)flipBoard:(id)sender {
    (void)sender;
    if (self.boardView == nil) {
        return;
    }

    [self.boardView clearSelection];
    self.boardView.boardFlipped = !self.boardView.boardFlipped;
    self.boardView.needsDisplay = YES;
}

- (void)autoFlipChanged:(id)sender {
    (void)sender;

    autoFlipEnabled_ = self.autoFlipCheckbox.state == NSControlStateValueOn;
    if (autoFlipEnabled_) {
        [self applyAutoFlipForCurrentTurn];
    }
}

- (chess::Evaluation)currentEvaluationWithMateSearchPly:(int)maxMateSearchPly {
    if (analysisMode_) {
        return chess::evaluatePosition(analysisBoard_, analysisBoard_.sideToMove(), maxMateSearchPly);
    }

    const chess::ChessGame* displayGame = [self displayedGame];
    if (displayGame == nullptr) {
        return {};
    }

    return chess::evaluatePosition(displayGame->board(), evaluationPerspective_, maxMateSearchPly);
}

- (void)setEvaluationContentHidden:(BOOL)hidden {
    self.evaluationPanel.hidden = hidden;
    self.evaluationTitleLabel.hidden = hidden;
    self.evaluationTrack.hidden = hidden;
    self.evaluationValueLabel.hidden = hidden;
    self.evaluationPanelHeightConstraint.constant = hidden ? 0.0 : 82.0;
    self.evaluationToggleButton.title = hidden ? @"Show Evaluation" : @"Hide Evaluation";
    [self.gameView layoutSubtreeIfNeeded];
}

- (void)updateEvaluationDisplayAnimated:(BOOL)animated mateSearchPly:(int)maxMateSearchPly {
    if (self.evaluationTrack == nil || self.evaluationPlayerFillWidthConstraint == nil) {
        return;
    }

    const chess::Evaluation evaluation = [self currentEvaluationWithMateSearchPly:maxMateSearchPly];
    if (self.evaluationValueLabel != nil) {
        self.evaluationValueLabel.stringValue = evaluationText(evaluation);
    }

    CGFloat trackWidth = NSWidth(self.evaluationTrack.bounds);
    if (trackWidth <= 0.0) {
        trackWidth = 220.0;
    }

    CGFloat targetWidth = 0.0;
    if (evaluation.forcedMate) {
        self.evaluationPlayerFill.hidden = YES;
        self.evaluationTrack.layer.backgroundColor = evaluationColor(evaluation.forcedMate->winner).CGColor;
    } else {
        self.evaluationPlayerFill.hidden = NO;
        self.evaluationTrack.layer.backgroundColor = evaluationColor(chess::opposite(evaluationPerspective_)).CGColor;
        self.evaluationPlayerFill.layer.backgroundColor = evaluationColor(evaluationPerspective_).CGColor;
        targetWidth = std::floor(trackWidth * evaluationShareForCentipawns(evaluation.centipawns));
    }

    if (!animated) {
        self.evaluationPlayerFillWidthConstraint.constant = targetWidth;
        [self.evaluationTrack layoutSubtreeIfNeeded];
        return;
    }

    [NSAnimationContext runAnimationGroup:^(NSAnimationContext* context) {
        context.duration = 0.25;
        context.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
        [[self.evaluationPlayerFillWidthConstraint animator] setConstant:targetWidth];
        [self.evaluationTrack layoutSubtreeIfNeeded];
    } completionHandler:nil];
}

- (void)stopEvaluationLoop {
    ++evaluationGeneration_;
}

- (void)runEvaluationTickForGeneration:(int)generation tick:(int)tick {
    if (analysisMode_) {
        if (generation != evaluationGeneration_ || !evaluationVisible_) {
            return;
        }
        [self updateEvaluationDisplayAnimated:tick != 0 mateSearchPly:mateSearchPlyForTick(tick)];
        return;
    }

    const chess::ChessGame* displayGame = [self displayedGame];
    if (generation != evaluationGeneration_ || !evaluationVisible_ || displayGame == nullptr) {
        return;
    }

    [self updateEvaluationDisplayAnimated:tick != 0 mateSearchPly:mateSearchPlyForTick(tick)];

    if (tick >= 20 || isTerminal(displayGame->status())) {
        return;
    }

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(NSEC_PER_SEC)),
                   dispatch_get_main_queue(),
                   ^{
                       [self runEvaluationTickForGeneration:generation tick:tick + 1];
                   });
}

- (void)restartEvaluationLoop {
    [self stopEvaluationLoop];
    if (!evaluationVisible_ || (!analysisMode_ && [self displayedGame] == nullptr)) {
        return;
    }

    const int generation = ++evaluationGeneration_;
    [self runEvaluationTickForGeneration:generation tick:0];
}

- (void)toggleEvaluation:(id)sender {
    (void)sender;

    evaluationVisible_ = !evaluationVisible_;
    [self setEvaluationContentHidden:!evaluationVisible_];
    if (evaluationVisible_) {
        [self restartEvaluationLoop];
    } else {
        [self stopEvaluationLoop];
    }
}

- (BOOL)isGameFinished {
    return gameEnded_ || (game_ && isTerminal(game_->status()));
}

- (void)setGameMessage:(NSString*)message {
    if (self.messageLabel == nil) {
        return;
    }

    self.messageLabel.stringValue = message ?: @"";
}

- (void)clearPromotionChoice {
    pendingPromotionCandidates_.clear();
    promotionPending_ = NO;
    self.promotionChoiceView.hidden = YES;
    self.promotionChoiceHeightConstraint.constant = 0.0;
    [self updateHistoryButtons];
}

- (void)showPromotionChoices:(const std::vector<chess::Move>&)candidates {
    pendingPromotionCandidates_ = candidates;
    promotionPending_ = YES;
    self.promotionChoiceView.hidden = NO;
    self.promotionChoiceHeightConstraint.constant = 66.0;
    [self setGameMessage:@"Choose a promotion piece."];
    [self updateHistoryButtons];
}

- (BOOL)commitMove:(const chess::Move&)move message:(NSString*)message {
    if (!game_ || [self isGameFinished]) {
        [self setGameMessage:@"The game is already over."];
        return NO;
    }
    if (![self isViewingLatestPosition]) {
        [self setGameMessage:@"Return to the latest position to move."];
        return NO;
    }

    const chess::MoveResult result = game_->playMove(move);
    if (!result.ok) {
        [self setGameMessage:@"Illegal move."];
        return NO;
    }
    const auto humanMoveCommittedAt = Clock::now();

    historyCursor_ = game_->moveCount();
    [self syncHistoryBoardDisplay];
    [self updateStatusLabel];
    [self setGameMessage:isTerminal(result.status) ? @"Game over." : message];
    [self restartEvaluationLoop];

    if (botMode_ && !isTerminal(result.status)) {
        const auto earliestBotMoveAt = humanMoveCommittedAt + MinimumBotMoveDelay;
        dispatch_async(dispatch_get_main_queue(), ^{
            [self playBotMoveIfNeededWithEarliestMoveAt:earliestBotMoveAt];
        });
    }

    return YES;
}

- (void)finishPendingPromotionWithPiece:(chess::PieceType)pieceType {
    if (!promotionPending_) {
        [self setGameMessage:@"No promotion is pending."];
        return;
    }

    const std::optional<chess::Move> move = chooseCandidate(pendingPromotionCandidates_, pieceType);
    [self clearPromotionChoice];
    if (!move) {
        [self setGameMessage:@"Promotion failed."];
        return;
    }

    NSString* message = [NSString stringWithFormat:@"Pawn promoted to %@.", promotionTitle(move->promotion)];
    [self commitMove:*move message:message];
}

- (void)promotionPieceSelected:(NSButton*)sender {
    [self finishPendingPromotionWithPiece:static_cast<chess::PieceType>(sender.tag)];
}

- (void)returnToMainMenu:(id)sender {
    (void)sender;

    [self stopEvaluationLoop];
    ++botMoveGeneration_;
    game_.reset();
    viewedGame_.reset();
    analysisMode_ = NO;
    activeBot_ = nullptr;
    botMode_ = NO;
    pendingPromotionCandidates_.clear();
    historyCursor_ = 0;
    promotionPending_ = NO;
    gameEnded_ = NO;
    self.gameOverMessage = nil;
    self.boardView = nil;
    self.statusLabel = nil;
    self.messageLabel = nil;
    self.promotionChoiceView = nil;
    self.promotionChoiceHeightConstraint = nil;
    self.themePopup = nil;
    self.autopromoteCheckbox = nil;
    self.autopromotePopup = nil;
    self.lightSquareField = nil;
    self.darkSquareField = nil;
    self.lightSquareWell = nil;
    self.darkSquareWell = nil;
    self.evaluationPanel = nil;
    self.evaluationTitleLabel = nil;
    self.evaluationValueLabel = nil;
    self.evaluationTrack = nil;
    self.evaluationPlayerFill = nil;
    self.evaluationPlayerFillWidthConstraint = nil;
    self.evaluationPanelHeightConstraint = nil;
    self.evaluationToggleButton = nil;
    self.flipBoardButton = nil;
    self.autoFlipCheckbox = nil;
    self.historyBackButton = nil;
    self.historyForwardButton = nil;
    self.historyLatestButton = nil;
    self.analysisSideControl = nil;
    self.analysisPaletteView = nil;
    self.bestMovesLabel = nil;
    self.settingsView = nil;
    self.currentSummary = nil;
    [self showSetupView];
}

- (NSString*)statusTextForGame:(const chess::ChessGame&)game {
    const chess::GameStatus status = game.status();
    switch (status) {
    case chess::GameStatus::Ongoing:
        return [NSString stringWithFormat:@"%@ to move", colorToNSString(game.sideToMove())];
    case chess::GameStatus::Check:
        return [NSString stringWithFormat:@"%@ to move, in check", colorToNSString(game.sideToMove())];
    case chess::GameStatus::Checkmate:
        return [NSString stringWithFormat:@"Checkmate. %@ wins.", colorToNSString(chess::opposite(game.sideToMove()))];
    case chess::GameStatus::Stalemate:
        return @"Draw by stalemate.";
    case chess::GameStatus::FiftyMoveDraw:
        return @"Draw by fifty-move rule.";
    case chess::GameStatus::InsufficientMaterialDraw:
        return @"Draw by insufficient material.";
    }
    return @"Ready";
}

- (NSString*)statusText {
    if (!game_) {
        return @"Ready";
    }

    if (![self isViewingLatestPosition]) {
        const chess::ChessGame* displayGame = [self displayedGame];
        NSString* status = displayGame == nullptr ? @"Ready" : [self statusTextForGame:*displayGame];
        return [NSString stringWithFormat:@"Position %lu of %lu: %@",
                                          static_cast<unsigned long>(historyCursor_),
                                          static_cast<unsigned long>(game_->moveCount()),
                                          status];
    }

    if (gameEnded_) {
        return self.gameOverMessage ?: @"Game over.";
    }

    return [self statusTextForGame:*game_];
}

- (void)updateStatusLabel {
    if (self.statusLabel == nil) {
        return;
    }

    if (analysisMode_) {
        [self updateAnalysisStatus];
        return;
    }

    self.statusLabel.stringValue = [self statusText];
}

- (void)boardView:(ChessBoardView*)boardView placeAnalysisPieceAtSquare:(chess::Square)square {
    (void)boardView;
    if (!analysisMode_) {
        return;
    }

    analysisBoard_.setPieceAt(square, selectedAnalysisPiece_);
    [self refreshAnalysisBoard];
}

- (void)boardView:(ChessBoardView*)boardView removeAnalysisPieceAtSquare:(chess::Square)square {
    (void)boardView;
    if (!analysisMode_) {
        return;
    }

    analysisBoard_.setPieceAt(square, {});
    [self refreshAnalysisBoard];
}

- (BOOL)boardView:(ChessBoardView*)boardView canSelectSquare:(chess::Square)square {
    (void)boardView;
    if (!game_ || [self isGameFinished] || promotionPending_) {
        return NO;
    }
    if (![self isViewingLatestPosition]) {
        return NO;
    }

    const chess::Piece piece = game_->board().pieceAt(square);
    if (piece.isEmpty() || piece.color != game_->sideToMove()) {
        return NO;
    }

    return !botMode_ || piece.color == humanColor_;
}

- (BOOL)boardView:(ChessBoardView*)boardView tryMoveFrom:(chess::Square)from to:(chess::Square)to {
    (void)boardView;
    if (!game_) {
        return NO;
    }
    if (![self isViewingLatestPosition]) {
        [self setGameMessage:@"Return to the latest position to move."];
        return NO;
    }
    if ([self isGameFinished]) {
        [self setGameMessage:@"The game is already over."];
        return NO;
    }
    if (promotionPending_) {
        [self setGameMessage:@"Choose a promotion piece first."];
        return NO;
    }
    if (botMode_ && game_->sideToMove() != humanColor_) {
        [self setGameMessage:@"Wait for the bot to move."];
        return NO;
    }

    const std::vector<chess::Move> candidates = legalMoveCandidates(game_->board(), from, to);
    if (candidates.empty()) {
        [self setGameMessage:@"Illegal move."];
        return NO;
    }

    if (requiresPromotionChoice(candidates)) {
        if (autopromoteEnabled_) {
            const std::optional<chess::Move> promotedMove = chooseCandidate(candidates, autopromotePiece_);
            if (!promotedMove) {
                [self setGameMessage:@"Promotion failed."];
                return NO;
            }

            NSString* message = [NSString stringWithFormat:@"Pawn promoted to %@.", promotionTitle(promotedMove->promotion)];
            return [self commitMove:*promotedMove message:message];
        }

        [self showPromotionChoices:candidates];
        return YES;
    }

    const std::optional<chess::Move> move = chooseCandidate(candidates, chess::PieceType::Queen);
    if (!move) {
        [self setGameMessage:@"Illegal move."];
        return NO;
    }

    return [self commitMove:*move message:@""];
}

- (void)playBotMoveIfNeeded {
    [self playBotMoveIfNeededWithEarliestMoveAt:Clock::now()];
}

- (void)playBotMoveIfNeededWithEarliestMoveAt:(Clock::time_point)earliestMoveAt {
    if (!game_ || !botMode_ || activeBot_ == nullptr ||
        game_->sideToMove() != botColor_ || [self isGameFinished]) {
        return;
    }

    const chess::Board board = game_->board();
    const chess::ChessBot* bot = activeBot_;
    const int generation = ++botMoveGeneration_;

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        const std::optional<chess::Move> botMove = bot->chooseMove(board);
        const int64_t delay = delayNanosecondsUntil(earliestMoveAt);

        void (^applyBotMove)(void) = ^{
            if (generation != botMoveGeneration_ ||
                !game_ ||
                !botMode_ ||
                activeBot_ != bot ||
                game_->sideToMove() != botColor_ ||
                [self isGameFinished]) {
                return;
            }

            if (botMove) {
                const chess::MoveResult result = game_->playMove(*botMove);
                if (!result.ok) {
                    return;
                }
            }

            historyCursor_ = game_->moveCount();
            [self syncHistoryBoardDisplay];
            [self updateStatusLabel];
            if (isTerminal(game_->status())) {
                [self setGameMessage:@"Game over."];
            }
            [self restartEvaluationLoop];
        };

        if (delay > 0) {
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay), dispatch_get_main_queue(), applyBotMove);
        } else {
            dispatch_async(dispatch_get_main_queue(), applyBotMove);
        }
    });
}

- (void)resignGame:(id)sender {
    (void)sender;

    if (!game_) {
        return;
    }
    if ([self isGameFinished]) {
        [self setGameMessage:@"The game is already over."];
        return;
    }

    [self stopEvaluationLoop];
    ++botMoveGeneration_;
    [self clearPromotionChoice];

    const chess::Color loser = botMode_ ? humanColor_ : game_->sideToMove();
    const chess::Color winner = chess::opposite(loser);
    gameEnded_ = YES;
    self.gameOverMessage = [NSString stringWithFormat:@"%@ resigns. %@ wins.",
                                                      colorToNSString(loser),
                                                      colorToNSString(winner)];
    [self.boardView clearSelection];
    self.boardView.needsDisplay = YES;
    [self updateStatusLabel];
    [self setGameMessage:@"Game over."];
}

@end

int main(int argc, const char* argv[]) {
    (void)argc;
    (void)argv;

    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        ChessAppDelegate* delegate = [[ChessAppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
    }

    return 0;
}
