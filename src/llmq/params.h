// Copyright (c) 2021-2024 The Dash Core developers
// Copyright (c) 2021-2025 The Sparks Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LLMQ_PARAMS_H
#define BITCOIN_LLMQ_PARAMS_H

#include <array>
#include <cstdint>
#include <string_view>

namespace Consensus {

enum class LLMQType : uint8_t {
    LLMQ_NONE = 0xff,

    LLMQ_50_60 = 1,  // 50 members, 30 (60%) threshold, one per hour
    LLMQ_400_60 = 2, // 400 members, 240 (60%) threshold, one every 12 hours
    LLMQ_400_85 = 3, // 400 members, 340 (85%) threshold, one every 24 hours
    LLMQ_15_60 = 4, // 15 members, 9 (60%) threshold, one per hour
    LLMQ_25_60 = 5, // 25 members, 15 (60%) threshold, one every 12 hours
    LLMQ_25_80 = 6, // 25 members, 20 (80%) threshold, one every 24 hours
    LLMQ_20_70 = 7, // 20 members, 14 (70%) threshold, one per hour
    LLMQ_20_75 = 8,  // 20 members, 15 (75%) threshold, one every 12 hours
    LLMQ_25_67 = 9, // 25 members, 17 (67%) threshold, one per hour
    // for testing only
    LLMQ_TEST = 100, // 3 members, 2 (66%) threshold, one per hour. Params might differ when -llmqtestparams is used

    // for devnets only
    LLMQ_DEVNET = 101, // 12 members, 6 (50%) threshold, one per hour. Params might differ when -llmqdevnetparams is used
    LLMQ_DEVNET_PLATFORM = 107, // 12 members, 8 (67%) threshold, one per hour.

    // for testing activation of new quorums only
    LLMQ_TEST_V17 = 102, // 3 members, 2 (66%) threshold, one per hour. Params might differ when -llmqtestparams is used

    // for testing only
    LLMQ_TEST_DIP0024 = 103,     // 4 members, 3 (75%) threshold, one per hour.
    LLMQ_TEST_INSTANTSEND = 104, // 3 members, 2 (66%) threshold, one per hour. Params might differ when -llmqtestinstantsendparams is used
    LLMQ_TEST_PLATFORM = 106,    // 3 members, 2 (66%) threshold, one per hour.

    // for devnets only. rotated version (v2) for devnets
    LLMQ_DEVNET_DIP0024 = 105 // 8 members, 4 (50%) threshold, one per hour. Params might differ when -llmqdevnetparams is used
};

// Configures a LLMQ and its DKG
// See https://github.com/sparkspay/dips/blob/master/dip-0006.md for more details
struct LLMQParams {
    LLMQType type;

    // not consensus critical, only used in logging, RPC and UI
    std::string_view name;

    // Whether this is a DIP0024 quorum or not
    bool useRotation;

    // the size of the quorum, e.g. 50 or 400
    int size;

    // The minimum number of valid members after the DKK. If less members are determined valid, no commitment can be
    // created. Should be higher then the threshold to allow some room for failing nodes, otherwise quorum might end up
    // not being able to ever created a recovered signature if more nodes fail after the DKG
    int minSize;

    // The threshold required to recover a final signature. Should be at least 50%+1 of the quorum size. This value
    // also controls the size of the public key verification vector and has a large influence on the performance of
    // recovery. It also influences the amount of minimum messages that need to be exchanged for a single signing session.
    // This value has the most influence on the security of the quorum. The number of total malicious masternodes
    // required to negatively influence signing sessions highly correlates to the threshold percentage.
    int threshold;

    // The interval in number blocks for DKGs and the creation of LLMQs. If set to 24 for example, a DKG will start
    // every 24 blocks, which is approximately once every hour.
    int dkgInterval;

    // The number of blocks per phase in a DKG session. There are 6 phases plus the mining phase that need to be processed
    // per DKG. Set this value to a number of blocks so that each phase has enough time to propagate all required
    // messages to all members before the next phase starts. If blocks are produced too fast, whole DKG sessions will
    // fail.
    int dkgPhaseBlocks;

    // The starting block inside the DKG interval for when mining of commitments starts. The value is inclusive.
    // Starting from this block, the inclusion of (possibly null) commitments is enforced until the first non-null
    // commitment is mined. The chosen value should be at least 5 * dkgPhaseBlocks so that it starts right after the
    // finalization phase.
    int dkgMiningWindowStart;

    // The ending block inside the DKG interval for when mining of commitments ends. The value is inclusive.
    // Choose a value so that miners have enough time to receive the commitment and mine it. Also take into consideration
    // that miners might omit real commitments and revert to always including null commitments. The mining window should
    // be large enough so that other miners have a chance to produce a block containing a non-null commitment. The window
    // should at the same time not be too large so that not too much space is wasted with null commitments in case a DKG
    // session failed.
    int dkgMiningWindowEnd;

