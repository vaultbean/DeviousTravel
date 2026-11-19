namespace DeviousTravelRoads;

public sealed record Marker(int Id, float X, float Y, float Z, float HeadingRadians, int Road, float MeanSlopeDeg);

// Uniform grid over 2D points for radius queries.
public sealed class SpatialGrid<T>
{
    readonly float _cellSize;
    readonly Dictionary<(int, int), List<int>> _cells = new();
    readonly List<(float X, float Y, T Item)> _items = new();

    public SpatialGrid(float cellSize) => _cellSize = cellSize;

    public int Count => _items.Count;

    public void Add(float x, float y, T item)
    {
        var key = ((int)MathF.Floor(x / _cellSize), (int)MathF.Floor(y / _cellSize));
        if (!_cells.TryGetValue(key, out var list)) _cells[key] = list = new List<int>();
        list.Add(_items.Count);
        _items.Add((x, y, item));
    }

    public IEnumerable<(int Index, T Item)> Query(float x, float y, float radius)
    {
        int x0 = (int)MathF.Floor((x - radius) / _cellSize), x1 = (int)MathF.Floor((x + radius) / _cellSize);
        int y0 = (int)MathF.Floor((y - radius) / _cellSize), y1 = (int)MathF.Floor((y + radius) / _cellSize);
        float r2 = radius * radius;
        for (int cx = x0; cx <= x1; cx++)
        for (int cy = y0; cy <= y1; cy++)
        {
            if (!_cells.TryGetValue((cx, cy), out var list)) continue;
            foreach (var i in list)
            {
                var (ix, iy, item) = _items[i];
                float dx = ix - x, dy = iy - y;
                if (dx * dx + dy * dy <= r2) yield return (i, item);
            }
        }
    }

    public bool Any(float x, float y, float radius) => Query(x, y, radius).Any();
}

public static class MarkerPlanner
{
    public static List<Marker> Plan(WorldData world, Settings s)
    {
        var preferred = world.Preferred;

        // 1. Group preferred triangles into roads (union-find over nearby centroids).
        var prefGrid = new SpatialGrid<int>(s.LinkDistance);
        for (int i = 0; i < preferred.Count; i++) prefGrid.Add(preferred[i].X, preferred[i].Y, i);
        var parent = Enumerable.Range(0, preferred.Count).ToArray();
        int Find(int i) { while (parent[i] != i) i = parent[i] = parent[parent[i]]; return i; }
        for (int i = 0; i < preferred.Count; i++)
            foreach (var (j, _) in prefGrid.Query(preferred[i].X, preferred[i].Y, s.LinkDistance))
            {
                int a = Find(i), b = Find(j);
                if (a != b) parent[a] = b;
            }

        // 2. Keep roads whose extent is large enough; drops courtyard and town-path fragments.
        var extent = new Dictionary<int, (float MinX, float MinY, float MaxX, float MaxY)>();
        for (int i = 0; i < preferred.Count; i++)
        {
            int r = Find(i);
            var t = preferred[i];
            extent[r] = extent.TryGetValue(r, out var e)
                ? (MathF.Min(e.MinX, t.X), MathF.Min(e.MinY, t.Y), MathF.Max(e.MaxX, t.X), MathF.Max(e.MaxY, t.Y))
                : (t.X, t.Y, t.X, t.Y);
        }
        float minExtent = s.MinRoadExtentCells * 4096f;
        var keptRoads = extent.Where(kv => MathF.Max(kv.Value.MaxX - kv.Value.MinX, kv.Value.MaxY - kv.Value.MinY) >= minExtent)
            .Select(kv => kv.Key).ToHashSet();

        // 3. Spatial indexes for the filters.
        var allGrid = new SpatialGrid<Tri>(s.RoomRadius);
        foreach (var t in world.Triangles) allGrid.Add(t.X, t.Y, t);
        var doorGrid = new SpatialGrid<int>(s.DoorClearance);
        foreach (var d in world.Doors) doorGrid.Add(d.X, d.Y, 0);
        var mapGrid = new SpatialGrid<int>(s.MapMarkerClearance);
        foreach (var m in world.MapMarkers) mapGrid.Add(m.X, m.Y, 0);
        var personGrid = new SpatialGrid<int>(s.PersonClearance);
        foreach (var p in world.People) personGrid.Add(p.X, p.Y, 0);
        var creatureGrid = new SpatialGrid<int>(MathF.Max(s.CreatureClearance, 1f));
        foreach (var c in world.Creatures) creatureGrid.Add(c.X, c.Y, 0);
        // Locations have their own radius; index them by centre and check each one's reach.
        float maxLocationReach = world.Locations.Count == 0 ? 0f : world.Locations.Max(l => l.Radius) + s.LocationMargin;
        var locationGrid = new SpatialGrid<LocationArea>(8192f);
        foreach (var l in world.Locations) locationGrid.Add(l.X, l.Y, l);
        bool InsideLocation(Tri t)
        {
            foreach (var (_, l) in locationGrid.Query(t.X, t.Y, maxLocationReach))
            {
                float dx = l.X - t.X, dy = l.Y - t.Y, reach = l.Radius + s.LocationMargin;
                if (dx * dx + dy * dy <= reach * reach) return true;
            }
            return false;
        }

        // 4. Evaluate every preferred triangle on a kept road.
        float roomArea = MathF.PI * s.RoomRadius * s.RoomRadius;
        var candidates = new List<(int Index, float MeanSlope)>();

        for (int i = 0; i < preferred.Count; i++)
        {
            if (!keptRoads.Contains(Find(i))) continue;
            var t = preferred[i];
            if (world.IsExcludedCell(t.X, t.Y)) continue;
            if (doorGrid.Any(t.X, t.Y, s.DoorClearance)) continue;
            if (mapGrid.Any(t.X, t.Y, s.MapMarkerClearance)) continue;
            if (InsideLocation(t)) continue;
            if (personGrid.Any(t.X, t.Y, s.PersonClearance)) continue;
            if (s.CreatureClearance > 0f && creatureGrid.Any(t.X, t.Y, s.CreatureClearance)) continue;

            var ground = MeasureGround(allGrid, t, s);
            if (ground.NavArea < s.MinNavCoverage * roomArea) continue;
            if (ground.HeightRange > s.MaxHeightRange) continue;
            if (ground.MeanSlope > s.MaxMeanSlopeDeg) continue;
            if (ground.SteepFraction > s.MaxSteepFraction) continue;
            candidates.Add((i, ground.MeanSlope));
        }

        // 5. Place markers flattest first (whole degrees, then larger triangles), so the flattest
        //    spot on each stretch of road wins; position breaks ties for deterministic output.
        var order = candidates
            .OrderBy(c => MathF.Floor(c.MeanSlope))
            .ThenByDescending(c => preferred[c.Index].Area)
            .ThenBy(c => preferred[c.Index].X).ThenBy(c => preferred[c.Index].Y)
            .ToList();

        float spacing = s.SpacingCells * 4096f;
        var placed = new SpatialGrid<int>(spacing);
        var markers = new List<Marker>();
        foreach (var (i, meanSlope) in order)
        {
            var t = preferred[i];
            if (placed.Any(t.X, t.Y, spacing)) continue;
            float heading = RoadHeading(prefGrid, preferred, t, Find(i), Find);
            placed.Add(t.X, t.Y, markers.Count);
            markers.Add(new Marker(markers.Count + 1, t.X, t.Y, t.Z + s.HeightOffset, heading, Find(i), meanSlope));
        }

        return markers;
    }

