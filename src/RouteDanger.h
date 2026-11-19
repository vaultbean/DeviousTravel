#pragma once

namespace RouteDanger
{
	float Compute(const RE::BGSKeyword* a_locType, RE::TESObjectREFR* a_from, RE::TESObjectREFR* a_to);

	struct WorldPos
	{
		RE::TESWorldSpace* world = nullptr;
		RE::NiPoint3 pos;
	};

	std::optional<WorldPos> ExteriorPosition(RE::TESObjectREFR* a_ref);

	void RegisterPapyrus();
}