    // In the complaint phase, members will vote on other members being bad (missing valid contribution). If at least
    // dkgBadVotesThreshold have voted for another member to be bad, it will considered to be bad by all other members
    // as well. This serves as a protection against late-comers who send their contribution on the bring of
    // phase-transition, which would otherwise result in inconsistent views of the valid members set
    int dkgBadVotesThreshold;

    // Number of quorums to consider "active" for signing sessions
    int signingActiveQuorumCount;

    // Used for intra-quorum communication. This is the number of quorums for which we should keep old connections.
    // For non-rotated quorums it should be at least one more than the active quorums set.
    // For rotated quorums it should be equal to 2 x active quorums set.
    int keepOldConnections;

    // The number of quorums for which we should keep keys. Usually it's equal to signingActiveQuorumCount * 2.
    // Unlike for other quorum types we want to keep data (secret key shares and vvec)
    // for Platform quorums for much longer because Platform can be restarted and
    // it must be able to re-sign stuff.

    int keepOldKeys;

    // How many members should we try to send all sigShares to before we give up.
    int recoveryMembers;
public:

    [[ nodiscard ]] constexpr int max_cycles(int quorums_count) const
    {
        return useRotation ? quorums_count / signingActiveQuorumCount : quorums_count;
    }

    // For how many blocks recent DKG info should be kept
    [[ nodiscard ]] constexpr int max_store_depth() const
    {
        return max_cycles(keepOldKeys) * dkgInterval;
    }
};

//static_assert(std::is_trivial_v<Consensus::LLMQParams>, "LLMQParams is not a trivial type");
static_assert(std::is_trivially_copyable_v<Consensus::LLMQParams>, "LLMQParams is not trivially copyable");
//static_assert(std::is_trivially_default_constructible_v<Consensus::LLMQParams>, "LLMQParams is not trivially default constructible");
static_assert(std::is_trivially_assignable_v<Consensus::LLMQParams, Consensus::LLMQParams>, "LLMQParams is not trivially assignable");


static constexpr std::array<LLMQParams, 18> available_llmqs = {

    /**
     * llmq_test
     * This quorum is only used for testing
     *
     */
    LLMQParams{
        .type = LLMQType::LLMQ_TEST,
        .name = "llmq_test",
        .useRotation = false,
        .size = 5,
        .minSize = 3,
        .threshold =3,

        .dkgInterval = 24, // one DKG per hour
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 18,
        .dkgBadVotesThreshold =3,

        .signingActiveQuorumCount = 2, // just a few ones to allow easier testing

        .keepOldConnections = 3,
        .keepOldKeys = 4,
        .recoveryMembers = 5,
    },

    /**
     * llmq_test_instantsend (same as llmq_test but used for InstantSend exclusively)
     * This quorum is only used for testing
     *
     */
    LLMQParams{
        .type = LLMQType::LLMQ_TEST_INSTANTSEND,
        .name = "llmq_test_instantsend",
        .useRotation = false,
        .size = 3,
        .minSize = 2,
        .threshold = 2,

        .dkgInterval = 24, // one DKG per hour
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 18,
        .dkgBadVotesThreshold = 2,

        .signingActiveQuorumCount = 2, // just a few ones to allow easier testing

        .keepOldConnections = 3,
        .keepOldKeys = 4,
        .recoveryMembers = 3,
    },

    /**
     * llmq_test (Sparks Core 0.17) aka llmq_test_v17
     * This quorum is only used for testing
     *
     */
    LLMQParams{
        .type = LLMQType::LLMQ_TEST_V17,
        .name = "llmq_test_v17",
        .useRotation = false,
        .size = 3,
        .minSize = 2,
        .threshold = 2,

        .dkgInterval = 24, // one DKG per hour
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 18,
        .dkgBadVotesThreshold = 2,

        .signingActiveQuorumCount = 2, // just a few ones to allow easier testing

        .keepOldConnections = 3,
        .keepOldKeys = 4,
        .recoveryMembers = 3,
    },

    /**
     * llmq_test_dip0024
     * This quorum is only used for testing
     *
     */
    LLMQParams{
        .type = LLMQType::LLMQ_TEST_DIP0024,
        .name = "llmq_test_dip0024",
        .useRotation = true,
        .size = 4,
        .minSize = 4,
        .threshold = 3,

        .dkgInterval = 24, // DKG cycle
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 12, // signingActiveQuorumCount + dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 20,
        .dkgBadVotesThreshold = 2,

        .signingActiveQuorumCount = 2, // just a few ones to allow easier testing

        .keepOldConnections = 4,
        .keepOldKeys = 4,
        .recoveryMembers = 3,
    },

    /**
     * llmq_test_platform
     * This quorum is only used for testing
     *
     */
    LLMQParams{
        .type = LLMQType::LLMQ_TEST_PLATFORM,
        .name = "llmq_test_platform",
        .useRotation = false,
        .size = 3,
        .minSize = 2,
        .threshold = 2,

        .dkgInterval = 24, // DKG cycle
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 10, // signingActiveQuorumCount + dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 18,
        .dkgBadVotesThreshold = 2,

        .signingActiveQuorumCount = 2, // just a few ones to allow easier testing

        .keepOldConnections = 4,
        .keepOldKeys = 24 * 30 * 2, // 2 months of quorums
        .recoveryMembers = 3,
    },

    /**
     * llmq_devnet
     * This quorum is only used for testing on devnets
     *
     */
    LLMQParams{
        .type = LLMQType::LLMQ_DEVNET,
        .name = "llmq_devnet",
        .useRotation = false,
        .size = 12,
        .minSize = 7,
        .threshold = 6,

        .dkgInterval = 24, // one DKG per hour
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 18,
        .dkgBadVotesThreshold = 7,

        .signingActiveQuorumCount = 4, // just a few ones to allow easier testing

        .keepOldConnections = 5,
        .keepOldKeys = 8,
        .recoveryMembers = 6,
    },

    /**
     * llmq_devnet_dip0024
     * This quorum is only used for testing on devnets
     *
     */
    LLMQParams{
        .type = LLMQType::LLMQ_DEVNET_DIP0024,
        .name = "llmq_devnet_dip0024",
        .useRotation = true,
        .size = 8,
        .minSize = 6,
        .threshold = 4,

        .dkgInterval = 48, // DKG cycle
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 12, // signingActiveQuorumCount + dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 20,
        .dkgBadVotesThreshold = 7,

        .signingActiveQuorumCount = 2, // just a few ones to allow easier testing

        .keepOldConnections = 4,
        .keepOldKeys = 4,
        .recoveryMembers = 4,
    },

    /**
     * llmq_devnet_platform
     * This quorum is only used for testing on devnets
     *
     */
    LLMQParams{
        .type = LLMQType::LLMQ_DEVNET_PLATFORM,
        .name = "llmq_devnet_platform",
        .useRotation = false,
        .size = 12,
        .minSize = 9,
        .threshold = 8,

        .dkgInterval = 24, // one DKG per hour
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 18,
        .dkgBadVotesThreshold = 7,

        .signingActiveQuorumCount = 4, // just a few ones to allow easier testing

        .keepOldConnections = 5,
        .keepOldKeys = 24 * 30 * 2, // 2 months of quorums
        .recoveryMembers = 6,
    },

    /**
     * llmq_50_60
     * This quorum is deployed on mainnet and requires
     * 40 - 50 participants
     *
     */
    LLMQParams{
        .type = LLMQType::LLMQ_50_60,
        .name = "llmq_50_60",
        .useRotation = false,
        .size = 50,
        .minSize = 40,
        .threshold = 30,

        .dkgInterval = 24, // one DKG per hour
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 18,
        .dkgBadVotesThreshold = 40,

        .signingActiveQuorumCount = 24, // a full day worth of LLMQs
        .keepOldConnections = 25,
        .keepOldKeys = 48,
        .recoveryMembers = 25,
    },

    /**
     * llmq_20_75
     * This quorum is deployed on mainnet and requires
     * 50 - 60 participants
     *
     */
    LLMQParams{
        .type = LLMQType::LLMQ_20_75,
        .name = "llmq_20_75",
        .useRotation = true,
        .size = 20,
        .minSize = 18,
        .threshold = 15,

        .dkgInterval = 24 * 12, // DKG cycle every 12 hours
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 42, // signingActiveQuorumCount + dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 50,
        .dkgBadVotesThreshold = 18,

        .signingActiveQuorumCount = 32,
        .keepOldConnections = 64,
        .keepOldKeys = 64,
        .recoveryMembers = 10,
    },

    /**
     * llmq_400_60
     * This quorum is deployed on mainnet and requires
     * 300 - 400 participants
     *
     */
    LLMQParams{
        .type = LLMQType::LLMQ_400_60,
        .name = "llmq_400_60",
        .useRotation = false,
        .size = 400,
        .minSize = 300,
        .threshold = 240,

        .dkgInterval = 24 * 12, // one DKG every 12 hours
        .dkgPhaseBlocks = 4,
        .dkgMiningWindowStart = 20, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 28,
        .dkgBadVotesThreshold = 300,

        .signingActiveQuorumCount = 4, // two days worth of LLMQs

        .keepOldConnections = 5,
        .keepOldKeys = 8,
        .recoveryMembers = 100,
    },

    /**
     * llmq_400_85
     * This quorum is deployed on mainnet and requires
     * 300 - 400 participants _with_ a supermajority
     *
     * Used for deployment and min-proto-version signalling
     */
    LLMQParams{
        .type = LLMQType::LLMQ_400_85,
        .name = "llmq_400_85",
        .useRotation = false,
        .size = 400,
        .minSize = 350,
        .threshold = 340,

        .dkgInterval = 24 * 24, // one DKG every 24 hours
        .dkgPhaseBlocks = 4,
        .dkgMiningWindowStart = 20, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 48,   // give it a larger mining window to make sure it is mined
        .dkgBadVotesThreshold = 300,

        .signingActiveQuorumCount = 4, // four days worth of LLMQs

        .keepOldConnections = 5,
        .keepOldKeys = 8,
        .recoveryMembers = 100,
    },

    LLMQParams{
        .type = LLMQType::LLMQ_15_60,
        .name = "llmq_15_60",
        .size = 15,
        .minSize = 12,
        .threshold = 9,

        .dkgInterval = 24, // one DKG per hour
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 18,
        .dkgBadVotesThreshold = 12,

        .signingActiveQuorumCount = 5, // 5 hours worth of LLMQs

        .keepOldConnections = 25,
        .keepOldKeys = 10,
        .recoveryMembers = 8,
    },

    LLMQParams{
        .type = LLMQType::LLMQ_25_60,
        .name = "llmq_25_60",
        .size = 25,
        .minSize = 20,
        .threshold = 15,

        .dkgInterval = 24 * 12, // one DKG every 12 hours
        .dkgPhaseBlocks = 4,
        .dkgMiningWindowStart = 20, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 28,
        .dkgBadVotesThreshold = 20,

        .signingActiveQuorumCount = 4, // two days worth of LLMQs

        .keepOldConnections = 5,
        .keepOldKeys = 8,
        .recoveryMembers = 7,
    },

    // Used for deployment and min-proto-version signalling, so it needs a higher threshold
    LLMQParams{
        .type = LLMQType::LLMQ_25_80,
        .name = "llmq_25_80",
        .size = 25,
        .minSize = 23,
        .threshold = 20,

        .dkgInterval = 24 * 24, // one DKG every 24 hours
        .dkgPhaseBlocks = 4,
        .dkgMiningWindowStart = 20, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 48, // give it a larger mining window to make sure it is mined
        .dkgBadVotesThreshold = 23,

        .signingActiveQuorumCount = 4, // two days worth of LLMQs

        .keepOldConnections = 5,
        .keepOldKeys = 8,
        .recoveryMembers = 7,
    },


    /**
     * llmq_20_70
     * This quorum is deployed on mainnet and requires
     * 16 - 20 participants
     *
     * Used by Sparks Platform
     */
    LLMQParams{
        .type = LLMQType::LLMQ_20_70,
        .name = "llmq_20_70",
        .useRotation = false,
        .size = 20,
        .minSize = 16,
        .threshold = 14,

        .dkgInterval = 24, // one DKG per hour
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 18,
        .dkgBadVotesThreshold = 16,

        .signingActiveQuorumCount = 5, // a full day worth of LLMQs

        .keepOldConnections = 5,
        .keepOldKeys = 24 * 30 * 2, // 2 months of quorums
        .recoveryMembers = 10,
    },

    /**
     * llmq_25_67
     * This quorum is deployed on Testnet and requires
     * 25 participants
     *
     * Used by Sparks Platform
     */
    LLMQParams{
        .type = LLMQType::LLMQ_25_67,
        .name = "llmq_25_67",
        .useRotation = false,
        .size = 25,
        .minSize = 22,
        .threshold = 17,

        .dkgInterval = 24, // one DKG per hour
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 18,
        .dkgBadVotesThreshold = 22,

        .signingActiveQuorumCount = 24, // a full day worth of LLMQs

        .keepOldConnections = 25,
        .keepOldKeys = 24 * 30 * 2, // 2 months of quorums
        .recoveryMembers = 12,
    },

}; // available_llmqs

} // namespace Consensus

// This must be outside of all namespaces. We must also duplicate the forward declaration of is_serializable_enum to
// avoid inclusion of serialize.h here.
template<typename T> struct is_serializable_enum;
template<> struct is_serializable_enum<Consensus::LLMQType> : std::true_type {};

#endif // BITCOIN_LLMQ_PARAMS_H
