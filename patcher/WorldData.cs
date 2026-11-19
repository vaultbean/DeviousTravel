using Mutagen.Bethesda;
using Mutagen.Bethesda.Environments;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Cache;
using Mutagen.Bethesda.Skyrim;
using Noggog;

namespace DeviousTravelRoads;

// Centroid, horizontal area, and slope in degrees from horizontal (0 = flat, 90 = vertical).
public readonly record struct Tri(float X, float Y, float Z, float Area, float SlopeDeg);

public readonly record struct Point2(float X, float Y);

// A location's centre (its world location marker) and its radius (0 if the location sets none).
public readonly record struct LocationArea(float X, float Y, float Radius);

// Everything the planner needs about one worldspace, resolved against the load order.
public sealed class WorldData
{
    readonly FormKey _world;

    WorldData(FormKey world) => _world = world;

    // Location keywords that mark places we don't want ambushes in.
    static readonly string[] ExcludedLocationKeywords =
    {
        "LocTypeCity", "LocTypeTown", "LocTypeSettlement", "LocTypeHabitation",
        "LocTypeDwelling", "LocTypeFarm", "LocTypeInn", "LocTypePlayerHouse",
    };

    public List<Tri> Triangles { get; } = new();   // every walkable triangle (not deleted, not water)
    public List<Tri> Preferred { get; } = new();   // the subset flagged Preferred
    public Dictionary<(int X, int Y), FormKey?> CellLocations { get; } = new();
    public HashSet<FormKey> ExcludedLocations { get; } = new();
    public List<Point2> Doors { get; } = new();
    public List<Point2> MapMarkers { get; } = new();
    public List<LocationArea> Locations { get; } = new();  // every location below hold level with a marker in this worldspace
    public List<Point2> People { get; } = new();           // placed actors that are (or may spawn) people
    public List<Point2> Creatures { get; } = new();        // placed creatures that attack on sight

    public bool IsExcludedCell(float x, float y)
    {
        var key = ((int)MathF.Floor(x / 4096f), (int)MathF.Floor(y / 4096f));
        return CellLocations.TryGetValue(key, out var loc) && loc is { } fk && ExcludedLocations.Contains(fk);
    }

    public static WorldData Load(IGameEnvironment<ISkyrimMod, ISkyrimModGetter> env, FormKey world)
    {
        var data = new WorldData(world);
        data.LoadNavmesh(env);
        var locationMarkers = data.CollectLocationMarkers(env);
        data.LoadCellsAndReferences(env, locationMarkers);
        data.ResolveExcludedLocations(env);
        return data;
    }

