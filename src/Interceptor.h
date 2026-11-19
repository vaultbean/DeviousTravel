#pragma once

#include "RoadPoints.h"

namespace Interceptor
{
	enum class Source : std::int32_t
	{
		kMap = 0,
		kCarriage = 1,
	};

	struct Decision
	{
		RoadPoints::Pick pick;
		float danger = 0.0f;
		std::vector<RE::BSFixedString> labels;
		std::vector<float> labelDanger;
	};

	std::optional<Decision> Decide(Source a_source, RE::TESObjectREFR* a_destination);

	void Commit(Source a_source, Decision&& a_decision, RE::TESObjectREFR* a_originalDestination, RE::Actor* a_driver);

	RE::Actor* FindCarriageDriver();
}
