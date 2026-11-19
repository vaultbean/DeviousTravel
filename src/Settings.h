#pragma once

namespace Settings
{
	struct Values
	{
		float minChance = 5.0f;
		float maxChance = 75.0f;
		float minCarriageChance = 5.0f;
		float maxCarriageChance = 75.0f;
	};

	Values Read();

	struct CampType
	{
		std::string_view keyword;
		std::string_view label;
	};

	inline constexpr std::array kCampTypes{
		CampType{ "LocTypeBanditCamp"sv, "bandit"sv },
		CampType{ "LocTypeForswornCamp"sv, "forsworn"sv },
	};

	inline constexpr std::array kCarriageFurnitureKeywords{
		"isCartTravelPlayer"sv,
	};
}
