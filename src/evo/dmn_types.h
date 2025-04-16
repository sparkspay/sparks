// Copyright (c) 2023 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_EVO_DMN_TYPES_H
#define BITCOIN_EVO_DMN_TYPES_H

#include <amount.h>
#include <chain.h>
#include <chainparams.h>
#include <validation.h>
#include <deploymentstatus.h>

#include <limits>
#include <string_view>

enum class MnType : uint16_t {
    Regular = 0,
    Evo = 1,
    COUNT,
    Invalid = std::numeric_limits<uint16_t>::max(),
};

template<typename T> struct is_serializable_enum;
template<> struct is_serializable_enum<MnType> : std::true_type {};

namespace dmn_types {

struct mntype_struct
{
    const int32_t voting_weight;
    const CAmount collat_amount;
    const std::string_view description;
};

constexpr auto Regular = mntype_struct{
    .voting_weight = 1,
    .collat_amount = 5000 * COIN,
    .description = "Masternode",
};
//First approach of Evonodes on Sparks
//Started at when activating v19
constexpr auto Evo4 = mntype_struct{
    .voting_weight = 4,
    .collat_amount = 25000 * COIN,
    .description = "Evonode",
};
//Second approach of Evonodes on Sparks while disable masternodes
//Started at when activating v20
constexpr auto Evo1 = mntype_struct{
    .voting_weight = 1,
    .collat_amount = 25000 * COIN,
    .description = "Evonode",
};
//Third approach of Evonodes on Sparks when enabling masternodes again
//Will start in future
constexpr auto Evo5 = mntype_struct{
    .voting_weight = 5,
    .collat_amount = 25000 * COIN,
    .description = "Evonode",
};
constexpr auto Invalid = mntype_struct{
    .voting_weight = 0,
    .collat_amount = MAX_MONEY,
    .description = "Invalid",
};

[[nodiscard]] inline const dmn_types::mntype_struct GetEvoVersion(gsl::not_null<const CBlockIndex*> pindexPrev)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    const bool isV20Active{DeploymentActiveAt(*pindexPrev, consensusParams, Consensus::DEPLOYMENT_V20)};
    if (pindexPrev->nHeight >= consensusParams.V19Height && !isV20Active) {
        return dmn_types::Evo4;
    } else if (isV20Active){
        return dmn_types::Evo1;
    } else {
        //when enabling masternodes again, should add new condition here and should return Evo5
        return dmn_types::Invalid;
    }
}

[[nodiscard]] static constexpr bool IsCollateralAmount(CAmount amount)
{
    return amount == Regular.collat_amount ||
        amount == Evo4.collat_amount || amount == Evo1.collat_amount || amount == Evo5.collat_amount;
}

} // namespace dmn_types

[[nodiscard]] constexpr const dmn_types::mntype_struct GetMnType(MnType type, gsl::not_null<const CBlockIndex*> pindexPrev)
{
    switch (type) {
        case MnType::Regular: return dmn_types::Regular;
        case MnType::Evo: return dmn_types::GetEvoVersion(pindexPrev);
        default: return dmn_types::Invalid;
    }
}

[[nodiscard]] constexpr const bool IsValidMnType(MnType type)
{
    return type < MnType::COUNT;
}

#endif // BITCOIN_EVO_DMN_TYPES_H
