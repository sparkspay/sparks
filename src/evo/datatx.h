
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
    static constexpr auto SPECIALTX_TYPE = TRANSACTION_MNHF_SIGNAL;
    static const uint16_t CURRENT_VERSION = 1;

public:
    uint16_t nVersion{CURRENT_VERSION};
    std::vector<unsigned char> GUID;
    uint256 hash;

public:
    SERIALIZE_METHODS(CDataTx, obj){ READWRITE(obj.nVersion, obj.GUID, obj.hash); }

    std::string ToString() const;

    void ToJson(UniValue& obj) const
    {
        obj.clear();
        obj.setObject();
        obj.pushKV("version", (int)nVersion);
        std::string s_guid(GUID.begin(), GUID.end());
        obj.pushKV("GUID", s_guid);
        obj.pushKV("hash", hash.ToString());
    }

};

bool CheckDataTx(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state);

#endif // BITCOIN_EVO_DATATX_H