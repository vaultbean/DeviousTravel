#include "Interceptor.h"
#include "RoamingMarker.h"
#include "RouteDanger.h"
#include "Settings.h"

namespace
{
	constexpr RE::FormID kWIEventsEnabled = 0x035D66;
	constexpr RE::FormID kCarriageSystemFaction = 0x017F04;
	constexpr RE::FormID kJobCarriageFaction = 0x0DDDA8;
	constexpr RE::FormID kMainQuestLocalID = 0x001;
	constexpr std::string_view kPluginName = "Devious Travel.esp"sv;
	constexpr float kDriverSearchRadius = 2048.0f;
	constexpr float kDangerScale = 0.5f;

	RE::TESQuest* MainQuest()
	{
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		return dataHandler ? dataHandler->LookupForm<RE::TESQuest>(kMainQuestLocalID, kPluginName) : nullptr;
	}

	RE::TESGlobal* WIEventsEnabled()
	{
		return RE::TESForm::LookupByID<RE::TESGlobal>(kWIEventsEnabled);
	}

	float Random01()
	{
		static std::mutex lock;
		static std::mt19937 rng{ std::random_device{}() };
		std::scoped_lock guard(lock);
		return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
	}
}

namespace Interceptor
{
	std::optional<Decision> Decide(Source a_source, RE::TESObjectREFR* a_destination)
	{
		const auto settings = Settings::Read();
		const auto tag = a_source == Source::kMap ? "map"sv : "carriage"sv;
		auto skip = [&](std::string_view a_why) -> std::optional<Decision> {
			logger::info("[intercept] {}: no intercept ({})", tag, a_why);
			return std::nullopt;
		};

		const bool carriage = a_source == Source::kCarriage;
		const float minChance = carriage ? settings.minCarriageChance : settings.minChance;
		const float maxChance = carriage ? settings.maxCarriageChance : settings.maxChance;
		
		if (maxChance <= 0.0f) {
			return skip("max chance is 0");
		}
		
		auto* player = RE::PlayerCharacter::GetSingleton();
		const auto from = RouteDanger::ExteriorPosition(player);
		const auto to = RouteDanger::ExteriorPosition(a_destination);

		if (!from || !to || !from->world || from->world != to->world) {
			return skip("route is not within one worldspace");
		}

		if (RoadPoints::Count(from->world) == 0) {
			return skip("no road points for this worldspace");
		}

		// this should've happened already since we move into tamriel on game load but guard just in case
		if (a_source == Source::kCarriage && !RoamingMarker::IsInWorld(from->world)) {
			return skip("roaming marker is not in this worldspace");
		}

		std::vector<std::pair<std::string_view, float>> camps;
		for (const auto& [editorID, label] : Settings::kCampTypes) {
			auto* keyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(editorID);
			if (!keyword) {
				continue;
			}
			const float danger = RouteDanger::Compute(keyword, player, a_destination);
			auto it = std::ranges::find(camps, label, &std::pair<std::string_view, float>::first);
			if (it == camps.end()) {
				camps.emplace_back(label, danger);
			} else {
				it->second = (std::max)(it->second, danger);
			}
		}
		std::ranges::stable_sort(camps, std::greater{}, &std::pair<std::string_view, float>::second);

		Decision decision;
		for (const auto& [label, danger] : camps) {
			decision.labels.emplace_back(label);
			decision.labelDanger.push_back(danger);
		}
		decision.danger = camps.empty() ? 0.0f : camps.front().second;

		const float range = (std::max)(maxChance - minChance, 0.0f);
		const float chance = minChance + range * (1.0f - std::exp(-kDangerScale * decision.danger));
		const float roll = Random01() * 100.0f;
		logger::info("[intercept] {}: danger {:.2f}, chance {:.0f}%, roll {:.0f}", tag, decision.danger, chance, roll);
		if (roll >= chance) {
			return skip("roll failed");
		}

		auto pick = RoadPoints::PickAlongRoute(from->world, from->pos, to->pos);
		if (!pick) {
			return skip("no road point along this route");
		}
		decision.pick = *pick;
		logger::info("[intercept] {}: road point {} at ({:.0f},{:.0f},{:.0f})", tag, pick->point.id,
			pick->point.pos.x, pick->point.pos.y, pick->point.pos.z);
		return decision;
	}

	void Commit(Source a_source, Decision&& a_decision, RE::TESObjectREFR* a_originalDestination, RE::Actor* a_driver)
	{
		if (auto* global = WIEventsEnabled()) {
			global->value = 0.0f;
		}

		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		auto* quest = MainQuest();
		if (!vm || !quest) {
			return;
		}
		const auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(quest->GetFormType(), quest);
		auto* args = RE::MakeFunctionArguments(
			static_cast<RE::TESObjectREFR*>(RoamingMarker::Get()),
			static_cast<RE::TESObjectREFR*>(a_originalDestination),
			static_cast<std::int32_t>(a_source),
			static_cast<RE::Actor*>(a_driver),
			std::move(a_decision.labels),
			std::move(a_decision.labelDanger));
		vm->SendEvent(handle, "OnDTVIntercept"sv, args);
	}

	RE::Actor* FindCarriageDriver()
	{
		const std::array factions{
			RE::TESForm::LookupByID<RE::TESFaction>(kCarriageSystemFaction),
			RE::TESForm::LookupByID<RE::TESFaction>(kJobCarriageFaction),
		};
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* processLists = RE::ProcessLists::GetSingleton();
		if (!player || !processLists) {
			return nullptr;
		}
		auto isDriver = [&](RE::Actor* a_actor) {
			return std::ranges::any_of(factions, [&](auto* f) { return f && a_actor->IsInFaction(f); });
		};

		const auto origin = player->GetPosition();
		RE::Actor* best = nullptr;
		float bestDistance = kDriverSearchRadius;
		for (auto& handle : processLists->highActorHandles) {
			auto actor = handle.get();
			if (!actor || actor->IsDead() || !isDriver(actor.get())) {
				continue;
			}
			const float distance = origin.GetDistance(actor->GetPosition());
			if (distance < bestDistance) {
				bestDistance = distance;
				best = actor.get();
			}
		}
		return best;
	}
}
