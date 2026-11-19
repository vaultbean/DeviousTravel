#include "Settings.h"

namespace
{
	void ReadGlobal(std::string_view a_editorID, float& a_value)
	{
		if (const auto* global = RE::TESForm::LookupByEditorID<RE::TESGlobal>(a_editorID)) {
			a_value = global->value;
		} else {
			logger::warn("[settings] global {} not found; using {}", a_editorID, a_value);
		}
	}
}

namespace Settings
{
	Values Read()
	{
		Values v;
		ReadGlobal("DTV_MinChance"sv, v.minChance);
		ReadGlobal("DTV_MaxChance"sv, v.maxChance);
		ReadGlobal("DTV_MinCarriageChance"sv, v.minCarriageChance);
		ReadGlobal("DTV_MaxCarriageChance"sv, v.maxCarriageChance);
		return v;
	}
}