    readonly record struct Ground(float NavArea, float HeightRange, float MeanSlope, float SteepFraction);

    // The navmesh within RoomRadius of t: how much walkable area, how uneven, and how sloped
    // (area-weighted mean slope, and the share of area steeper than SteepSlopeDeg).
    static Ground MeasureGround(SpatialGrid<Tri> allGrid, Tri t, Settings s)
    {
        float navArea = 0f, minZ = float.MaxValue, maxZ = float.MinValue, slopeArea = 0f, steepArea = 0f;
        foreach (var (_, n) in allGrid.Query(t.X, t.Y, s.RoomRadius))
        {
            navArea += n.Area;
            minZ = MathF.Min(minZ, n.Z);
            maxZ = MathF.Max(maxZ, n.Z);
            slopeArea += n.SlopeDeg * n.Area;
            if (n.SlopeDeg > s.SteepSlopeDeg) steepArea += n.Area;
        }
        if (navArea <= 0f) return new Ground(0f, 0f, 90f, 1f);
        return new Ground(navArea, maxZ - minZ, slopeArea / navArea, steepArea / navArea);
    }

    // Direction of the road at t: principal axis of nearby preferred triangles on the same road.
    // Skyrim headings are radians clockwise from north (+Y).
    static float RoadHeading(SpatialGrid<int> prefGrid, List<Tri> preferred, Tri t, int road, Func<int, int> find)
    {
        double sxx = 0, syy = 0, sxy = 0;
        foreach (var (_, j) in prefGrid.Query(t.X, t.Y, 1024f))
        {
            if (find(j) != road) continue;
            double dx = preferred[j].X - t.X, dy = preferred[j].Y - t.Y;
            sxx += dx * dx; syy += dy * dy; sxy += dx * dy;
        }
        double theta = 0.5 * Math.Atan2(2 * sxy, sxx - syy);   // axis angle from +X
        double dirX = Math.Cos(theta), dirY = Math.Sin(theta);
        double heading = Math.Atan2(dirX, dirY);
        if (heading < 0) heading += 2 * Math.PI;
        return (float)heading;
    }
}
