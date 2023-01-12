// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2014-2022 The Dash Core developers
// Copyright (c) 2016-2022 The Sparks Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/merkle.h>

#include <tinyformat.h>
#include <util.h>
#include <utilstrencodings.h>

#include <arith_uint256.h>

#include <assert.h>

#include <chainparamsseeds.h>

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

static CBlock CreateDevNetGenesisBlock(const uint256 &prevBlockHash, const std::string& devNetName, uint32_t nTime, uint32_t nNonce, uint32_t nBits, const CAmount& genesisReward)
{
    assert(!devNetName.empty());

    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    // put height (BIP34) and devnet name into coinbase
    txNew.vin[0].scriptSig = CScript() << 1 << std::vector<unsigned char>(devNetName.begin(), devNetName.end());
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = CScript() << OP_RETURN;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = 4;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock = prevBlockHash;
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=00000ffd590b14, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=e0028e, nTime=1390095618, nBits=1e0ffff0, nNonce=28917698, vtx=1)
 *   CTransaction(hash=e0028e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d01044c5957697265642030392f4a616e2f3230313420546865204772616e64204578706572696d656e7420476f6573204c6976653a204f76657273746f636b2e636f6d204973204e6f7720416363657074696e6720426974636f696e73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0xA9037BAC7050C479B121CF)
 *   vMerkleTree: e0028e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "The Sparks Genesis 22.12.2017: Ok, lets go to the moon!";
    const CScript genesisOutputScript = CScript() << ParseHex("048ec1d4d14133344c77703e4c5c722d6b1cb0b840f9531f6b73e93943cddbd2e01e07bd40057a66e0fe19790ffc6d7427cd9088cf6e44c41c4a82b2b92c3b3909") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}


void CChainParams::UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout, int64_t nWindowSize, int64_t nThresholdStart, int64_t nThresholdMin, int64_t nFalloffCoeff)
{
    consensus.vDeployments[d].nStartTime = nStartTime;
    consensus.vDeployments[d].nTimeout = nTimeout;
    if (nWindowSize != -1) {
            consensus.vDeployments[d].nWindowSize = nWindowSize;
    }
    if (nThresholdStart != -1) {
        consensus.vDeployments[d].nThresholdStart = nThresholdStart;
    }
    if (nThresholdMin != -1) {
        consensus.vDeployments[d].nThresholdMin = nThresholdMin;
    }
    if (nFalloffCoeff != -1) {
        consensus.vDeployments[d].nFalloffCoeff = nFalloffCoeff;
    }
}

void CChainParams::UpdateDIP3Parameters(int nActivationHeight, int nEnforcementHeight)
{
    consensus.DIP0003Height = nActivationHeight;
    consensus.DIP0003EnforcementHeight = nEnforcementHeight;
}

void CChainParams::UpdateDIP8Parameters(int nActivationHeight)
{
    consensus.DIP0008Height = nActivationHeight;
}

void CChainParams::UpdateBudgetParameters(int nMasternodePaymentsStartBlock, int nBudgetPaymentsStartBlock, int nSuperblockStartBlock)
{
    consensus.nMasternodePaymentsStartBlock = nMasternodePaymentsStartBlock;
    consensus.nBudgetPaymentsStartBlock = nBudgetPaymentsStartBlock;
    consensus.nSuperblockStartBlock = nSuperblockStartBlock;
}

void CChainParams::UpdateSubsidyAndDiffParams(int nMinimumDifficultyBlocks, int nHighSubsidyBlocks, int nHighSubsidyFactor)
{
    consensus.nMinimumDifficultyBlocks = nMinimumDifficultyBlocks;
    consensus.nHighSubsidyBlocks = nHighSubsidyBlocks;
    consensus.nHighSubsidyFactor = nHighSubsidyFactor;
}

void CChainParams::UpdateLLMQChainLocks(Consensus::LLMQType llmqType) {
    consensus.llmqTypeChainLocks = llmqType;
}

void CChainParams::UpdateLLMQInstantSend(Consensus::LLMQType llmqType) {
    consensus.llmqTypeInstantSend = llmqType;
}

void CChainParams::UpdateLLMQTestParams(int size, int threshold) {
    auto& params = consensus.llmqs.at(Consensus::LLMQ_TEST);
    params.size = size;
    params.minSize = threshold;
    params.threshold = threshold;
    params.dkgBadVotesThreshold = threshold;
}

void CChainParams::UpdateLLMQDevnetParams(int size, int threshold)
{
    auto& params = consensus.llmqs.at(Consensus::LLMQ_DEVNET);
    params.size = size;
    params.minSize = threshold;
    params.threshold = threshold;
    params.dkgBadVotesThreshold = threshold;
}

static CBlock FindDevNetGenesisBlock(const CBlock &prevBlock, const CAmount& reward)
{
    std::string devNetName = gArgs.GetDevNetName();
    assert(!devNetName.empty());

    CBlock block = CreateDevNetGenesisBlock(prevBlock.GetHash(), devNetName.c_str(), prevBlock.nTime + 1, 0, prevBlock.nBits, reward);

    arith_uint256 bnTarget;
    bnTarget.SetCompact(block.nBits);

    for (uint32_t nNonce = 0; nNonce < UINT32_MAX; nNonce++) {
        block.nNonce = nNonce;

        uint256 hash = block.GetHash();
        if (UintToArith256(hash) <= bnTarget)
            return block;
    }

    // This is very unlikely to happen as we start the devnet with a very low difficulty. In many cases even the first
    // iteration of the above loop will give a result already
    error("FindDevNetGenesisBlock: could not find devnet genesis block for %s", devNetName);
    assert(false);
}

