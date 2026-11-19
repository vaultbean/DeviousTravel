-- DeviousTravel SKSE plugin
-- Build:   xmake            (first run: xmake f -m releasedbg)
-- Deploy:  with XSE_TES5_GAME_PATH (or XSE_TES5_MODS_PATH) set, each build copies the DLL, PDB
--          and everything under dist/ (road points) into the game's Data folder (or the mod).
-- Road points: dotnet run --project patcher   (regenerates dist/SKSE/Plugins/DeviousTravel/RoadPoints/<worldspace>.csv)

set_xmakever("3.0.0")

includes("lib/commonlibsse-ng")

set_project("DeviousTravel")
set_version("0.1.0")

set_defaultplat("windows")  -- otherwise xmake picks MinGW when run from Git Bash
set_arch("x64")
set_languages("c++23")
set_warnings("allextra")
set_encodings("utf-8")

add_rules("mode.debug", "mode.releasedbg")
set_defaultmode("releasedbg")

add_requires("minhook")

target("DeviousTravel")
    add_rules("commonlibsse-ng.plugin", {
        name = "DeviousTravel",
        author = "",  -- fill in
        description = "Redirects map and carriage fast travel into roadside ambushes"
    })

    add_packages("minhook")
    add_installfiles("dist/(**)")

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/PCH.h")
