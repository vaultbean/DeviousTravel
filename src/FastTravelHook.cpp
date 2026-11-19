#include "FastTravelHook.h"
#include "Interceptor.h"
#include "RoamingMarker.h"
#include "Settings.h"

namespace
{
	using Arg = std::uintptr_t;
	using FastTravel_t = void (*)(Arg, Arg, Arg, Arg);

	FastTravel_t g_original = nullptr;
	std::uintptr_t g_target = 0;

	RE::NiPointer<RE::TESObjectREFR> SeatOf(RE::PlayerCharacter* a_player)
	{
		if (!a_player || a_player->AsActorState()->GetSitSleepState() != RE::SIT_SLEEP_STATE::kIsSitting) {
			return nullptr;
		}
		return a_player->GetOccupiedFurniture().get();
	}

	bool IsCarriageSeat(RE::TESObjectREFR* a_seat)
	{
		const auto* base = a_seat ? a_seat->GetBaseObject() : nullptr;
		const auto* keywordForm = base ? base->As<RE::BGSKeywordForm>() : nullptr;
		if (!keywordForm) {
			return false;
		}
		for (const auto editorID : Settings::kCarriageFurnitureKeywords) {
			const auto* keyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(editorID);
			if (keyword && keywordForm->HasKeyword(keyword)) {
				return true;
			}
		}
		return false;
	}

	void Detour(Arg a1, Arg a2, Arg a3, Arg a4)
	{
		auto* destination = reinterpret_cast<RE::TESObjectREFR*>(a4);
		auto* player = RE::PlayerCharacter::GetSingleton();
		const auto seat = SeatOf(player);
		if (!destination || !IsCarriageSeat(seat.get())) {
			g_original(a1, a2, a3, a4);
			return;
		}

		auto decision = Interceptor::Decide(Interceptor::Source::kCarriage, destination);
		auto* marker = RoamingMarker::Get();
		if (!decision || !marker) {
			g_original(a1, a2, a3, a4);
			return;
		}
		auto* driver = Interceptor::FindCarriageDriver();
		const auto pos = decision->pick.point.pos;
		const float heading = decision->pick.headingRadians;

		RoamingMarker::SetPositionDirect(pos, heading);
		logger::info("[intercept] carriage: redirected {:08X} -> roaming marker {:08X}, driver {:08X}",
			destination->GetFormID(), marker->GetFormID(), driver ? driver->GetFormID() : 0);
		Interceptor::Commit(Interceptor::Source::kCarriage, std::move(*decision), destination, driver);

		g_original(a1, a2, a3, reinterpret_cast<Arg>(marker));

		SKSE::GetTaskInterface()->AddTask([pos, heading]() { RoamingMarker::MoveTo(pos, heading); });
	}

	void Papyrus_FastTravelNoIntercept(RE::BSScript::IVirtualMachine* a_vm, RE::VMStackID a_stackID,
		RE::StaticFunctionTag* a_tag, RE::TESObjectREFR* a_destination)
	{
		if (!a_destination) {
			logger::warn("FastTravelNoIntercept called with None");
			return;
		}
		const auto fn = g_original ? g_original : reinterpret_cast<FastTravel_t>(g_target);
		fn(reinterpret_cast<Arg>(a_vm), static_cast<Arg>(a_stackID), reinterpret_cast<Arg>(a_tag),
			reinterpret_cast<Arg>(a_destination));
	}

	bool RegisterFuncs(RE::BSScript::IVirtualMachine* a_vm)
	{
		a_vm->RegisterFunction("FastTravelNoIntercept"sv, "DTV_Native"sv, Papyrus_FastTravelNoIntercept);
		return true;
	}
}

namespace FastTravelHook
{
	void RegisterPapyrus()
	{
		if (auto* papyrus = SKSE::GetPapyrusInterface()) {
			papyrus->Register(RegisterFuncs);
		}
	}

	bool Install()
	{
		g_target = REL::Relocation<std::uintptr_t>{ RELOCATION_ID(54824, 55457) }.address();

		if (const auto status = MH_Initialize(); status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
			logger::error("MH_Initialize failed: {}", MH_StatusToString(status));
			return false;
		}

		auto* target = reinterpret_cast<LPVOID>(g_target);
		if (const auto status = MH_CreateHook(target, reinterpret_cast<LPVOID>(&Detour), reinterpret_cast<LPVOID*>(&g_original));
			status != MH_OK) {
			logger::error("MH_CreateHook failed: {}", MH_StatusToString(status));
			return false;
		}
		if (const auto status = MH_EnableHook(target); status != MH_OK) {
			logger::error("MH_EnableHook failed: {}", MH_StatusToString(status));
			return false;
		}

		logger::info("Game.FastTravel hooked");
		return true;
	}
}
