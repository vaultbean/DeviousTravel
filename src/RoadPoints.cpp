#include "RoadPoints.h"

namespace
{
	std::unordered_map<RE::FormID, std::vector<RoadPoints::Point>> g_points;

	struct Band
	{
		float minAlong;
		float maxAlong;
		float maxOffRoute;
	};

	constexpr Band kStrict{ 0.30f, 0.70f, 1.5f * 4096.0f };
	constexpr Band kWide{ 0.15f, 0.85f, 3.0f * 4096.0f };

	constexpr float kMaxSlopeDeg = 15.0f;

	std::filesystem::path Directory()
	{
		return std::filesystem::current_path() / "Data/SKSE/Plugins/DeviousTravel/RoadPoints";
	}

	std::vector<RoadPoints::Point> ReadFile(const std::filesystem::path& a_path)
	{
		std::vector<RoadPoints::Point> points;
		std::ifstream file(a_path);
		std::string line;
		std::size_t lineNo = 0, bad = 0;
		while (std::getline(file, line)) {
			++lineNo;
			if (line.empty() || line[0] == '#' || line.starts_with("id,")) {
				continue;
			}
			RoadPoints::Point p;
			float headingDeg = 0.0f;
			const int read = sscanf_s(line.c_str(), "%u,%f,%f,%f,%f,%u,%f", &p.id, &p.pos.x, &p.pos.y, &p.pos.z,
				&headingDeg, &p.road, &p.slopeDeg);
			if (read != 6 && read != 7) {
				if (++bad <= 5) {
					logger::warn("[points] {} line {} unreadable: {}", a_path.filename().string(), lineNo, line);
				}
				continue;
			}
			p.headingRadians = headingDeg * std::numbers::pi_v<float> / 180.0f;
			points.push_back(p);
		}
		return points;
	}

	std::mt19937& Rng()
	{
		static std::mt19937 rng{ std::random_device{}() };
		return rng;
	}

	float AngleDiff(float a, float b)
	{
		float d = std::fmod(std::fabs(a - b), 2.0f * std::numbers::pi_v<float>);
		return d > std::numbers::pi_v<float> ? 2.0f * std::numbers::pi_v<float> - d : d;
	}

	std::optional<RoadPoints::Pick> PickInBand(const std::vector<RoadPoints::Point>& a_points, const RE::NiPoint3& a_from,
		const RE::NiPoint3& a_to, const Band& a_band)
	{
		const float abx = a_to.x - a_from.x, aby = a_to.y - a_from.y;
		const float lenSq = abx * abx + aby * aby;
		if (lenSq < 1.0f) {
			return std::nullopt;
		}
		const float len = std::sqrt(lenSq);

		struct Candidate
		{
			const RoadPoints::Point* point;
			float along;
			float offRoute;
			float weight;
		};
		std::vector<Candidate> candidates;
		for (const auto& p : a_points) {
			const float apx = p.pos.x - a_from.x, apy = p.pos.y - a_from.y;
			const float along = (apx * abx + apy * aby) / lenSq;
			if (along < a_band.minAlong || along > a_band.maxAlong) {
				continue;
			}
			const float offRoute = std::fabs(apx * aby - apy * abx) / len;
			if (offRoute > a_band.maxOffRoute) {
				continue;
			}
			const float lineWeight = 1.0f - offRoute / a_band.maxOffRoute;
			const float middleWeight = 1.0f - std::fabs(along - 0.5f) / 0.5f;
			const float flatWeight = 1.0f - 0.5f * std::clamp(p.slopeDeg / kMaxSlopeDeg, 0.0f, 1.0f);
			candidates.push_back({ &p, along, offRoute, (0.1f + lineWeight * (0.5f + 0.5f * middleWeight)) * flatWeight });
		}
		if (candidates.empty()) {
			return std::nullopt;
		}

		std::vector<float> weights;
		weights.reserve(candidates.size());
		for (const auto& c : candidates) {
			weights.push_back(c.weight);
		}
		std::discrete_distribution<std::size_t> dist(weights.begin(), weights.end());
		const auto& chosen = candidates[dist(Rng())];

		const float travelHeading = std::atan2(abx, aby);
		float heading = chosen.point->headingRadians;
		if (AngleDiff(heading, travelHeading) > std::numbers::pi_v<float> / 2.0f) {
			heading = std::fmod(heading + std::numbers::pi_v<float>, 2.0f * std::numbers::pi_v<float>);
		}

		RoadPoints::Pick pick;
		pick.point = *chosen.point;
		pick.headingRadians = heading;
		return pick;
	}
}

namespace RoadPoints
{
	void Load()
	{
		g_points.clear();
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(Directory(), ec)) {
			if (!entry.is_regular_file() || entry.path().extension() != ".csv") {
				continue;
			}
			const auto editorID = entry.path().stem().string();
			const auto* world = RE::TESForm::LookupByEditorID<RE::TESWorldSpace>(editorID);
			if (!world) {
				logger::info("[points] {}: worldspace not in the load order, skipped", editorID);
				continue;
			}
			auto& points = g_points[world->GetFormID()];
			points = ReadFile(entry.path());
			logger::info("[points] {}: loaded {} road points", editorID, points.size());
		}
		if (ec || g_points.empty()) {
			logger::error("[points] no road points loaded from {}", Directory().string());
		}
	}

	std::size_t Count(const RE::TESWorldSpace* a_world)
	{
		const auto it = a_world ? g_points.find(a_world->GetFormID()) : g_points.end();
		return it != g_points.end() ? it->second.size() : 0;
	}

	std::optional<Pick> PickAlongRoute(const RE::TESWorldSpace* a_world, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to)
	{
		const auto it = a_world ? g_points.find(a_world->GetFormID()) : g_points.end();
		if (it == g_points.end()) {
			return std::nullopt;
		}
		static std::mutex lock;
		std::scoped_lock guard(lock);
		if (auto pick = PickInBand(it->second, a_from, a_to, kStrict)) {
			return pick;
		}
		return PickInBand(it->second, a_from, a_to, kWide);
	}
}
