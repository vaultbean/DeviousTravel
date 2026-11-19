#include "MapTravelHook.h"
#include "Interceptor.h"
#include "RoamingMarker.h"

namespace
{
	using Run_t = void(RE::FastTravelConfirmCallback*, std::uint8_t);
	REL::Relocation<Run_t> g_originalRun;

	constexpr std::uint8_t kButtonYes = 1;

	RE::MapMenu::RUNTIME_DATA* MapData(RE::FastTravelConfirmCallback* a_self)
	{
		auto* menu = a_self ? a_self->mapMenu : nullptr;
		return menu ? menu->GetRuntimeData() : nullptr;
	}

	void MaybeIntercept(RE::FastTravelConfirmCallback* a_self)
	{
		auto* data = MapData(a_self);
		auto destination = data ? data->mapMarker.get() : nullptr;
		if (!destination) {
			return;
		}
		auto decision = Interceptor::Decide(Interceptor::Source::kMap, destination.get());
		if (!decision) {
			return;
		}
		auto* marker = RoamingMarker::Get();
		if (!marker) {
			logger::warn("[intercept] map: roaming marker missing; travelling normally");
			return;
		}
		RoamingMarker::MoveTo(decision->pick.point.pos, decision->pick.headingRadians, destination.get());
		data->mapMarker = marker->GetHandle();
		logger::info("[intercept] map: redirected {:08X} -> roaming marker {:08X}", destination->GetFormID(), marker->GetFormID());
		Interceptor::Commit(Interceptor::Source::kMap, std::move(*decision), destination.get(), nullptr);
	}

	void Run(RE::FastTravelConfirmCallback* a_self, std::uint8_t a_button)
	{
		if (a_button == kButtonYes) {
			MaybeIntercept(a_self);
		}
		g_originalRun(a_self, a_button);
	}
}

namespace MapTravelHook
{
	bool Install()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::FastTravelConfirmCallback::VTABLE[0] };
		g_originalRun = vtbl.write_vfunc(1, &Run);
		logger::info("FastTravelConfirmCallback::Run hooked");
		return true;
	}
}
