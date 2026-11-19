using System.Globalization;

namespace DeviousTravelRoads;

public static class Output
{
    // Read by the DLL from SKSE\Plugins\DeviousTravel\RoadPoints\<worldspace editor ID>.csv.
    // '#' lines are comments.
    //   id          stable within one file only; regenerating renumbers
    //   x,y,z       world position, z already raised above the navmesh
    //   heading_deg direction of the road, clockwise from north (either way along the road)
    //   road        which connected road the point is on (1..n)
    //   slope_deg   area-weighted mean slope of the walkable ground around the point (0 = flat)
    public static void WritePoints(string path, List<Marker> markers, Settings s, int pluginCount, string worldspace)
    {
        var roadIds = markers.Select(m => m.Road).Distinct().Order()
            .Select((road, i) => (road, i)).ToDictionary(p => p.road, p => p.i + 1);

        using var csv = new StreamWriter(path);
        csv.WriteLine("# DeviousTravel road points, format 2");
        csv.WriteLine(string.Create(CultureInfo.InvariantCulture,
            $"# generated {DateTime.UtcNow:yyyy-MM-dd HH:mm}Z from {pluginCount} plugins, spacing {s.SpacingCells} cells, worldspace {worldspace}"));
        csv.WriteLine("id,x,y,z,heading_deg,road,slope_deg");
        foreach (var m in markers)
            csv.WriteLine(string.Create(CultureInfo.InvariantCulture,
                $"{m.Id},{m.X:F0},{m.Y:F0},{m.Z:F0},{m.HeadingRadians * 180 / MathF.PI:F0},{roadIds[m.Road]},{m.MeanSlopeDeg:F1}"));
    }
}
