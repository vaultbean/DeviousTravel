// Generates Devious Travel's roadside ambush points from the active load order, one file per
// worldspace (RoadPoints/<worldspace editor ID>.csv): roads come from navmesh triangles flagged
// "Preferred"; points are spaced along them, clear of settlements, locations, doors, map markers
// and placed actors, on flat walkable ground.
//
// Usage (from anywhere in the repo):
//   dotnet run --project patcher [-- [worldspace editor IDs...] [--out <dir>] [--spacing <cells>]]
// Worldspaces default to Tamriel and DLC2SolstheimWorld; the output defaults to the repo's
// dist/SKSE/Plugins/DeviousTravel/RoadPoints, where xmake install picks it up.
using DeviousTravelRoads;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Environments;
using Mutagen.Bethesda.Skyrim;

var outDir = DefaultOutDir();
var settings = new Settings();
var worldspaces = new List<string>();
for (int i = 0; i < args.Length; i++)
{
    if (args[i] == "--spacing" && i + 1 < args.Length) settings.SpacingCells = float.Parse(args[++i]);
    else if (args[i] == "--out" && i + 1 < args.Length) outDir = args[++i];
    else worldspaces.Add(args[i]);
}
if (worldspaces.Count == 0) worldspaces.AddRange(new[] { "Tamriel", "DLC2SolstheimWorld" });
Directory.CreateDirectory(outDir);

using var env = GameEnvironment.Typical.Skyrim(SkyrimRelease.SkyrimSE);
var byEditorId = env.LoadOrder.PriorityOrder.Worldspace().WinningOverrides()
    .Where(w => w.EditorID != null)
    .GroupBy(w => w.EditorID!, StringComparer.OrdinalIgnoreCase)
    .ToDictionary(g => g.Key, g => g.First(), StringComparer.OrdinalIgnoreCase);

foreach (var editorId in worldspaces)
{
    if (!byEditorId.TryGetValue(editorId, out var ws))
    {
        Console.WriteLine($"{editorId}: worldspace not found in the load order, skipped");
        continue;
    }
    var world = WorldData.Load(env, ws.FormKey);
    var markers = MarkerPlanner.Plan(world, settings);
    var path = Path.Combine(outDir, $"{ws.EditorID}.csv");
    Output.WritePoints(path, markers, settings, env.LoadOrder.ListedOrder.Count(), $"{ws.EditorID} ({ws.FormKey})");
    Console.WriteLine($"{ws.EditorID}: wrote {markers.Count} road points to {path}");
}

// The repo root is the nearest folder up from the working directory that has xmake.lua.
static string DefaultOutDir()
{
    for (var dir = new DirectoryInfo(Directory.GetCurrentDirectory()); dir != null; dir = dir.Parent)
    {
        if (File.Exists(Path.Combine(dir.FullName, "xmake.lua")))
            return Path.Combine(dir.FullName, "dist", "SKSE", "Plugins", "DeviousTravel", "RoadPoints");
    }
    return Directory.GetCurrentDirectory();
}

namespace DeviousTravelRoads
{
    public sealed class Settings
    {
        public float SpacingCells = 1.0f;          // minimum distance between markers
        public float MinRoadExtentCells = 2.0f;    // drop preferred patches smaller than this (town paths, courtyards)
        public float LinkDistance = 640f;          // preferred triangles closer than this belong to the same road
        public float DoorClearance = 1024f;        // stay away from doors (dungeon/house entrances)
        public float MapMarkerClearance = 1536f;   // stay away from map markers (points of interest)
        public float LocationMargin = 2048f;       // stay this far outside every location's radius
        public float PersonClearance = 4096f;      // stay away from placed people (and leveled actors)
        public float CreatureClearance = 1536f;    // stay away from placed hostile creatures (0 = ignore them)
        public float RoomRadius = 512f;            // area checked for walkable ground around a marker
        public float MinNavCoverage = 0.5f;        // fraction of that area that must be navmesh
        public float MaxHeightRange = 320f;        // max height difference of navmesh within RoomRadius
        public float MaxMeanSlopeDeg = 15f;        // max area-weighted mean slope of navmesh within RoomRadius
        public float SteepSlopeDeg = 25f;          // ground steeper than this counts as "steep"
        public float MaxSteepFraction = 0.2f;      // max share of steep ground within RoomRadius
        public float HeightOffset = 32f;           // place markers this far above the navmesh
    }
}
