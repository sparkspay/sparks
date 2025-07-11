// Copyright (c) 2014-2023 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SPORK_H
#define BITCOIN_SPORK_H

#include <hash.h>
#include <key.h>
#include <net.h>
#include <net_types.h>
#include <pubkey.h>
#include <saltedhasher.h>
#include <sync.h>
#include <uint256.h>

#include <array>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

class CConnman;
template<typename T>
class CFlatDB;
class CNode;
class CDataStream;
class PeerManager;

class CSporkMessage;
class CSporkManager;

/*
    Don't ever reuse these IDs for other sporks
    - This would result in old clients getting confused about which spork is for what
*/
enum SporkId : int32_t {
    SPORK_2_INSTANTSEND_ENABLED                            = 10001,
    SPORK_3_INSTANTSEND_BLOCK_FILTERING                    = 10002,
    SPORK_9_SUPERBLOCKS_ENABLED                            = 10008,
    SPORK_18_QUORUM_DKG_ENABLED                            = 10017,
    SPORK_19_CHAINLOCKS_ENABLED                            = 10018,
    SPORK_21_QUORUM_ALL_CONNECTED                          = 10020,
    SPORK_23_QUORUM_POSE                                   = 10022,
    SPORK_24_DATATX_FEE                                    = 10023,
    SPORK_25_TEST_EHF                                      = 10024,

    SPORK_INVALID                                          = -1,
};
template<> struct is_serializable_enum<SporkId> : std::true_type {};

namespace std
{
    template<> struct hash<SporkId>
    {
        std::size_t operator()(SporkId const& id) const noexcept
        {
            return std::hash<int>{}(id);
        }
    };
}

using SporkValue = int64_t;
struct CSporkDef
{
    SporkId sporkId{SPORK_INVALID};
    SporkValue defaultValue{0};
    std::string_view name;
};

#define MAKE_SPORK_DEF(name, defaultValue) CSporkDef{name, defaultValue, #name}
[[maybe_unused]] static constexpr std::array<CSporkDef, 9> sporkDefs = {
    MAKE_SPORK_DEF(SPORK_2_INSTANTSEND_ENABLED,            4070908800ULL), // OFF
    MAKE_SPORK_DEF(SPORK_3_INSTANTSEND_BLOCK_FILTERING,    4070908800ULL), // OFF
    MAKE_SPORK_DEF(SPORK_9_SUPERBLOCKS_ENABLED,            4070908800ULL), // OFF
    MAKE_SPORK_DEF(SPORK_18_QUORUM_DKG_ENABLED,            4070908800ULL), // OFF
    MAKE_SPORK_DEF(SPORK_19_CHAINLOCKS_ENABLED,            4070908800ULL), // OFF
    MAKE_SPORK_DEF(SPORK_21_QUORUM_ALL_CONNECTED,          4070908800ULL), // OFF
    MAKE_SPORK_DEF(SPORK_23_QUORUM_POSE,                   4070908800ULL), // OFF
    MAKE_SPORK_DEF(SPORK_24_DATATX_FEE,                    1000000), // default datatx min fee rate
    MAKE_SPORK_DEF(SPORK_25_TEST_EHF,                      4070908800ULL), // OFF
};
#undef MAKE_SPORK_DEF

/**
 * Sporks are network parameters used primarily to prevent forking and turn
 * on/off certain features. They are a soft consensus mechanism.
 *
 * We use 2 main classes to manage the spork system.
 *
 * SporkMessages - low-level constructs which contain the sporkID, value,
 *                 signature and a signature timestamp
 * SporkManager  - a higher-level construct which manages the naming, use of
 *                 sporks, signatures and verification, and which sporks are active according
 *                 to this node
 */

/**
 * CSporkMessage is a low-level class used to encapsulate Spork messages and
 * serialize them for transmission to other peers. This includes the internal
 * spork ID, value, spork signature and timestamp for the signature.
 */
class CSporkMessage
{
private:
    std::vector<unsigned char> vchSig;

public:
    SporkId nSporkID{0};
    SporkValue nValue{0};
    int64_t nTimeSigned{0};

    CSporkMessage(SporkId nSporkID, SporkValue nValue, int64_t nTimeSigned) :
        nSporkID(nSporkID),
        nValue(nValue),
        nTimeSigned(nTimeSigned)
        {}