// this one is for testing only
static Consensus::LLMQParams llmq_test = {
        .type = Consensus::LLMQ_TEST,
        .name = "llmq_test",
        .size = 5,
        .minSize = 3,
        .threshold = 3,

        .dkgInterval = 24, // one DKG per hour
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 18,
        .dkgBadVotesThreshold = 3,

        .signingActiveQuorumCount = 2, // just a few ones to allow easier testing

        .keepOldConnections = 3,
        .recoveryMembers = 5,
};

// this one is for testing only
static Consensus::LLMQParams llmq_test_v17 = {
        .type = Consensus::LLMQ_TEST_V17,
        .name = "llmq_test_v17",
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
        .recoveryMembers = 3,
};

// this one is for devnets only
static Consensus::LLMQParams llmq_devnet = {
        .type = Consensus::LLMQ_DEVNET,
        .name = "llmq_devnet",
        .size = 10,
        .minSize = 7,
        .threshold = 6,

        .dkgInterval = 24, // one DKG per hour
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 18,
        .dkgBadVotesThreshold = 7,

        .signingActiveQuorumCount = 3, // just a few ones to allow easier testing

        .keepOldConnections = 4,
        .recoveryMembers = 6,
};

static Consensus::LLMQParams llmq50_60 = {
        .type = Consensus::LLMQ_50_60,
        .name = "llmq_50_60",
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
        .recoveryMembers = 25,
};

static Consensus::LLMQParams llmq400_60 = {
        .type = Consensus::LLMQ_400_60,
        .name = "llmq_400_60",
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
        .recoveryMembers = 100,
};

// Used for deployment and min-proto-version signalling, so it needs a higher threshold
static Consensus::LLMQParams llmq400_85 = {
        .type = Consensus::LLMQ_400_85,
        .name = "llmq_400_85",
        .size = 400,
        .minSize = 350,
        .threshold = 340,

        .dkgInterval = 24 * 24, // one DKG every 24 hours
        .dkgPhaseBlocks = 4,
        .dkgMiningWindowStart = 20, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 48, // give it a larger mining window to make sure it is mined
        .dkgBadVotesThreshold = 300,

        .signingActiveQuorumCount = 4, // four days worth of LLMQs

        .keepOldConnections = 5,
        .recoveryMembers = 100,
};

static Consensus::LLMQParams llmq15_60 = {
        .type = Consensus::LLMQ_15_60,
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
        .recoveryMembers = 8,
};

static Consensus::LLMQParams llmq25_60 = {
        .type = Consensus::LLMQ_25_60,
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
        .recoveryMembers = 7,
};

// Used for deployment and min-proto-version signalling, so it needs a higher threshold
static Consensus::LLMQParams llmq25_80 = {
        .type = Consensus::LLMQ_25_80,
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
        .recoveryMembers = 7,
};
// Used for Platform
static Consensus::LLMQParams llmq20_70 = {
        .type = Consensus::LLMQ_20_70,
        .name = "llmq_20_70",
        .size = 20,
        .minSize = 16,
        .threshold = 14,

        .dkgInterval = 24, // one DKG per hour
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 18,
        .dkgBadVotesThreshold = 16,

        .signingActiveQuorumCount = 5, // 5 hours worth of LLMQs

        .keepOldConnections = 5,
        .recoveryMembers = 10,
};


/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */


