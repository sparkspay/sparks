// Copyright (c) 2018-2024 The Sparks Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evo/datatx.h>
#include <evo/specialtx.h>
#include <consensus/validation.h>
#include <validation.h>

#include <chain.h>
#include <chainparams.h>
#include <consensus/merkle.h>

bool CheckDataTx(const CTransaction& tx, const CBlockIndex* pindexPrev, TxValidationState& state)
{
    if (tx.nType != TRANSACTION_DATA) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-datatx-type");
    }

    std::optional<CDataTx> dataTx = GetTxPayload<CDataTx>(tx);
    if (!dataTx.has_value()) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-datatx-payload");
    }

    bool fDataTXActive;
    {
        LOCK(cs_main);
        fDataTXActive = pindexPrev->nHeight + 1 >= Params().GetConsensus().DATATXHeight;
    }

    if (!fDataTXActive) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-datatx-not-active");
    }

    return true;
}