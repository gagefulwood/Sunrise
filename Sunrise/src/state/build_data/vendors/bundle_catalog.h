#pragma once

#include <array>
#include <span>

#include "../../account/account_state.h"
#include "../items/item_bundle.h"

namespace sunrise::state::build_data::vendors::bundles {

/** An armour upgrade fills the five armour equipment slots. */
inline constexpr std::size_t kPieceCount = 5;

/** A reconstructed claim effect is explicit; a negated purchase flag alone is not a write rule. */
struct ClaimEffect {
    std::uint32_t itemHash;
    std::uint32_t flagHash;
};

/** Only the three Solstice class upgrades have a supported claim-on-acquisition effect. */
inline constexpr auto kClaimEffects = std::to_array<ClaimEffect>({
    {1493877378U, 1702436248U}, // Hunter upgrade -> account acquired flag.
    {4036562374U, 3632322068U}, // Titan upgrade -> account acquired flag.
    {2370303981U, 2354892225U}, // Warlock upgrade -> account acquired flag.
});

/** Installed sale, purchase gates and direct sack members, resolved before request handling. */
struct Definition {
    std::uint32_t sourceHash{};
    std::uint16_t vendorIndex{};
    std::uint16_t saleIndex{};
    CharacterClass characterClass{};
    std::uint16_t claimRow{};
    std::array<std::uint16_t, kPieceCount> requiredRows{};
    items::ItemBundle rewards{};
};

/** Discard extracted bindings when the base vendor catalog changes. */
void clear() noexcept;
/** Mark unavailable content as settled so unrelated startup domains can complete. */
void unavailable() noexcept;
[[nodiscard]] bool settled() noexcept;
[[nodiscard]] bool ready() noexcept;
[[nodiscard]] bool replace(std::span<const Definition> definitions) noexcept;
[[nodiscard]] bool find(std::uint32_t sourceHash, Definition& definition) noexcept;

} // namespace sunrise::state::build_data::vendors::bundles