    CSporkMessage() = default;

    SERIALIZE_METHODS(CSporkMessage, obj)
    {
        READWRITE(obj.nSporkID, obj.nValue, obj.nTimeSigned, obj.vchSig);
    }

    /**
     * GetHash returns the double-sha256 hash of the serialized spork message.
     */
    uint256 GetHash() const;

    /**
     * GetSignatureHash returns the hash of the serialized spork message
     * without the signature included. The intent of this method is to get the
     * hash to be signed.
     */
    uint256 GetSignatureHash() const;

    /**
     * Sign will sign the spork message with the given key.
     */
    bool Sign(const CKey& key);

    /**
     * CheckSignature will ensure the spork signature matches the provided public
     * key hash.
     */
    bool CheckSignature(const CKeyID& pubKeyId) const;

    /**
     * GetSignerKeyID is used to recover the spork address of the key used to
     * sign this spork message.
     *
     * This method was introduced along with the multi-signer sporks feature,
     * in order to identify which spork key signed this message.
     */
    std::optional<CKeyID> GetSignerKeyID() const;

    /**
     * Relay is used to send this spork message to other peers.
     */
    void Relay(PeerManager& peerman) const;
};

class SporkStore
{
protected:
    static const std::string SERIALIZATION_VERSION_STRING;

    mutable Mutex cs;

    std::unordered_map<uint256, CSporkMessage, StaticSaltedHasher> mapSporksByHash GUARDED_BY(cs);
    std::unordered_map<SporkId, std::map<CKeyID, CSporkMessage> > mapSporksActive GUARDED_BY(cs);

public:
    template<typename Stream>
    void Serialize(Stream &s) const EXCLUSIVE_LOCKS_REQUIRED(!cs)
    {
        // We don't serialize pubkey ids because pubkeys should be
        // hardcoded or be set with cmdline or options, should
        // not reuse pubkeys from previous sparksd run.
        // We don't serialize private key to prevent its leakage.
        LOCK(cs);
        s << SERIALIZATION_VERSION_STRING << mapSporksByHash << mapSporksActive;
    }

    template<typename Stream>
    void Unserialize(Stream &s) EXCLUSIVE_LOCKS_REQUIRED(!cs)
    {
        LOCK(cs);
        std::string strVersion;
        s >> strVersion;
        if (strVersion != SERIALIZATION_VERSION_STRING) {
            return;
        }
        s >> mapSporksByHash >> mapSporksActive;
    }

    /**
     * Clear is used to clear all in-memory active spork messages. Since spork
     * public and private keys are set in init.cpp, we do not clear them here.
     *
     * This method was introduced along with the spork cache.
     */
    void Clear() EXCLUSIVE_LOCKS_REQUIRED(!cs);

    /**
     * ToString returns the string representation of the SporkManager.
     */
    std::string ToString() const EXCLUSIVE_LOCKS_REQUIRED(!cs);
};

/**
 * CSporkManager is a higher-level class which manages the node's spork
 * messages, rules for which sporks should be considered active/inactive, and
 * processing for certain sporks (e.g. spork 12).
 */
class CSporkManager : public SporkStore
{
private:
    using db_type = CFlatDB<SporkStore>;

private:
    const std::unique_ptr<db_type> m_db;
    bool is_valid{false};

    mutable Mutex cs_mapSporksCachedActive;
    mutable std::unordered_map<const SporkId, bool> mapSporksCachedActive GUARDED_BY(cs_mapSporksCachedActive);

    mutable Mutex cs_mapSporksCachedValues;
    mutable std::unordered_map<SporkId, SporkValue> mapSporksCachedValues GUARDED_BY(cs_mapSporksCachedValues);

    std::set<CKeyID> setSporkPubKeyIDs GUARDED_BY(cs);
    int nMinSporkKeys GUARDED_BY(cs) {std::numeric_limits<int>::max()};
    CKey sporkPrivKey GUARDED_BY(cs);

    /**
     * SporkValueIfActive is used to get the value agreed upon by the majority
     * of signed spork messages for a given Spork ID.
     */
    std::optional<SporkValue> SporkValueIfActive(SporkId nSporkID) const EXCLUSIVE_LOCKS_REQUIRED(cs);

public:
    CSporkManager();
    ~CSporkManager();

