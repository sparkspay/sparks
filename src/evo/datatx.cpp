// Copyright (c) 2018-2024 The Sparks Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evo/datatx.h>
#include <evo/specialtx.h>

#include <chain.h>
#include <chainparams.h>
#include <consensus/merkle.h>

bool CheckDataTx(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state)
{
    return true;
}