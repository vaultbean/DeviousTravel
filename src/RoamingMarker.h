#pragma once

namespace RoamingMarker
{
	RE::TESObjectREFR* Get();

	bool IsInWorld(const RE::TESWorldSpace* a_world);

	void MoveTo(const RE::NiPoint3& a_position, float a_headingRadians, RE::TESObjectREFR* a_anchor = nullptr);

	void SetPositionDirect(const RE::NiPoint3& a_position, float a_headingRadians);

	void EnsureInTamriel();
}
