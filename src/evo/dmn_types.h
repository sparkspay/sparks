// Copyright (c) 2023-2024 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_EVO_DMN_TYPES_H
#define BITCOIN_EVO_DMN_TYPES_H

#include <consensus/amount.h>
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
constexpr auto RegularV1 = mntype_struct{
    .voting_weight = 1,
    .collat_amount = 25000 * COIN,   // In sparks, at the begining activating masternodes, its collateral is 25000
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
//Enabling masternodes again
//Started at when activating v22
constexpr auto RegularV2 = mntype_struct{
    .voting_weight = 1,
    .collat_amount = 25000 * COIN,
    .description = "Masternode",
};
constexpr auto Invalid = mntype_struct{
    .voting_weight = 0,
    .collat_amount = MAX_MONEY,
    .description = "Invalid",
};

[[nodiscard]] inline const dmn_types::mntype_struct GetEvoVersion(gsl::not_null<const CBlockIndex*> pindex)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    const bool isV20Active{DeploymentActiveAt(*pindex, consensusParams, Consensus::DEPLOYMENT_V20)};
    const bool isV19Active{DeploymentActiveAt(*pindex, consensusParams, Consensus::DEPLOYMENT_V19)};
    if (isV19Active && !isV20Active) {
        return dmn_types::Evo4;
    } else if (isV20Active){
        return dmn_types::Evo1;
    } else {
        return dmn_types::Invalid;
    }
}

[[nodiscard]] inline const dmn_types::mntype_struct GetRegularVersion(gsl::not_null<const CBlockIndex*> pindex)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    const bool isV22Active{DeploymentActiveAt(*pindex, consensusParams, Consensus::DEPLOYMENT_V22)};
    const bool isV20Active{DeploymentActiveAt(*pindex, consensusParams, Consensus::DEPLOYMENT_V20)};
    const bool isV19Active{DeploymentActiveAt(*pindex, consensusParams, Consensus::DEPLOYMENT_V19)};
    if (!isV19Active) {
        return dmn_types::RegularV1;
    } else if (isV22Active){
        return dmn_types::RegularV2;
    } else {
        return dmn_types::Regular;
    }
}

[[nodiscard]] static constexpr bool IsCollateralAmount(CAmount amount)
{
    return amount == Regular.collat_amount ||
        amount == Evo4.collat_amount || amount == Evo1.collat_amount || amount == RegularV2.collat_amount;
}

} // namespace dmn_types

[[nodiscard]] constexpr const dmn_types::mntype_struct GetMnType(MnType type, gsl::not_null<const CBlockIndex*> pindex)
{
    switch (type) {
        case MnType::Regular: return dmn_types::GetRegularVersion(pindex);
        case MnType::Evo: return dmn_types::GetEvoVersion(pindex);
        default: return dmn_types::Invalid;
    }
}

[[nodiscard]] constexpr bool IsValidMnType(MnType type) { return type < MnType::COUNT; }

#endif // BITCOIN_EVO_DMN_TYPES_H