class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
        consensus.nSubsidyHalvingInterval = 262800; // Note: actual number of blocks per calendar year with DGW v3 is ~200700 (for example 449750 - 249050)
        consensus.nMasternodePaymentsStartBlock = 1152; // not true, but it's ok as long as it's less then nMasternodePaymentsIncreaseBlock
        consensus.nMasternodePaymentsIncreaseBlock = 158000; // actual historical value
        consensus.nMasternodePaymentsIncreasePeriod = 576*30; // 17280 - actual historical value
        consensus.nInstantSendConfirmationsRequired = 6;
        consensus.nInstantSendKeepLock = 24;
        consensus.nSuperblockStartBlock = 354924; // The block at which 12.1 goes live (end of final 12.0 budget cycle)
        consensus.nSuperblockStartHash = uint256S("0x000000000ecf57475bf9ff1887bd4daaa5edeb64f54fef44d8f54c386c6010d3");
        consensus.nSuperblockCycle = 16616; // ~(60*24*30)/2.6, actual number of blocks per month is 200700 / 12 = 16725
        consensus.nGovernanceMinQuorum = 10;
        consensus.nGovernanceFilterElements = 20000;
        consensus.nMasternodeMinimumConfirmations = 15;
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256S("0x00000892299773ac1f4f8bd1408949d81d5b893b7d4d03067d519de13ef377ef");
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.DIP0001Height = 119000;
        consensus.GuardianHeight = 316000; // we need to keep the GuardianHeight for block Subsidy halving changes after this height, otherwise blockchain will not sync
        consensus.DIP0003Height = 919000;
        consensus.DIP0003EnforcementHeight = 919512;
        consensus.DIP0003EnforcementHash = uint256S("00000002e0b0ab0b9026a213554fc4dfd34fb3f46acba6d3d4417a8a09792d81");
        consensus.DIP0008Height = 977700; // 0000000162c9550ef012374fda0e1f6067023fbbc7ec2ad183c88390b3860e0f
        consensus.powLimit = uint256S("00000fffff000000000000000000000000000000000000000000000000000000");

        consensus.nPowTargetTimespan = 60 * 60; // Sparks: 1 hour, 24 blocks
        consensus.nPowTargetSpacing = 2 * 60; // Sparks: 120 seconds
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nPowKGWHeight = 15200; // KGW > DGW, no KGW
        consensus.nPowDGWHeight = 700;
        consensus.nRuleChangeActivationThreshold = 1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1502280000; // Aug 9th, 2017
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1533816000; // Aug 9th, 2018

        // Deployment of DIP0001
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nStartTime = 1528502400; // June 9th, 2018
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nTimeout = 1560556800; // June 15th, 2019
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nWindowSize = 1000;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nThresholdStart = 600;

        // Deployment of BIP147
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nStartTime = 1524477600; // Apr 23th, 2018
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nTimeout = 1556013600; // Apr 23th, 2019
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nWindowSize = 4032;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nThresholdStart = 3226; // 80% of 4032


        // Deployment of DIP0003
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].bit = 4;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nStartTime = 1655856000; // Jun 22nd, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nTimeout = 1687392000; // Jun 22nd, 2023
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nWindowSize = 1000;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nThresholdStart = 700; // 70% of 1000

        // Deployment of DIP0008
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].bit = 5;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nStartTime = 1663891200; // Sep 23rd, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nTimeout = 1695427200; // Sep 23rd, 2023
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nThresholdStart = 60; // 60% of 100

        // Deployment of Block Reward Reallocation
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].bit = 6;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nStartTime = 1668211200; // Nov 12th, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nTimeout = 1699747200; // Nov 12th, 2023
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nThresholdStart = 60; // 60% of 100

        // Deployment of DIP0020, DIP0021 and LLMQ_20_70 quorums
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].bit = 7;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nStartTime = 1668988800; // Nov 21st, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nTimeout = 1700524800; // Nov 21st, 2023
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nThresholdStart = 60; // 60% of 100

        // Deployment of IPv6 Masternodes
        consensus.vDeployments[Consensus::DEPLOYMENT_IPV6_MN].bit = 8;
        consensus.vDeployments[Consensus::DEPLOYMENT_IPV6_MN].nStartTime = 1669593600; // Nov 28th, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_IPV6_MN].nTimeout = 1701129600; // Nov 28th, 2023
        consensus.vDeployments[Consensus::DEPLOYMENT_IPV6_MN].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_IPV6_MN].nThresholdStart = 60; // 60% of 100

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00000000000000000000000000000000000000000000000000eee93cb0b1f4be");//269664

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x000000004516f472b86c0fe081cbd4a0533b87b63ace4a9774d41a6cb53df37a"); //269664

        //Sparks stuff
        consensus.nSPKHeight = 100000;
        consensus.nSPKPremine = 650000;
        consensus.nSPKPostmine = 300000;
        consensus.nSPKSubsidyLegacy = 18;
        consensus.nSPKSubidyReborn = 20;
        consensus.nSPKBlocksPerMonth = 21600;
        consensus.fSPKRatioMN = 0.7;
        consensus.fReallocRatioMN = 0.8;
        consensus.vBannedAddresses.push_back("GRFBCEuMcfi9PhFVfcVutL7bGwj4KdPyWX");
        consensus.vBannedAddresses.push_back("GPawkMiQm4qmYcz6mnM8ad3BxgsdgHjh52");
        consensus.vBannedAddresses.push_back("GSR6AY8GCW8KUf7N5FGz4xxdZpZ3sWkfrR");

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0x1a;
        pchMessageStart[1] = 0xb2;
        pchMessageStart[2] = 0xc3;
        pchMessageStart[3] = 0xd4;
        nDefaultPort = 8890;

        nPruneAfterHeight = 100000;

        genesis = CreateGenesisBlock(1513902562, 682979, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x00000a5c6ddfaac5097218560d5b92d416931cfeba1abf10c81d1d6a232fc8ea"));
        assert(genesis.hashMerkleRoot == uint256S("0x1b3952bab9df804c6f02372bb62df20fa2927030a4e80389ec14c1d86fc921e4"));

        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as a oneshot if they don't support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
        vSeeds.emplace_back("seed1.sparkspay.io");
        vSeeds.emplace_back("seed2.sparkspay.io");
        vSeeds.emplace_back("seed3.sparkspay.io");
        vSeeds.emplace_back("seed4.sparkspay.io");

        // Sparks addresses start with 'G'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,38);
        // Sparks script addresses start with '5'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,10);
        // Sparks private keys start with '5' or 'G' (?)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,198);
        // Sparks BIP32 pubkeys start with 'xpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        // Sparks BIP32 prvkeys start with 'xprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};
        // Sparks BIP44 coin type is '5'
        nExtCoinType = 5;

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        // long living quorum params
        consensus.llmqs[Consensus::LLMQ_50_60] = llmq50_60;
        consensus.llmqs[Consensus::LLMQ_400_60] = llmq400_60;
        consensus.llmqs[Consensus::LLMQ_400_85] = llmq400_85;
        consensus.llmqs[Consensus::LLMQ_15_60] = llmq15_60;
        consensus.llmqs[Consensus::LLMQ_25_60] = llmq25_60;
        consensus.llmqs[Consensus::LLMQ_25_80] = llmq25_80;
        consensus.llmqs[Consensus::LLMQ_20_70] = llmq20_70;
        consensus.llmqTypeChainLocks = Consensus::LLMQ_25_60;
        consensus.llmqTypeInstantSend = Consensus::LLMQ_15_60;
        consensus.llmqTypePlatform = Consensus::LLMQ_20_70;

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fRequireRoutableExternalIP = true;
        fMineBlocksOnDemand = false;
        fAllowMultipleAddressesFromGroup = false;
        fAllowMultiplePorts = false;
        nLLMQConnectionRetryTimeout = 60;

        nPoolMinParticipants = 3;
        nPoolMaxParticipants = 20;
        nFulfilledRequestExpireTime = 60*60; // fulfilled requests expire in 1 hour

        vSporkAddresses = {"GM2oPRsX7nPeSuXGqtB7shojo1F5zWbPfS"};
        nMinSporkKeys = 1;
        fBIP9CheckMasternodesUpgraded = true;

        checkpointData = {
            {
                {0, uint256S("0x00000a5c6ddfaac5097218560d5b92d416931cfeba1abf10c81d1d6a232fc8ea")},
	        {50000, uint256S("0x0000000010f1cfdac6446f431337e3874e6b91ada402e4fe9361778f1a684572")},
                {100000, uint256S("0x0000000006fbc78ab84d3ddf4246d128b36c5240060cfc5ce9bb989a775bf970")},
                {150000, uint256S("0x0000000005f483c970c60293a8d0508cfe6a4500359d26f84abdf302328f413d")},
                {200000, uint256S("0x00000000124558e8e546ebc3a45f05952cb8476c29193c0b2d130d97450f92e4")},
                {250000, uint256S("0x000000000c449edcdcd3fe2ea4a7f2910a13089065acc10a4bf7d15f00c5960e")},
                {317500, uint256S("0x00000000074a05411efe26c57b14780103b638918927a86ee4cf683dfd23a068")},
                {350000, uint256S("0x000000001d364a30921e1c208f5b2a3537c6f848cc16858976bed0dbed848244")},
                {400000, uint256S("0x000000007010d10480818fabe4b47c99e4f2973b78b6ccab3e369204df679eff")},
                {450000, uint256S("0x00000000cf88e545fbefa839ccbce18b24137835f02ac2b6818f8134fe1f9ca2")},
                {500000, uint256S("0x00000000abd8dec8cfb87b00d78344d432b8121f21b2af8a46dd84a20b0c0b51")},
                {550000, uint256S("0x00000000336504dbd915c8b17c3085ee08cc5eaef67e4ec34f1f7863f472149b")},
                {600000, uint256S("0x000000005496a2ba94506effcab8a2490fd126abca3e35916191b41e48b9f24c")},
                {650000, uint256S("0x0000000017e7e5a121dda111924daac4b753da4a7868198d62ca28f66e1a1dd0")},
                {700000, uint256S("0x000000010165bb7e367d0e9567ca851cbb689356529974d51f16f2f7f8b6de64")},
                {750000, uint256S("0x00000001d0d407b1668cfb22af894a0aff67994dacd7e44fd9513a3715d6f6e5")},
                {800000, uint256S("0x000000031757893847ae644d488fa49c9e1d68e062246e28f3e1a9233971647e")},
                {850000, uint256S("0x000000027ad324d353e377330590752a3dec1f0ee2cc679bf7feead6402c5c0c")},
                {900000, uint256S("0x0000000611f05c34587c6ed56364336460b6d7a6ee2cd62133e5b5a574e01dfe")},
                {920000, uint256S("0x000000010cc252a6bad5717ab07e171c4d7eb833877467b54c71d94790330291")},
                {950000, uint256S("0x00000001ff5971fb183ee2237eebf20c109d1992597438949beb8badc99a46b2")},
                {1000000, uint256S("0x00000008ea513eba0aa0d7dda4d2d0179564bea13075584ef024bd9b440bb533")},
                {1012000, uint256S("0x0000000351f0eb82a74f338dc0bbc50b72ebfe3297ddb98098555d04650c93d0")},
                {1050000, uint256S("0x00000000a821ff297d21633435aff4a7c59220bbc046aecc1e17b09f304b600c")},
           }
        };

        chainTxData = ChainTxData{
            1549015815, // * UNIX timestamp of last known number of transactions
            354527,    // * total number of transactions between genesis and that timestamp
                        //   (the tx=... number in the SetBestChain debug.log lines)
            0.1         // * estimated number of transactions per second after that timestamp
        };
    }
};

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        consensus.nSubsidyHalvingInterval = 262800;
        consensus.nMasternodePaymentsStartBlock = 50; // not true, but it's ok as long as it's less then nMasternodePaymentsIncreaseBlock
        consensus.nMasternodePaymentsIncreaseBlock = 1500;
        consensus.nMasternodePaymentsIncreasePeriod = 10;
        consensus.nInstantSendConfirmationsRequired = 2;
        consensus.nInstantSendKeepLock = 6;
        consensus.nSuperblockStartBlock = 2100000000; // NOTE: Should satisfy nSuperblockStartBlock > nBudgetPeymentsStartBlock
        consensus.nBudgetPaymentsStartBlock = 4100;
        consensus.nBudgetPaymentsCycleBlocks = 50;
        consensus.nBudgetPaymentsWindowBlocks = 10;
        consensus.nSuperblockStartHash = uint256(); // do not check this on testnet
        consensus.nSuperblockCycle = 24; // Superblocks can be issued hourly on testnet
        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 500;
        consensus.nMasternodeMinimumConfirmations = 1;
        consensus.BIP34Height = 100; // FIX
        consensus.BIP34Hash = uint256S("0x0000000023b3a96d3484e5abb3755c413e7d41500f8e2a5c3f0dd01299cd8ef8"); // FIX
        consensus.BIP65Height = 100; // 0000039cf01242c7f921dcb4806a5994bc003b48c1973ae0c89b67809c2bb2ab
        consensus.BIP66Height = 100; // 0000002acdd29a14583540cb72e1c5cc83783560e38fa7081495d474fe1671f7
        consensus.DIP0001Height = 100;
        consensus.GuardianHeight = 100;
        consensus.DIP0003Height = 30; // DIP0003 activated at block 30
        consensus.DIP0003EnforcementHeight = 31;
        consensus.DIP0003EnforcementHash = uint256();
        consensus.DIP0008Height = 30; // 00000a66521b7d1d40663e7e24ca99331f5521bad3feed3f6bb3acb3a3030948
        consensus.powLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~uint256(0) >> 20
        consensus.nPowTargetTimespan = 5 * 60; // Sparks: 5 minutes, 50 blocks
        consensus.nPowTargetSpacing = 0.1 * 60; // Sparks: 30 seconds
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nPowKGWHeight = 100; // nPowKGWHeight >= nPowDGWHeight means "no KGW"
        consensus.nPowDGWHeight = 101;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1502280000; // Aug 9th, 2017
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1533816000; // Aug 9th, 2018

        // Deployment of DIP0001
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nStartTime = 1528502400; // June 9th, 2018
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nTimeout = 1560556800; // June 15th, 2019
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nWindowSize = 9;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nThresholdStart = 7; // 7/9 MNs

        // Deployment of BIP147
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nStartTime = 1544655600; // Dec 13th, 2018
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nThresholdStart = 50; // 50% of 100


        // Deployment of DIP0003
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].bit = 4;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nStartTime = 1638786047; // Dec 6th, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nTimeout = 1670302247; // Dec 6th, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nWindowSize = 10;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nThresholdStart = 5; // 50% of 10

        // Deployment of DIP0008
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].bit = 5;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nStartTime = 1658793600; // Jul 26th, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nTimeout = 1690329600; // Jul 26th, 2023
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nWindowSize = 10;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nThresholdStart = 5; // 5% of 10

        // Deployment of Block Reward Reallocation
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].bit = 6;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nStartTime = 1666310400; // Oct 21st, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nTimeout = 1697846400; // Oct 21st, 2023
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nWindowSize = 10;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nThresholdStart = 8; // 80% of 10

        // Deployment of DIP0020, DIP0021 and LLMQ_20_70 quorums
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].bit = 7;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nStartTime = 1668421800; // Nov 14th, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nTimeout = 1699957800; // Nov 14th, 2023
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nWindowSize = 10;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nThresholdStart = 8; // 8% of 10

        // Deployment of IPv6 Masternodes
        consensus.vDeployments[Consensus::DEPLOYMENT_IPV6_MN].bit = 8;
        consensus.vDeployments[Consensus::DEPLOYMENT_IPV6_MN].nStartTime = 1669291200; // Nov 24th, 2022
        consensus.vDeployments[Consensus::DEPLOYMENT_IPV6_MN].nTimeout = 1700827200; // Nov 24th, 2023
        consensus.vDeployments[Consensus::DEPLOYMENT_IPV6_MN].nWindowSize = 10;
        consensus.vDeployments[Consensus::DEPLOYMENT_IPV6_MN].nThresholdStart = 8; // 80% of 10

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0000000000000000000000000000000000000000000000000000000000100010");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x000005f15ec2b9e4495efb539fb5b113338df946291cccd8dfd192bb68cd6dcf");

        //Sparks stuff
        consensus.nSPKHeight = 100;
        consensus.nSPKPremine = 650000;
        consensus.nSPKPostmine = 300000;
        consensus.nSPKSubsidyLegacy = 18;
        consensus.nSPKSubidyReborn = 20;
        consensus.nSPKBlocksPerMonth = 1;
        consensus.fSPKRatioMN = 0.7;
        consensus.fReallocRatioMN = 0.8;
        consensus.vBannedAddresses.push_back("nKhbKA1L4mSn6Xovy4wTtSor1wwRx1GGNL");
        consensus.vBannedAddresses.push_back("nTMbCAsxCEYMEF7fhPiJ6gSPYxnSUbRNHa");

        pchMessageStart[0] = 0xd1;
        pchMessageStart[1] = 0x2b;
        pchMessageStart[2] = 0xb3;
        pchMessageStart[3] = 0x7a;
        nDefaultPort = 8891;

        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1513902563, 109775, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x000005f15ec2b9e4495efb539fb5b113338df946291cccd8dfd192bb68cd6dcf"));
        assert(genesis.hashMerkleRoot == uint256S("0x1b3952bab9df804c6f02372bb62df20fa2927030a4e80389ec14c1d86fc921e4"));

        vFixedSeeds.clear();
        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
        vSeeds.emplace_back("test0.sparkspay.io"); // Just a static list of stable node(s), only supports x9
        vSeeds.emplace_back("test1.sparkspay.io");
        vSeeds.emplace_back("test2.sparkspay.io");
        vSeeds.emplace_back("test3.sparkspay.io");
        vSeeds.emplace_back("test4.sparkspay.io");

        // Testnet Sparks addresses start with 'n'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,112);
        // Testnet Sparks script addresses start with '9'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,20);
        // Testnet private keys start with '9' or 'c' (Bitcoin defaults) (?)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,240);
        // Testnet Sparks BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        // Testnet Sparks BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // Testnet Sparks BIP44 coin type is '1' (All coin's testnet default)
        nExtCoinType = 1;

        // long living quorum params
        consensus.llmqs[Consensus::LLMQ_50_60] = llmq50_60;
        consensus.llmqs[Consensus::LLMQ_400_60] = llmq400_60;
        consensus.llmqs[Consensus::LLMQ_400_85] = llmq400_85;
        consensus.llmqs[Consensus::LLMQ_TEST] = llmq_test;
        consensus.llmqs[Consensus::LLMQ_25_60] = llmq25_60;
        consensus.llmqs[Consensus::LLMQ_25_80] = llmq25_80;
        consensus.llmqs[Consensus::LLMQ_20_70] = llmq20_70;
        consensus.llmqTypeChainLocks = Consensus::LLMQ_TEST;
        consensus.llmqTypeInstantSend = Consensus::LLMQ_TEST;
        consensus.llmqTypePlatform = Consensus::LLMQ_20_70;

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fRequireRoutableExternalIP = true;
        fMineBlocksOnDemand = false;
        fAllowMultipleAddressesFromGroup = false;
        fAllowMultiplePorts = true;
        nLLMQConnectionRetryTimeout = 60;

        nPoolMinParticipants = 2;
        nPoolMaxParticipants = 20;
        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes

        vSporkAddresses = {"nFnDXRQFZWscibREq3EAh5GiPuKc92D2Ac"};
        nMinSporkKeys = 1;
        fBIP9CheckMasternodesUpgraded = true;

        checkpointData = {
            {
                {0, uint256S("0x000005f15ec2b9e4495efb539fb5b113338df946291cccd8dfd192bb68cd6dcf")},
            }
        };

        chainTxData = ChainTxData{
            1513902563, // * UNIX timestamp of last known number of transactions
            0,       // * total number of transactions between genesis and that timestamp
                        //   (the tx=... number in the SetBestChain debug.log lines)
            0.01        // * estimated number of transactions per second after that timestamp
        };

    }
};

