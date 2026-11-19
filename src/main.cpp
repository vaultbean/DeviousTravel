#include "FastTravelHook.h"
#include "MapTravelHook.h"
#include "RoadPoints.h"
#include "RoamingMarker.h"
#include "RouteDanger.h"

namespace
{
	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			RoadPoints::Load();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			RoamingMarker::EnsureInTamriel();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	logger::init();

	logger::info("{} {} loaded, game {}", SKSE::GetPluginName(), SKSE::GetPluginVersion().string(),
		REL::Module::get().version().string());

	if (!FastTravelHook::Install()) {
		logger::error("Game.FastTravel hook not installed; carriage interception inactive");
	}
	MapTravelHook::Install();

	RouteDanger::RegisterPapyrus();
	FastTravelHook::RegisterPapyrus();

	if (auto* messaging = SKSE::GetMessagingInterface()) {
		messaging->RegisterListener(OnMessage);
	}

	return true;
}