    void LoadNavmesh(IGameEnvironment<ISkyrimMod, ISkyrimModGetter> env)
    {
        foreach (var nav in env.LoadOrder.PriorityOrder.NavigationMesh().WinningOverrides())
        {
            var mesh = nav.Data;
            if (mesh?.Parent is not IWorldspaceNavmeshParentGetter parent || parent.Parent.FormKey != _world)
                continue;
            var verts = mesh.Vertices;
            foreach (var tri in mesh.Triangles)
            {
                if (tri.Flags.HasFlag(NavmeshTriangle.Flag.Deleted) || tri.Flags.HasFlag(NavmeshTriangle.Flag.Water))
                    continue;
                int i0 = tri.Vertices.X, i1 = tri.Vertices.Y, i2 = tri.Vertices.Z;
                if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= verts.Count || i1 >= verts.Count || i2 >= verts.Count)
                    continue;
                P3Float a = verts[i0], b = verts[i1], c = verts[i2];
                var t = new Tri((a.X + b.X + c.X) / 3f, (a.Y + b.Y + c.Y) / 3f, (a.Z + b.Z + c.Z) / 3f,
                    MathF.Abs((b.X - a.X) * (c.Y - a.Y) - (c.X - a.X) * (b.Y - a.Y)) / 2f, SlopeDegrees(a, b, c));
                Triangles.Add(t);
                if (tri.Flags.HasFlag(NavmeshTriangle.Flag.Preferred))
                    Preferred.Add(t);
            }
        }
    }

    // Angle between the triangle's plane and the horizontal: acos of the normal's vertical part.
    static float SlopeDegrees(P3Float a, P3Float b, P3Float c)
    {
        float ux = b.X - a.X, uy = b.Y - a.Y, uz = b.Z - a.Z;
        float vx = c.X - a.X, vy = c.Y - a.Y, vz = c.Z - a.Z;
        float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        float len = MathF.Sqrt(nx * nx + ny * ny + nz * nz);
        if (len <= 0f) return 90f;   // degenerate triangle: treat as unusable
        return MathF.Acos(Math.Clamp(MathF.Abs(nz) / len, 0f, 1f)) * 180f / MathF.PI;
    }

    // Location marker reference -> the locations (below hold level) that use it as their centre.
    Dictionary<FormKey, List<float>> CollectLocationMarkers(IGameEnvironment<ISkyrimMod, ISkyrimModGetter> env)
    {
        var holdKeyword = env.LoadOrder.PriorityOrder.Keyword().WinningOverrides()
            .FirstOrDefault(k => string.Equals(k.EditorID, "LocTypeHold", StringComparison.OrdinalIgnoreCase))?.FormKey;
        var markers = new Dictionary<FormKey, List<float>>();
        foreach (var loc in env.LoadOrder.PriorityOrder.Location().WinningOverrides())
        {
            if (holdKeyword is { } hold && loc.Keywords?.Any(k => k.FormKey == hold) == true)
                continue;
            if (loc.WorldLocationMarkerRef.FormKeyNullable is not { } marker)
                continue;
            if (!markers.TryGetValue(marker, out var list)) markers[marker] = list = new();
            list.Add(loc.WorldLocationRadius ?? 0f);
        }
        return markers;
    }

    [Flags]
    enum RefKind { None = 0, Door = 1, MapMarker = 2, LocationMarker = 4, Person = 8, Creature = 16 }

    enum ActorClass { Person, HostileCreature, HarmlessCreature }

    // Classifies what a placed actor's base is or can spawn:
    //   Person            race has ActorTypeNPC
    //   HostileCreature   any other race in PredatorFaction (vanilla predators are Unaggressive)
    //                     or with aggression Aggressive or worse
    //   HarmlessCreature  everything else
    // Templates are followed for traits, factions and AI data. A leveled list takes the most
    // cautious class of its entries; anything unresolved counts as a Person.
    sealed class ActorClassifier
    {
        readonly IGameEnvironment<ISkyrimMod, ISkyrimModGetter> _env;
        static readonly FormKey PredatorFaction = FormKey.Factory("02E893:Skyrim.esm");
        readonly FormKey? _personKeyword;
        readonly Dictionary<FormKey, ActorClass> _cache = new();

        public ActorClassifier(IGameEnvironment<ISkyrimMod, ISkyrimModGetter> env, FormKey? personKeyword)
        {
            _env = env;
            _personKeyword = personKeyword;
        }

        public ActorClass Classify(FormKey key, int depth = 0)
        {
            if (_cache.TryGetValue(key, out var known)) return known;
            if (depth > 8) return ActorClass.Person;
            var result = ActorClass.Person;
            if (_env.LinkCache.TryResolve<INpcGetter>(key, out var npc)) result = ClassifyNpc(npc, depth);
            else if (_env.LinkCache.TryResolve<ILeveledNpcGetter>(key, out var list)) result = ClassifyList(list, depth);
            return _cache[key] = result;
        }

        ActorClass ClassifyNpc(INpcGetter npc, int depth)
        {
            var flags = npc.Configuration.TemplateFlags;
            var template = npc.Template.FormKeyNullable;

            bool person;
            if (flags.HasFlag(NpcConfiguration.TemplateFlag.Traits) && template is { } traitsFrom)
                person = Classify(traitsFrom, depth + 1) == ActorClass.Person;
            else
                person = _personKeyword is { } kw && _env.LinkCache.TryResolve<IRaceGetter>(npc.Race.FormKey, out var race) &&
                         race.Keywords?.Any(k => k.FormKey == kw) == true;
            if (person) return ActorClass.Person;

            bool aggressive;
            if (flags.HasFlag(NpcConfiguration.TemplateFlag.AIData) && template is { } aiFrom)
                aggressive = Classify(aiFrom, depth + 1) != ActorClass.HarmlessCreature;
            else
                aggressive = npc.AIData.Aggression != Aggression.Unaggressive;

            bool predator;
            if (flags.HasFlag(NpcConfiguration.TemplateFlag.Factions) && template is { } factionsFrom)
                predator = Classify(factionsFrom, depth + 1) != ActorClass.HarmlessCreature;
            else
                predator = npc.Factions.Any(f => f.Faction.FormKey == PredatorFaction);

            return aggressive || predator ? ActorClass.HostileCreature : ActorClass.HarmlessCreature;
        }

        ActorClass ClassifyList(ILeveledNpcGetter list, int depth)
        {
            var classes = (list.Entries ?? Array.Empty<ILeveledNpcEntryGetter>())
                .Select(e => e.Data?.Reference.FormKey)
                .OfType<FormKey>()
                .Select(k => Classify(k, depth + 1))
                .ToList();
            if (classes.Contains(ActorClass.Person)) return ActorClass.Person;
            if (classes.Contains(ActorClass.HostileCreature)) return ActorClass.HostileCreature;
            return ActorClass.HarmlessCreature;
        }
    }

    // Walks every plugin's copy of the worldspace in load order so later plugins override earlier
    // ones: cell -> location, plus the positions of doors, map markers, location markers and
    // placed actors.
    void LoadCellsAndReferences(IGameEnvironment<ISkyrimMod, ISkyrimModGetter> env,
        Dictionary<FormKey, List<float>> locationMarkers)
    {
        var doorBases = env.LoadOrder.PriorityOrder.Door().WinningOverrides().Select(d => d.FormKey).ToHashSet();
        var personRaceKeyword = env.LoadOrder.PriorityOrder.Keyword().WinningOverrides()
            .FirstOrDefault(k => string.Equals(k.EditorID, "ActorTypeNPC", StringComparison.OrdinalIgnoreCase))?.FormKey;
        var classifier = new ActorClassifier(env, personRaceKeyword);
        var refs = new Dictionary<FormKey, (Point2 Pos, RefKind Kind)>();

        void Visit(IPlacedGetter placed)
        {
            RefKind kind = RefKind.None;
            P3Float? position = null;
            if (placed is IPlacedObjectGetter obj)
            {
                if (obj.Base.FormKeyNullable is { } b && doorBases.Contains(b)) kind |= RefKind.Door;
                if (obj.MapMarker != null) kind |= RefKind.MapMarker;
                position = obj.Placement?.Position;
            }
            else if (placed is IPlacedNpcGetter npcRef)
            {
                // Initially disabled actors only appear when a quest enables them; ignore them.
                const int InitiallyDisabled = 0x800;
                if ((npcRef.MajorRecordFlagsRaw & InitiallyDisabled) == 0 && npcRef.Base.FormKeyNullable is { } b)
                    kind |= classifier.Classify(b) switch
                    {
                        ActorClass.Person => RefKind.Person,
                        ActorClass.HostileCreature => RefKind.Creature,
                        _ => RefKind.None,
                    };
                position = npcRef.Placement?.Position;
            }
            if (locationMarkers.ContainsKey(placed.FormKey)) kind |= RefKind.LocationMarker;

            if (placed.IsDeleted || position is not { } pos || kind == RefKind.None)
            {
                refs.Remove(placed.FormKey);
                return;
            }
            refs[placed.FormKey] = (new Point2(pos.X, pos.Y), kind);
        }

        foreach (var listing in env.LoadOrder.ListedOrder)
        {
            var mod = listing.Mod;
            if (mod == null)
                continue;
            foreach (var ws in mod.Worldspaces)
            {
                if (ws.FormKey != _world)
                    continue;
                if (ws.TopCell is { } top)
                {
                    foreach (var p in top.Persistent) Visit(p);
                    foreach (var p in top.Temporary) Visit(p);
                }
                foreach (var block in ws.SubCells)
                foreach (var sub in block.Items)
                foreach (var cell in sub.Items)
                {
                    if (cell.Grid is { } grid)
                        CellLocations[(grid.Point.X, grid.Point.Y)] = cell.Location.FormKeyNullable;
                    foreach (var p in cell.Persistent) Visit(p);
                    foreach (var p in cell.Temporary) Visit(p);
                }
            }
        }

        foreach (var (key, (pos, kind)) in refs)
        {
            if (kind.HasFlag(RefKind.Door)) Doors.Add(pos);
            if (kind.HasFlag(RefKind.MapMarker)) MapMarkers.Add(pos);
            if (kind.HasFlag(RefKind.Person)) People.Add(pos);
            if (kind.HasFlag(RefKind.Creature)) Creatures.Add(pos);
            if (kind.HasFlag(RefKind.LocationMarker))
                foreach (var radius in locationMarkers[key])
                    Locations.Add(new LocationArea(pos.X, pos.Y, radius));
        }
    }

    // A cell is excluded if its location, or any parent below the hold, has one of the
    // settlement keywords.
    void ResolveExcludedLocations(IGameEnvironment<ISkyrimMod, ISkyrimModGetter> env)
    {
        var keywordByEditorId = env.LoadOrder.PriorityOrder.Keyword().WinningOverrides()
            .Where(k => k.EditorID != null)
            .GroupBy(k => k.EditorID!, StringComparer.OrdinalIgnoreCase)
            .ToDictionary(g => g.Key, g => g.First().FormKey, StringComparer.OrdinalIgnoreCase);
        var excludedKeywords = new HashSet<FormKey>();
        foreach (var name in ExcludedLocationKeywords)
        {
            if (keywordByEditorId.TryGetValue(name, out var fk)) excludedKeywords.Add(fk);
            else Console.WriteLine($"  note: keyword {name} not found, ignored");
        }
        keywordByEditorId.TryGetValue("LocTypeHold", out var holdKeyword);

        var cache = env.LinkCache;
        foreach (var locKey in CellLocations.Values.OfType<FormKey>().Distinct())
        {
            var current = locKey;
            for (int depth = 0; depth < 6; depth++)
            {
                if (!cache.TryResolve<ILocationGetter>(current, out var loc))
                    break;
                var kws = loc.Keywords?.Select(k => k.FormKey).ToHashSet() ?? new HashSet<FormKey>();
                if (kws.Contains(holdKeyword))
                    break;
                if (kws.Overlaps(excludedKeywords))
                {
                    ExcludedLocations.Add(locKey);
                    break;
                }
                if (loc.ParentLocation.FormKeyNullable is not { } parent)
                    break;
                current = parent;
            }
        }
    }
}
