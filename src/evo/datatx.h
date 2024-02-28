
// Copyright (c) 2018-2024 The Sparks Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_EVO_DATATX_H
#define BITCOIN_EVO_DATATX_H

#include <consensus/validation.h>
#include <primitives/transaction.h>
#include <univalue.h>

class CBlock;
class CBlockIndex;
class CCoinsViewCache;

// data transaction
class CDataTx
{
public:
    static const uint16_t CURRENT_VERSION = 1;

public:
    uint16_t nVersion{CURRENT_VERSION};
    std::vector<unsigned char> data;

public:
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nVersion);
        READWRITE(data);
    }

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

bool CheckDataTx(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state);

#endif // BITCOIN_EVO_DATATX_H