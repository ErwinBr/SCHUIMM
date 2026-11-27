#pragma once

// The project-wide compilation of a game: the build step (schuimmc --game, and later a DevKit)
// that turns a game's script sources into its one compiled program. Not part of the shipped
// runtime: it reads sources, which a game does not ship.
//
//   1. Discovery: the game resource's global scripts (role global, in listed order), then for
//      each listed room, in order, its room script (role room) and the script of each actor
//      placement's actor resource (role actor). Each source takes part once, whatever number of
//      rooms and placements refer to it; a source cannot have two roles. The resources are read
//      and validated with the engine's own loaders, so a broken resource fails the build.
//   2. Compilation: compileProject over all sources, with the adventure interface, into one
//      linked program (Compiler.md, "Project compilation"). Every source is named by its asset
//      path, the identity resources refer to it by.
//
// The library depends on the compiler and the resource loaders, never on graphics, audio or a
// live game; the compiler library itself depends on none of those.

#include "Adventure/GameResource.h"
#include "Core/Common/Error.h"
#include "Resource/AssetLocator.h"
#include "Resource/AssetPath.h"
#include "Script/Compiler/Compiler.h"

#include <memory>
#include <vector>

namespace schuimm::adventure {

// The participating sources of a game (step 1). The SourceFiles own the text the compile result
// refers to.
struct GameSources {
    GameResource game;
    std::vector<std::unique_ptr<script::SourceFile>> files;
    std::vector<script::ProjectSource> sources; // One per file, with its role.
};

// Errors: the locator's for a missing resource or source, the loaders' for an invalid one, and
// InvalidData for a source used in two roles.
[[nodiscard]] Result<GameSources> discoverGameSources(const AssetLocator& locator, const AssetPath& game);

struct GameBuild {
    GameSources sources;
    script::CompileResult result; // result.success and result.module: the game's program.
};

// Discovery and compilation (step 1 and 2). An Error for a discovery failure; compile errors are
// diagnostics in result (formatDiagnostics(result) prints them with their files).
[[nodiscard]] Result<std::unique_ptr<GameBuild>> compileGame(const AssetLocator& locator, const AssetPath& game,
                                                             bool optimize = true);

} // namespace schuimm::adventure
