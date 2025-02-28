
// Copyright (c) 2018-2024 The Sparks Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_EVO_DATATX_H
#define BITCOIN_EVO_DATATX_H

#include <primitives/transaction.h>
#include <univalue.h>

class CBlock;
class CBlockIndex;
class CCoinsViewCache;
class TxValidationState;

namespace llmq {
class CQuorumBlockProcessor;
}// namespace llmq

// data transaction
class CDataTx
{
public:
    static constexpr auto SPECIALTX_TYPE = TRANSACTION_DATA;
    static constexpr uint16_t CURRENT_VERSION = 1;

public:
    uint16_t nVersion{CURRENT_VERSION};
    std::vector<unsigned char> data;

public:
    SERIALIZE_METHODS(CDataTx, obj){ READWRITE(obj.nVersion, obj.data); }

    std::string ToString() const;

    void ToJson(UniValue& obj) const
    {
        obj.clear();
        obj.setObject();
        obj.pushKV("version", (int)nVersion);
        std::string s_data(data.begin(), data.end());
        obj.pushKV("data", s_data);
    }

};

bool CheckDataTx(const CTransaction& tx, const CBlockIndex* pindexPrev, TxValidationState& state);

#endif // BITCOIN_EVO_DATATX_H