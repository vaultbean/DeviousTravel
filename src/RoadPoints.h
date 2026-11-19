#pragma once

namespace RoadPoints
{
	struct Point
	{
		std::uint32_t id = 0;
		RE::NiPoint3 pos;
		float headingRadians = 0.0f;
		std::uint32_t road = 0;
		float slopeDeg = 0.0f;
	};

	struct Pick
	{
		Point point;
		float headingRadians = 0.0f;
	};

	void Load();

	std::size_t Count(const RE::TESWorldSpace* a_world);

	std::optional<Pick> PickAlongRoute(const RE::TESWorldSpace* a_world, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to);
}