/**
 * Devnet
 */
class CDevNetParams : public CChainParams {
public:
    CDevNetParams(bool fHelpOnly = false) {
        strNetworkID = "devnet";
        consensus.nSubsidyHalvingInterval = 210240;
        consensus.nMasternodePaymentsStartBlock = 4010; // not true, but it's ok as long as it's less then nMasternodePaymentsIncreaseBlock
        consensus.nMasternodePaymentsIncreaseBlock = 4030;
        consensus.nMasternodePaymentsIncreasePeriod = 10;
        consensus.nInstantSendConfirmationsRequired = 2;
        consensus.nInstantSendKeepLock = 6;
        consensus.nBudgetPaymentsStartBlock = 4100;
        consensus.nBudgetPaymentsCycleBlocks = 50;
        consensus.nBudgetPaymentsWindowBlocks = 10;
        consensus.nSuperblockStartBlock = 4200; // NOTE: Should satisfy nSuperblockStartBlock > nBudgetPeymentsStartBlock
        consensus.nSuperblockStartHash = uint256(); // do not check this on devnet
        consensus.nSuperblockCycle = 24; // Superblocks can be issued hourly on devnet
        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 500;
        consensus.nMasternodeMinimumConfirmations = 1;
        consensus.BIP34Height = 1; // BIP34 activated immediately on devnet
        consensus.BIP65Height = 1; // BIP65 activated immediately on devnet
        consensus.BIP66Height = 1; // BIP66 activated immediately on devnet
        consensus.DIP0001Height = 2; // DIP0001 activated immediately on devnet
        consensus.DIP0003Height = 2; // DIP0003 activated immediately on devnet
        consensus.DIP0003EnforcementHeight = 2; // DIP0003 activated immediately on devnet
        consensus.DIP0003EnforcementHash = uint256();
        consensus.DIP0008Height = 2; // DIP0008 activated immediately on devnet
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~uint256(0) >> 1
        consensus.nPowTargetTimespan = 24 * 60 * 60; // Sparks: 1 day
        consensus.nPowTargetSpacing = 2.5 * 60; // Sparks: 2.5 minutes
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nPowKGWHeight = 4001; // nPowKGWHeight >= nPowDGWHeight means "no KGW"
        consensus.nPowDGWHeight = 4001;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1506556800; // September 28th, 2017
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 999999999999ULL;

        // Deployment of DIP0001
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nStartTime = 1505692800; // Sep 18th, 2017
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nThresholdStart = 50; // 50% of 100

        // Deployment of BIP147
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nStartTime = 1517792400; // Feb 5th, 2018
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nThresholdStart = 50; // 50% of 100

        // Deployment of DIP0003
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].bit = 3;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nStartTime = 1535752800; // Sep 1st, 2018
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nThresholdStart = 50; // 50% of 100

