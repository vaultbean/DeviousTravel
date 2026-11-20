#include "RoamingMarker.h"

namespace
{
	constexpr RE::FormID kMarkerLocalID = 0x05A;
	constexpr std::string_view kPluginName = "Devious Travel.esp"sv;
	constexpr RE::FormID kTamriel = 0x3C;
	constexpr RE::FormID kTamrielAnchor = 0x01624A;

	RE::TESWorldSpace* RootWorld(RE::TESObjectREFR* a_ref)
	{
		auto* world = a_ref ? a_ref->GetWorldspace() : nullptr;
		while (world && world->parentWorld) {
			world = world->parentWorld;
		}
		return world;
	}

	bool InTamriel(RE::TESObjectREFR* a_ref)
	{
		const auto* world = RootWorld(a_ref);
		return world && world->GetFormID() == kTamriel;
	}

	void MoveAnchored(RE::TESObjectREFR* a_marker, RE::TESObjectREFR* a_anchor, const RE::NiPoint3& a_position, float a_heading)
	{
		using MoveTo_t = void(RE::TESObjectREFR*, const RE::ObjectRefHandle&, RE::TESObjectCELL*, RE::TESWorldSpace*,
			const RE::NiPoint3&, const RE::NiPoint3&);
		static REL::Relocation<MoveTo_t> moveTo{ RELOCATION_ID(56227, 56626) };
		moveTo(a_marker, a_anchor->GetHandle(), a_anchor->GetParentCell(), a_anchor->GetWorldspace(), a_position,
			RE::NiPoint3{ 0.0f, 0.0f, a_heading });
	}
}

namespace RoamingMarker
{
	RE::TESObjectREFR* Get()
	{
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		return dataHandler ? dataHandler->LookupForm<RE::TESObjectREFR>(kMarkerLocalID, kPluginName) : nullptr;
	}

	bool IsInWorld(const RE::TESWorldSpace* a_world)
	{
		return a_world && RootWorld(Get()) == a_world;
	}

	void MoveTo(const RE::NiPoint3& a_position, float a_headingRadians, RE::TESObjectREFR* a_anchor)
	{
		if (auto* marker = Get()) {
			MoveAnchored(marker, a_anchor ? a_anchor : marker, a_position, a_headingRadians);
		}
	}

	void SetPositionDirect(const RE::NiPoint3& a_position, float a_headingRadians)
	{
		if (auto* marker = Get()) {
			marker->data.location = a_position;
			marker->data.angle = RE::NiPoint3{ 0.0f, 0.0f, a_headingRadians };
		}
	}

	void EnsureInTamriel()
	{
		auto* marker = Get();
		if (!marker) {
			logger::error("[marker] {:06X} not found in {}", kMarkerLocalID, kPluginName);
			return;
		}
		if (InTamriel(marker)) {
			return;
		}
		auto* anchor = RE::TESForm::LookupByID<RE::TESObjectREFR>(kTamrielAnchor);
		if (!anchor || !InTamriel(anchor)) {
			logger::error("[marker] anchor {:08X} missing or not in Tamriel", kTamrielAnchor);
			return;
		}
		MoveAnchored(marker, anchor, anchor->GetPosition(), 0.0f);
	}
}