    bool LoadCache();

    bool IsValid() const { return is_valid; }

    /**
     * CheckAndRemove is defined to fulfill an interface as part of the on-disk
     * cache used to cache sporks between runs. If sporks that are restored
     * from cache do not have valid signatures when compared against the
     * current spork private keys, they are removed from in-memory storage.
     *
     * This method was introduced along with the spork cache.
     */
    void CheckAndRemove() EXCLUSIVE_LOCKS_REQUIRED(!cs);

    /**
     * ProcessMessage is used to call ProcessSpork and ProcessGetSporks. See below
     */
    PeerMsgRet ProcessMessage(CNode& peer, CConnman& connman, PeerManager& peerman, std::string_view msg_type, CDataStream& vRecv);

    /**
     * ProcessSpork is used to handle the 'spork' p2p message.
     *
     * For 'spork', it validates the spork and adds it to the internal spork storage and
     * performs any necessary processing.
     */
    PeerMsgRet ProcessSpork(const CNode& peer, PeerManager& peerman, CDataStream& vRecv) EXCLUSIVE_LOCKS_REQUIRED(!cs);

    /**
     * ProcessGetSporks is used to handle the 'getsporks' p2p message.
     *
     * For 'getsporks', it sends active sporks to the requesting peer.
     */
    void ProcessGetSporks(CNode& peer, CConnman& connman) EXCLUSIVE_LOCKS_REQUIRED(!cs);

    /**
     * UpdateSpork is used by the spork RPC command to set a new spork value, sign
     * and broadcast the spork message.
     */
    bool UpdateSpork(PeerManager& peerman, SporkId nSporkID, SporkValue nValue) EXCLUSIVE_LOCKS_REQUIRED(!cs);

    /**
     * IsSporkActive returns a bool for time-based sporks, and should be used
     * to determine whether the spork can be considered active or not.
     * For value-based sporks such as SPORK_5_INSTANTSEND_MAX_VALUE, the spork
     * value should not be considered a timestamp, but an integer value
     * instead, and therefore this method doesn't make sense and should not be
     * used.
     */
    bool IsSporkActive(SporkId nSporkID) const;

    /**
     * GetSporkValue returns the spork value given a Spork ID. If no active spork
     * message has yet been received by the node, it returns the default value.
     */
    SporkValue GetSporkValue(SporkId nSporkID) const EXCLUSIVE_LOCKS_REQUIRED(!cs);

    /**
     * GetSporkIDByName returns the internal Spork ID given the spork name.
     */
    static SporkId GetSporkIDByName(std::string_view strName);

    /**
     * GetSporkByHash returns a spork message given a hash of the spork message.
     *
     * This is used when a requesting peer sends a MSG_SPORK inventory message with
     * the hash, to quickly lookup and return the full spork message. We maintain a
     * hash-based index of sporks for this reason, and this function is the access
     * point into that index.
     */
    std::optional<CSporkMessage> GetSporkByHash(const uint256& hash) const EXCLUSIVE_LOCKS_REQUIRED(!cs);

    /**
     * SetSporkAddress is used to set a public key ID which will be used to
     * verify spork signatures.
     *
     * This can be called multiple times to add multiple keys to the set of
     * valid spork signers.
     */
    bool SetSporkAddress(const std::string& strAddress) EXCLUSIVE_LOCKS_REQUIRED(!cs);

    /**
     * SetMinSporkKeys is used to set the required spork signer threshold, for
     * a spork to be considered active.
     *
     * This value must be at least a majority of the total number of spork
     * keys, and for obvious reasons cannot be larger than that number.
     */
    bool SetMinSporkKeys(int minSporkKeys) EXCLUSIVE_LOCKS_REQUIRED(!cs);

    /**
     * SetPrivKey is used to set a spork key to enable setting / signing of
     * spork values.
     *
     * This will return false if the private key does not match any spork
     * address in the set of valid spork signers (see SetSporkAddress).
     */
    bool SetPrivKey(const std::string& strPrivKey) EXCLUSIVE_LOCKS_REQUIRED(!cs);
};

#endif // BITCOIN_SPORK_H