        // Deployment of DIP0008
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].bit = 4;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nStartTime = 1553126400; // Mar 21st, 2019
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nThresholdStart = 50; // 50% of 100

        // Deployment of Block Reward Reallocation
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].bit = 5;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nStartTime = 1598918400; // Sep 1st, 2020
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nThresholdStart = 80; // 80% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nThresholdMin = 60; // 60% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nFalloffCoeff = 5; // this corresponds to 10 periods

        // Deployment of DIP0020, DIP0021 and LLMQ_20_70 quorums
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].bit = 6;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nStartTime = 1604188800; // November 1st, 2020
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nTimeout = 1635724800; // November 1st, 2021
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nThresholdStart = 80; // 80% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nThresholdMin = 60; // 60% of 100
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nFalloffCoeff = 5; // this corresponds to 10 periods

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000000000000000000000");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x000000000000000000000000000000000000000000000000000000000000000");

        pchMessageStart[0] = 0xe2;
        pchMessageStart[1] = 0xca;
        pchMessageStart[2] = 0xff;
        pchMessageStart[3] = 0xce;
        nDefaultPort = 19799;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1417713337, 1096447, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        // assert(consensus.hashGenesisBlock == uint256S("0x000008ca1832a4baf228eb1553c03d3a2c8e02399550dd6ea8d65cec3ef23d2e"));
        // assert(genesis.hashMerkleRoot == uint256S("0xe0028eb9648db56b1ac77cf090b99048a8007e2bb64b68f092c03c7f56a662c7"));

        if (!fHelpOnly) {
            devnetGenesis = FindDevNetGenesisBlock(genesis, 50 * COIN);
            consensus.hashDevnetGenesisBlock = devnetGenesis.GetHash();
        }

        vFixedSeeds.clear();
        vSeeds.clear();

        // Testnet Sparks addresses start with 'y'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,140);
        // Testnet Sparks script addresses start with '8' or '9'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,19);
        // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        // Testnet Sparks BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        // Testnet Sparks BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // Testnet Sparks BIP44 coin type is '1' (All coin's testnet default)
        nExtCoinType = 1;

        // long living quorum params
        consensus.llmqs[Consensus::LLMQ_DEVNET] = llmq_devnet;
        consensus.llmqs[Consensus::LLMQ_50_60] = llmq50_60;
        consensus.llmqs[Consensus::LLMQ_400_60] = llmq400_60;
        consensus.llmqs[Consensus::LLMQ_400_85] = llmq400_85;
        consensus.llmqs[Consensus::LLMQ_15_60] = llmq15_60;
        consensus.llmqs[Consensus::LLMQ_25_60] = llmq25_60;
        consensus.llmqs[Consensus::LLMQ_25_80] = llmq25_80;
        consensus.llmqs[Consensus::LLMQ_20_70] = llmq20_70;
        consensus.llmqTypeChainLocks = Consensus::LLMQ_15_60;
        consensus.llmqTypeInstantSend = Consensus::LLMQ_15_60;
        consensus.llmqTypePlatform = Consensus::LLMQ_20_70;

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fRequireRoutableExternalIP = true;
        fMineBlocksOnDemand = false;
        fAllowMultipleAddressesFromGroup = true;
        fAllowMultiplePorts = true;
        nLLMQConnectionRetryTimeout = 60;

        nPoolMinParticipants = 2;
        nPoolMaxParticipants = 20;
        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes

        vSporkAddresses = {"yjPtiKh2uwk3bDutTEA2q9mCtXyiZRWn55"};
        nMinSporkKeys = 1;
        // devnets are started with no blocks and no MN, so we can't check for upgraded MN (as there are none)
        fBIP9CheckMasternodesUpgraded = false;

        checkpointData = (CCheckpointData) {
            {
                { 0, uint256S("0x000008ca1832a4baf228eb1553c03d3a2c8e02399550dd6ea8d65cec3ef23d2e")},
                { 1, devnetGenesis.GetHash() },
            }
        };

        chainTxData = ChainTxData{
            devnetGenesis.GetBlockTime(), // * UNIX timestamp of devnet genesis block
            2,                            // * we only have 2 coinbase transactions when a devnet is started up
            0.01                          // * estimated number of transactions per second
        };
    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        consensus.nSubsidyHalvingInterval = 150;
        consensus.nMasternodePaymentsStartBlock = 240;
        consensus.nMasternodePaymentsIncreaseBlock = 350;
        consensus.nMasternodePaymentsIncreasePeriod = 10;
        consensus.nInstantSendConfirmationsRequired = 2;
        consensus.nInstantSendKeepLock = 6;
        consensus.nBudgetPaymentsStartBlock = 1000;
        consensus.nBudgetPaymentsCycleBlocks = 50;
        consensus.nBudgetPaymentsWindowBlocks = 10;
        consensus.nSuperblockStartBlock = 1500;
        consensus.nSuperblockStartHash = uint256(); // do not check this on regtest
        consensus.nSuperblockCycle = 10;
        consensus.nGovernanceMinQuorum = 1;
        consensus.nGovernanceFilterElements = 100;
        consensus.nMasternodeMinimumConfirmations = 1;
        consensus.BIP34Height = 100000000; // BIP34 has not activated on regtest (far in the future so block v1 are not rejected in tests)
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1351; // BIP65 activated on regtest (Used in rpc activation tests)
        consensus.BIP66Height = 1251; // BIP66 activated on regtest (Used in rpc activation tests)
        consensus.DIP0001Height = 2000;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 60 * 60; // Sparks: 1 hour, 24 blocks
        consensus.nPowTargetSpacing = 2 * 60; // Sparks: 150 seconds
        consensus.DIP0003Height = 100;
        consensus.DIP0003EnforcementHeight = 101;
        consensus.DIP0003EnforcementHash = uint256();
        consensus.DIP0008Height = 100;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nPowKGWHeight = 15200; // same as mainnet
        consensus.nPowDGWHeight = 700; // same as mainnet
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0001].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_BIP147].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].bit = 3;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0003].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].bit = 4;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0008].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].bit = 5;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nWindowSize = 500;
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nThresholdStart = 400; // 80%
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nThresholdMin = 300; // 60%
        consensus.vDeployments[Consensus::DEPLOYMENT_REALLOC].nFalloffCoeff = 5;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].bit = 6;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nWindowSize = 100;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nThresholdStart = 80;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nThresholdMin = 60;
        consensus.vDeployments[Consensus::DEPLOYMENT_DIP0020].nFalloffCoeff = 5;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        //Sparks stuff
        consensus.nSPKHeight = 100;
        consensus.nSPKPremine = 650000;
        consensus.nSPKPostmine = 300000;
        consensus.nSPKSubsidyLegacy = 18;
        consensus.nSPKSubidyReborn = 20;
        consensus.nSPKBlocksPerMonth = 1;

        pchMessageStart[0] = 0xa1;
        pchMessageStart[1] = 0xb3;
        pchMessageStart[2] = 0xd5;
        pchMessageStart[3] = 0x7b;
        nDefaultPort = 18891;

        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1513902564, 3483397, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x00000a584fb9211f6dc67cebc024138caa9e387274bf91400cbb2aa49c53ceca"));
        assert(genesis.hashMerkleRoot == uint256S("0x1b3952bab9df804c6f02372bb62df20fa2927030a4e80389ec14c1d86fc921e4"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fRequireRoutableExternalIP = false;
        fMineBlocksOnDemand = true;
        fAllowMultipleAddressesFromGroup = true;
        fAllowMultiplePorts = true;
        nLLMQConnectionRetryTimeout = 1; // must be lower then the LLMQ signing session timeout so that tests have control over failing behavior

        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes
        nPoolMinParticipants = 2;
        nPoolMaxParticipants = 20;

        // privKey: cP4EKFyJsHT39LDqgdcB43Y3YXjNyjb5Fuas1GQSeAtjnZWmZEQK
        vSporkAddresses = {"nCYRgPB26SYEf37EmUPnZSqT6Uy4UjqWNm"};
        nMinSporkKeys = 1;
        // regtest usually has no masternodes in most tests, so don't check for upgraged MNs
        fBIP9CheckMasternodesUpgraded = false;

        checkpointData = {
            {
                {0, uint256S("0x00000a584fb9211f6dc67cebc024138caa9e387274bf91400cbb2aa49c53ceca")},
            }
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };
        // Regtest Sparks addresses start with 'n'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,112);
        // Regtest Sparks script addresses start with '9'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,20);
        // Regtest private keys start with '9' or 'c' (Bitcoin defaults) (?)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,240);
        // Regtest Sparks BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        // Regtest Sparks BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // Regtest Sparks BIP44 coin type is '1' (All coin's testnet default)
        nExtCoinType = 1;

        // long living quorum params
        consensus.llmqs[Consensus::LLMQ_TEST] = llmq_test;
        consensus.llmqs[Consensus::LLMQ_TEST_V17] = llmq_test_v17;
        consensus.llmqTypeChainLocks = Consensus::LLMQ_TEST;
        consensus.llmqTypeInstantSend = Consensus::LLMQ_TEST;
        consensus.llmqTypePlatform = Consensus::LLMQ_TEST;
    }
};

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<CChainParams> CreateChainParams(const std::string& chain, bool fHelpOnly)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CChainParams>(new CMainParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    else if (chain == CBaseChainParams::DEVNET) {
        return std::unique_ptr<CChainParams>(new CDevNetParams(fHelpOnly));
    } else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CChainParams>(new CRegTestParams());
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(network);
}

