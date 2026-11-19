#include "RouteDanger.h"

namespace
{
	constexpr float kNear = 2.0f * 4096.0f;
	constexpr float kFar = 6.0f * 4096.0f;

	using RouteDanger::WorldPos;

	RE::TESWorldSpace* RootWorld(RE::TESWorldSpace* a_world)
	{
		while (a_world && a_world->parentWorld) {
			a_world = a_world->parentWorld;
		}
		return a_world;
	}

	std::optional<WorldPos> ExteriorPosOfRef(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref) {
			return std::nullopt;
		}
		const auto* cell = a_ref->GetParentCell();
		auto* world = a_ref->GetWorldspace();
		if (cell && cell->IsInteriorCell()) {
			return std::nullopt;
		}
		if (!world) {
			return std::nullopt;
		}
		return WorldPos{ RootWorld(world), a_ref->GetPosition() };
	}

	std::optional<WorldPos> MarkerPos(const RE::BGSLocation* a_loc)
	{
		const auto marker = a_loc->worldLocMarker.get();
		return ExteriorPosOfRef(marker.get());
	}

	std::optional<WorldPos> ExteriorPos(RE::TESObjectREFR* a_ref)
	{
		if (auto pos = ExteriorPosOfRef(a_ref)) {
			return pos;
		}
		if (!a_ref) {
			return std::nullopt;
		}
		for (auto* loc = a_ref->GetCurrentLocation(); loc; loc = loc->parentLoc) {
			if (auto pos = MarkerPos(loc)) {
				return pos;
			}
		}
		return std::nullopt;
	}

	float DistanceToSegment(const RE::NiPoint3& p, const RE::NiPoint3& a, const RE::NiPoint3& b)
	{
		const float abx = b.x - a.x, aby = b.y - a.y;
		const float apx = p.x - a.x, apy = p.y - a.y;
		const float lenSq = abx * abx + aby * aby;
		float t = lenSq > 0.0f ? (apx * abx + apy * aby) / lenSq : 0.0f;
		t = std::clamp(t, 0.0f, 1.0f);
		const float dx = apx - t * abx, dy = apy - t * aby;
		return std::sqrt(dx * dx + dy * dy);
	}

	float Weight(float a_distance)
	{
		if (a_distance <= kNear) {
			return 1.0f;
		}
		if (a_distance >= kFar) {
			return 0.0f;
		}
		return 1.0f - (a_distance - kNear) / (kFar - kNear);
	}

	float Papyrus_GetRouteDanger(RE::StaticFunctionTag*, RE::BGSKeyword* a_locType,
		RE::TESObjectREFR* a_from, RE::TESObjectREFR* a_to)
	{
		return RouteDanger::Compute(a_locType, a_from, a_to);
	}

	bool RegisterFuncs(RE::BSScript::IVirtualMachine* a_vm)
	{
		a_vm->RegisterFunction("GetRouteDanger"sv, "DTV_Native"sv, Papyrus_GetRouteDanger);
		return true;
	}
}

namespace RouteDanger
{
	std::optional<WorldPos> ExteriorPosition(RE::TESObjectREFR* a_ref)
	{
		return ExteriorPos(a_ref);
	}

	float Compute(const RE::BGSKeyword* a_locType, RE::TESObjectREFR* a_from, RE::TESObjectREFR* a_to)
	{
		const auto from = ExteriorPos(a_from);
		const auto to = ExteriorPos(a_to);
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!a_locType || !from || !to || from->world != to->world || !dataHandler) {
			return 0.0f;
		}

		float danger = 0.0f;
		for (auto* loc : dataHandler->GetFormArray<RE::BGSLocation>()) {
			if (!loc || loc->IsCleared() || !loc->HasKeyword(a_locType)) {
				continue;
			}
			const auto camp = MarkerPos(loc);
			if (camp && camp->world == from->world) {
				danger += Weight(DistanceToSegment(camp->pos, from->pos, to->pos));
			}
		}
		return danger;
	}

	void RegisterPapyrus()
	{
		if (auto* papyrus = SKSE::GetPapyrusInterface()) {
			papyrus->Register(RegisterFuncs);
		}
	}
}