void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout, int64_t nWindowSize, int64_t nThresholdStart, int64_t nThresholdMin, int64_t nFalloffCoeff)
{
    globalChainParams->UpdateVersionBitsParameters(d, nStartTime, nTimeout, nWindowSize, nThresholdStart, nThresholdMin, nFalloffCoeff);
}

void UpdateDIP3Parameters(int nActivationHeight, int nEnforcementHeight)
{
    globalChainParams->UpdateDIP3Parameters(nActivationHeight, nEnforcementHeight);
}

void UpdateDIP8Parameters(int nActivationHeight)
{
    globalChainParams->UpdateDIP8Parameters(nActivationHeight);
}

void UpdateBudgetParameters(int nMasternodePaymentsStartBlock, int nBudgetPaymentsStartBlock, int nSuperblockStartBlock)
{
    globalChainParams->UpdateBudgetParameters(nMasternodePaymentsStartBlock, nBudgetPaymentsStartBlock, nSuperblockStartBlock);
}

void UpdateDevnetSubsidyAndDiffParams(int nMinimumDifficultyBlocks, int nHighSubsidyBlocks, int nHighSubsidyFactor)
{
    globalChainParams->UpdateSubsidyAndDiffParams(nMinimumDifficultyBlocks, nHighSubsidyBlocks, nHighSubsidyFactor);
}

void UpdateDevnetLLMQChainLocks(Consensus::LLMQType llmqType)
{
    globalChainParams->UpdateLLMQChainLocks(llmqType);
}

void UpdateDevnetLLMQInstantSend(Consensus::LLMQType llmqType)
{
    globalChainParams->UpdateLLMQInstantSend(llmqType);
}

void UpdateLLMQTestParams(int size, int threshold)
{
    globalChainParams->UpdateLLMQTestParams(size, threshold);
}

void UpdateLLMQDevnetParams(int size, int threshold)
{
    globalChainParams->UpdateLLMQDevnetParams(size, threshold);
}
