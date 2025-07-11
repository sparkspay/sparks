// Copyright (c) 2018-2024 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <llmq/utils.h>

#include <llmq/options.h>
#include <llmq/snapshot.h>

#include <chainparams.h>
#include <deploymentstatus.h>
#include <evo/deterministicmns.h>
#include <evo/evodb.h>
#include <masternode/meta.h>
#include <net.h>
#include <random.h>
#include <util/irange.h>
#include <util/ranges.h>
#include <util/time.h>
#include <util/underlying.h>
#include <validation.h>

#include <atomic>
#include <optional>

class CBLSSignature;
namespace llmq
{
class CQuorum;
using CQuorumPtr = std::shared_ptr<CQuorum>;
using CQuorumCPtr = std::shared_ptr<const CQuorum>;
}

/**
 * Forward declarations
 */
std::optional<std::pair<CBLSSignature, uint32_t>> GetNonNullCoinbaseChainlock(const CBlockIndex* pindex);

static bool IsV19Active(gsl::not_null<const CBlockIndex*> pindexPrev)
{
    return DeploymentActiveAfter(pindexPrev, Params().GetConsensus(), Consensus::DEPLOYMENT_V19);
}

static bool IsV20Active(gsl::not_null<const CBlockIndex*> pindexPrev)
{
    return DeploymentActiveAfter(pindexPrev, Params().GetConsensus(), Consensus::DEPLOYMENT_V20);
}

namespace llmq
{

namespace utils

{
//QuorumMembers per quorumIndex at heights H-Cycle, H-2Cycles, H-3Cycles
struct PreviousQuorumQuarters {
    std::vector<std::vector<CDeterministicMNCPtr>> quarterHMinusC;
    std::vector<std::vector<CDeterministicMNCPtr>> quarterHMinus2C;
    std::vector<std::vector<CDeterministicMNCPtr>> quarterHMinus3C;
    explicit PreviousQuorumQuarters(size_t s) :
        quarterHMinusC(s), quarterHMinus2C(s), quarterHMinus3C(s) {}
};

// Forward declarations
static std::vector<CDeterministicMNCPtr> ComputeQuorumMembers(Consensus::LLMQType llmqType, CDeterministicMNManager& dmnman, const CBlockIndex* pQuorumBaseBlockIndex);
static std::vector<std::vector<CDeterministicMNCPtr>> ComputeQuorumMembersByQuarterRotation(const Consensus::LLMQParams& llmqParams, CDeterministicMNManager& dmnman, const CBlockIndex* pCycleQuorumBaseBlockIndex);

static std::vector<std::vector<CDeterministicMNCPtr>> BuildNewQuorumQuarterMembers(const Consensus::LLMQParams& llmqParams, CDeterministicMNManager& dmnman, const CBlockIndex* pCycleQuorumBaseBlockIndex, const PreviousQuorumQuarters& quarters);

static PreviousQuorumQuarters GetPreviousQuorumQuarterMembers(const Consensus::LLMQParams& llmqParams, CDeterministicMNManager& dmnman, const CBlockIndex* pBlockHMinusCIndex, const CBlockIndex* pBlockHMinus2CIndex, const CBlockIndex* pBlockHMinus3CIndex, int nHeight);
static std::vector<std::vector<CDeterministicMNCPtr>> GetQuorumQuarterMembersBySnapshot(const Consensus::LLMQParams& llmqParams, CDeterministicMNManager& dmnman, const CBlockIndex* pCycleQuorumBaseBlockIndex, const llmq::CQuorumSnapshot& snapshot, int nHeights);
static std::pair<CDeterministicMNList, CDeterministicMNList> GetMNUsageBySnapshot(const Consensus::LLMQParams& llmqParams, CDeterministicMNManager& dmnman, const CBlockIndex* pCycleQuorumBaseBlockIndex, const llmq::CQuorumSnapshot& snapshot, int nHeight);

static void BuildQuorumSnapshot(const Consensus::LLMQParams& llmqParams, const CDeterministicMNList& allMns, const CDeterministicMNList& mnUsedAtH, std::vector<CDeterministicMNCPtr>& sortedCombinedMns, CQuorumSnapshot& quorumSnapshot, int nHeight, std::vector<int>& skipList, const CBlockIndex* pCycleQuorumBaseBlockIndex);

static uint256 GetHashModifier(const Consensus::LLMQParams& llmqParams, gsl::not_null<const CBlockIndex*> pCycleQuorumBaseBlockIndex)
{
    ASSERT_IF_DEBUG(pCycleQuorumBaseBlockIndex->nHeight % llmqParams.dkgInterval == 0);
    const CBlockIndex* pWorkBlockIndex = pCycleQuorumBaseBlockIndex->GetAncestor(pCycleQuorumBaseBlockIndex->nHeight - 8);

    if (IsV20Active(pWorkBlockIndex)) {
        // v20 is active: calculate modifier using the new way.
        auto cbcl = GetNonNullCoinbaseChainlock(pWorkBlockIndex);
        if (cbcl.has_value()) {
            // We have a non-null CL signature: calculate modifier using this CL signature
            auto& [bestCLSignature, bestCLHeightDiff] = cbcl.value();
            return ::SerializeHash(std::make_tuple(llmqParams.type, pWorkBlockIndex->nHeight, bestCLSignature));
        }
        // No non-null CL signature found in coinbase: calculate modifier using block hash only
        return ::SerializeHash(std::make_pair(llmqParams.type, pWorkBlockIndex->GetBlockHash()));
    }

    // v20 isn't active yet: calculate modifier using the usual way
    if (llmqParams.useRotation) {
        return ::SerializeHash(std::make_pair(llmqParams.type, pWorkBlockIndex->GetBlockHash()));
    }
    return ::SerializeHash(std::make_pair(llmqParams.type, pCycleQuorumBaseBlockIndex->GetBlockHash()));
}

std::vector<CDeterministicMNCPtr> GetAllQuorumMembers(Consensus::LLMQType llmqType, CDeterministicMNManager& dmnman, gsl::not_null<const CBlockIndex*> pQuorumBaseBlockIndex, bool reset_cache)
{
    static RecursiveMutex cs_members;
    static std::map<Consensus::LLMQType, unordered_lru_cache<uint256, std::vector<CDeterministicMNCPtr>, StaticSaltedHasher>> mapQuorumMembers GUARDED_BY(cs_members);
    static RecursiveMutex cs_indexed_members;
    static std::map<Consensus::LLMQType, unordered_lru_cache<std::pair<uint256, int>, std::vector<CDeterministicMNCPtr>, StaticSaltedHasher>> mapIndexedQuorumMembers GUARDED_BY(cs_indexed_members);
    if (!IsQuorumTypeEnabled(llmqType, pQuorumBaseBlockIndex->pprev)) {
        return {};
    }
    std::vector<CDeterministicMNCPtr> quorumMembers;
    {
        LOCK(cs_members);
        if (mapQuorumMembers.empty()) {
            InitQuorumsCache(mapQuorumMembers);
        }
        if (reset_cache) {
            mapQuorumMembers[llmqType].clear();
        } else if (mapQuorumMembers[llmqType].get(pQuorumBaseBlockIndex->GetBlockHash(), quorumMembers)) {
            return quorumMembers;
        }
    }

    const auto& llmq_params_opt = Params().GetLLMQ(llmqType);
    assert(llmq_params_opt.has_value());
    const auto& llmq_params = llmq_params_opt.value();

    if (IsQuorumRotationEnabled(llmq_params, pQuorumBaseBlockIndex)) {
        if (LOCK(cs_indexed_members); mapIndexedQuorumMembers.empty()) {
            InitQuorumsCache(mapIndexedQuorumMembers);
        }
        /*
         * Quorums created with rotation are now created in a different way. All signingActiveQuorumCount are created during the period of dkgInterval.
         * But they are not created exactly in the same block, they are spread overtime: one quorum in each block until all signingActiveQuorumCount are created.
         * The new concept of quorumIndex is introduced in order to identify them.
         * In every dkgInterval blocks (also called CycleQuorumBaseBlock), the spread quorum creation starts like this:
         * For quorumIndex = 0 : signingActiveQuorumCount
         * Quorum Q with quorumIndex is created at height CycleQuorumBaseBlock + quorumIndex
         */

        int quorumIndex = pQuorumBaseBlockIndex->nHeight % llmq_params.dkgInterval;
        if (quorumIndex >= llmq_params.signingActiveQuorumCount) {
            return {};
        }
        int cycleQuorumBaseHeight = pQuorumBaseBlockIndex->nHeight - quorumIndex;
        const CBlockIndex* pCycleQuorumBaseBlockIndex = pQuorumBaseBlockIndex->GetAncestor(cycleQuorumBaseHeight);

        /*
         * Since mapQuorumMembers stores Quorum members per block hash, and we don't know yet the block hashes of blocks for all quorumIndexes (since these blocks are not created yet)
         * We store them in a second cache mapIndexedQuorumMembers which stores them by {CycleQuorumBaseBlockHash, quorumIndex}
         */
        if (reset_cache) {
            LOCK(cs_indexed_members);
            mapIndexedQuorumMembers[llmqType].clear();
        } else if (LOCK(cs_indexed_members); mapIndexedQuorumMembers[llmqType].get(std::pair(pCycleQuorumBaseBlockIndex->GetBlockHash(), quorumIndex), quorumMembers)) {
            LOCK(cs_members);
            mapQuorumMembers[llmqType].insert(pQuorumBaseBlockIndex->GetBlockHash(), quorumMembers);
            return quorumMembers;
        }

        auto q = ComputeQuorumMembersByQuarterRotation(llmq_params, dmnman, pCycleQuorumBaseBlockIndex);
        LOCK(cs_indexed_members);
        for (const size_t i : irange::range(q.size())) {
            mapIndexedQuorumMembers[llmqType].insert(std::make_pair(pCycleQuorumBaseBlockIndex->GetBlockHash(), i), q[i]);
        }

        quorumMembers = q[quorumIndex];
    } else {
        quorumMembers = ComputeQuorumMembers(llmqType, dmnman, pQuorumBaseBlockIndex);
    }

    LOCK(cs_members);
    mapQuorumMembers[llmqType].insert(pQuorumBaseBlockIndex->GetBlockHash(), quorumMembers);
    return quorumMembers;
}

std::vector<CDeterministicMNCPtr> ComputeQuorumMembers(Consensus::LLMQType llmqType, CDeterministicMNManager& dmnman, const CBlockIndex* pQuorumBaseBlockIndex)
{
    bool EvoOnly = (Params().GetConsensus().llmqTypePlatform == llmqType) && IsV19Active(pQuorumBaseBlockIndex);
    const auto& llmq_params_opt = Params().GetLLMQ(llmqType);
    assert(llmq_params_opt.has_value());
    if (llmq_params_opt->useRotation || pQuorumBaseBlockIndex->nHeight % llmq_params_opt->dkgInterval != 0) {
        ASSERT_IF_DEBUG(false);
        return {};
    }

    const CBlockIndex* pWorkBlockIndex = IsV20Active(pQuorumBaseBlockIndex) ?
            pQuorumBaseBlockIndex->GetAncestor(pQuorumBaseBlockIndex->nHeight - 8) :
            pQuorumBaseBlockIndex;
    const auto modifier = GetHashModifier(llmq_params_opt.value(), pQuorumBaseBlockIndex);
    auto allMns = dmnman.GetListForBlock(pWorkBlockIndex);
    return allMns.CalculateQuorum(llmq_params_opt->size, modifier, EvoOnly);
}

std::vector<std::vector<CDeterministicMNCPtr>> ComputeQuorumMembersByQuarterRotation(const Consensus::LLMQParams& llmqParams, CDeterministicMNManager& dmnman, const CBlockIndex* pCycleQuorumBaseBlockIndex)
{
    const Consensus::LLMQType llmqType = llmqParams.type;

    const int cycleLength = llmqParams.dkgInterval;
    if (!llmqParams.useRotation || pCycleQuorumBaseBlockIndex->nHeight % llmqParams.dkgInterval != 0) {
        ASSERT_IF_DEBUG(false);
        return {};
    }

    const CBlockIndex* pBlockHMinusCIndex = pCycleQuorumBaseBlockIndex->GetAncestor(pCycleQuorumBaseBlockIndex->nHeight - cycleLength);
    const CBlockIndex* pBlockHMinus2CIndex = pCycleQuorumBaseBlockIndex->GetAncestor(pCycleQuorumBaseBlockIndex->nHeight - 2 * cycleLength);
    const CBlockIndex* pBlockHMinus3CIndex = pCycleQuorumBaseBlockIndex->GetAncestor(pCycleQuorumBaseBlockIndex->nHeight - 3 * cycleLength);
    const CBlockIndex* pWorkBlockIndex = pCycleQuorumBaseBlockIndex->GetAncestor(pCycleQuorumBaseBlockIndex->nHeight - 8);
    auto allMns = dmnman.GetListForBlock(pWorkBlockIndex);
    LogPrint(BCLog::LLMQ, "ComputeQuorumMembersByQuarterRotation llmqType[%d] nHeight[%d] allMns[%d]\n", ToUnderlying(llmqType), pCycleQuorumBaseBlockIndex->nHeight, allMns.GetValidMNsCount());

    PreviousQuorumQuarters previousQuarters = GetPreviousQuorumQuarterMembers(llmqParams, dmnman, pBlockHMinusCIndex, pBlockHMinus2CIndex, pBlockHMinus3CIndex, pCycleQuorumBaseBlockIndex->nHeight);

    size_t nQuorums = static_cast<size_t>(llmqParams.signingActiveQuorumCount);
    std::vector<std::vector<CDeterministicMNCPtr>> quorumMembers{nQuorums};

    auto newQuarterMembers = BuildNewQuorumQuarterMembers(llmqParams, dmnman, pCycleQuorumBaseBlockIndex, previousQuarters);
    //TODO Check if it is triggered from outside (P2P, block validation). Throwing an exception is probably a wiser choice
    //assert (!newQuarterMembers.empty());

    if (LogAcceptCategory(BCLog::LLMQ)) {
        for (const size_t i : irange::range(nQuorums)) {
            std::stringstream ss;

            ss << " 3Cmns[";
            for (const auto &m: previousQuarters.quarterHMinus3C[i]) {
                ss << m->proTxHash.ToString().substr(0, 4) << "|";
            }
            ss << " ] 2Cmns[";
            for (const auto &m: previousQuarters.quarterHMinus2C[i]) {
                ss << m->proTxHash.ToString().substr(0, 4) << "|";
            }
            ss << " ] Cmns[";
            for (const auto &m: previousQuarters.quarterHMinusC[i]) {
                ss << m->proTxHash.ToString().substr(0, 4) << "|";
            }
            ss << " ] new[";
            for (const auto &m: newQuarterMembers[i]) {
                ss << m->proTxHash.ToString().substr(0, 4) << "|";
            }
            ss << " ]";
            LogPrint(BCLog::LLMQ, "QuarterComposition h[%d] i[%d]:%s\n", pCycleQuorumBaseBlockIndex->nHeight, i,
                     ss.str());
        }
    }

    for (const size_t i : irange::range(nQuorums)) {
        // Move elements from previous quarters into quorumMembers
        std::move(previousQuarters.quarterHMinus3C[i].begin(), previousQuarters.quarterHMinus3C[i].end(), std::back_inserter(quorumMembers[i]));
        std::move(previousQuarters.quarterHMinus2C[i].begin(), previousQuarters.quarterHMinus2C[i].end(), std::back_inserter(quorumMembers[i]));
        std::move(previousQuarters.quarterHMinusC[i].begin(), previousQuarters.quarterHMinusC[i].end(), std::back_inserter(quorumMembers[i]));
        std::move(newQuarterMembers[i].begin(), newQuarterMembers[i].end(), std::back_inserter(quorumMembers[i]));

        if (LogAcceptCategory(BCLog::LLMQ)) {
            std::stringstream ss;
            ss << " [";
            for (const auto &m: quorumMembers[i]) {
                ss << m->proTxHash.ToString().substr(0, 4) << "|";
            }
            ss << "]";
            LogPrint(BCLog::LLMQ, "QuorumComposition h[%d] i[%d]:%s\n", pCycleQuorumBaseBlockIndex->nHeight, i,
                     ss.str());
        }
    }

    return quorumMembers;
}

PreviousQuorumQuarters GetPreviousQuorumQuarterMembers(const Consensus::LLMQParams& llmqParams,
                                                       CDeterministicMNManager& dmnman,
                                                       const CBlockIndex* pBlockHMinusCIndex,
                                                       const CBlockIndex* pBlockHMinus2CIndex,
                                                       const CBlockIndex* pBlockHMinus3CIndex,
                                                       int nHeight)
{
    size_t nQuorums = static_cast<size_t>(llmqParams.signingActiveQuorumCount);
    PreviousQuorumQuarters quarters{nQuorums};

    std::optional<llmq::CQuorumSnapshot> quSnapshotHMinusC = quorumSnapshotManager->GetSnapshotForBlock(llmqParams.type, pBlockHMinusCIndex);
    if (quSnapshotHMinusC.has_value()) {
        quarters.quarterHMinusC = GetQuorumQuarterMembersBySnapshot(llmqParams, dmnman, pBlockHMinusCIndex, quSnapshotHMinusC.value(), nHeight);
        //TODO Check if it is triggered from outside (P2P, block validation). Throwing an exception is probably a wiser choice
        //assert (!quarterHMinusC.empty());

        std::optional<llmq::CQuorumSnapshot> quSnapshotHMinus2C = quorumSnapshotManager->GetSnapshotForBlock(llmqParams.type, pBlockHMinus2CIndex);
        if (quSnapshotHMinus2C.has_value()) {
            quarters.quarterHMinus2C = GetQuorumQuarterMembersBySnapshot(llmqParams, dmnman, pBlockHMinus2CIndex, quSnapshotHMinus2C.value(), nHeight);
            //TODO Check if it is triggered from outside (P2P, block validation). Throwing an exception is probably a wiser choice
            //assert (!quarterHMinusC.empty());

            std::optional<llmq::CQuorumSnapshot> quSnapshotHMinus3C = quorumSnapshotManager->GetSnapshotForBlock(llmqParams.type, pBlockHMinus3CIndex);
            if (quSnapshotHMinus3C.has_value()) {
                quarters.quarterHMinus3C = GetQuorumQuarterMembersBySnapshot(llmqParams, dmnman, pBlockHMinus3CIndex, quSnapshotHMinus3C.value(), nHeight);
                //TODO Check if it is triggered from outside (P2P, block validation). Throwing an exception is probably a wiser choice
                //assert (!quarterHMinusC.empty());
            }
        }
    }

    return quarters;
}

std::vector<std::vector<CDeterministicMNCPtr>> BuildNewQuorumQuarterMembers(const Consensus::LLMQParams& llmqParams,
                                                                            CDeterministicMNManager& dmnman,
                                                                            const CBlockIndex* pCycleQuorumBaseBlockIndex,
                                                                            const PreviousQuorumQuarters& previousQuarters)
{
    if (!llmqParams.useRotation || pCycleQuorumBaseBlockIndex->nHeight % llmqParams.dkgInterval != 0) {
        ASSERT_IF_DEBUG(false);
        return {};
    }

    size_t nQuorums = static_cast<size_t>(llmqParams.signingActiveQuorumCount);
    std::vector<std::vector<CDeterministicMNCPtr>> quarterQuorumMembers{nQuorums};

    size_t quorumSize = static_cast<size_t>(llmqParams.size);
    auto quarterSize{quorumSize / 4};
    const CBlockIndex* pWorkBlockIndex = pCycleQuorumBaseBlockIndex->GetAncestor(pCycleQuorumBaseBlockIndex->nHeight - 8);
    const auto modifier = GetHashModifier(llmqParams, pCycleQuorumBaseBlockIndex);

    auto allMns = dmnman.GetListForBlock(pWorkBlockIndex);

    if (allMns.GetValidMNsCount() < quarterSize) {
        return quarterQuorumMembers;
    }

    auto MnsUsedAtH = CDeterministicMNList();
    auto MnsNotUsedAtH = CDeterministicMNList();
    std::vector<CDeterministicMNList> MnsUsedAtHIndexed{nQuorums};

    bool skipRemovedMNs = IsV19Active(pCycleQuorumBaseBlockIndex) || (Params().NetworkIDString() == CBaseChainParams::TESTNET);

    for (const size_t i : irange::range(nQuorums)) {
        for (const auto& mn : previousQuarters.quarterHMinusC[i]) {
            if (skipRemovedMNs && !allMns.HasMN(mn->proTxHash)) {
                continue;
            }
            if (allMns.IsMNPoSeBanned(mn->proTxHash)) {
                continue;
            }
            try {
                MnsUsedAtH.AddMN(mn);
            } catch (const std::runtime_error& e) {
            }
            try {
                MnsUsedAtHIndexed[i].AddMN(mn);
            } catch (const std::runtime_error& e) {
            }
        }
        for (const auto& mn : previousQuarters.quarterHMinus2C[i]) {
            if (skipRemovedMNs && !allMns.HasMN(mn->proTxHash)) {
                continue;
            }
            if (allMns.IsMNPoSeBanned(mn->proTxHash)) {
                continue;
            }
            try {
                MnsUsedAtH.AddMN(mn);
            } catch (const std::runtime_error& e) {
            }
            try {
                MnsUsedAtHIndexed[i].AddMN(mn);
            } catch (const std::runtime_error& e) {
            }
        }
        for (const auto& mn : previousQuarters.quarterHMinus3C[i]) {
            if (skipRemovedMNs && !allMns.HasMN(mn->proTxHash)) {
                continue;
            }
            if (allMns.IsMNPoSeBanned(mn->proTxHash)) {
                continue;
            }
            try {
                MnsUsedAtH.AddMN(mn);
            } catch (const std::runtime_error& e) {
            }
            try {
                MnsUsedAtHIndexed[i].AddMN(mn);
            } catch (const std::runtime_error& e) {
            }
        }
    }

    allMns.ForEachMNShared(false, [&MnsUsedAtH, &MnsNotUsedAtH](const CDeterministicMNCPtr& dmn) {
        if (!MnsUsedAtH.HasMN(dmn->proTxHash)) {
            if (!dmn->pdmnState->IsBanned()) {
                try {
                    MnsNotUsedAtH.AddMN(dmn);
                } catch (const std::runtime_error& e) {
                }
            }
        }
    });

    auto sortedMnsUsedAtHM = MnsUsedAtH.CalculateQuorum(MnsUsedAtH.GetAllMNsCount(), modifier);
    auto sortedMnsNotUsedAtH = MnsNotUsedAtH.CalculateQuorum(MnsNotUsedAtH.GetAllMNsCount(), modifier);
    auto sortedCombinedMnsList = std::move(sortedMnsNotUsedAtH);
    for (auto& m : sortedMnsUsedAtHM) {
        sortedCombinedMnsList.push_back(std::move(m));
    }

    if (LogAcceptCategory(BCLog::LLMQ)) {
        std::stringstream ss;
        ss << " [";
        for (const auto &m: sortedCombinedMnsList) {
            ss << m->proTxHash.ToString().substr(0, 4) << "|";
        }
        ss << "]";
        LogPrint(BCLog::LLMQ, "BuildNewQuorumQuarterMembers h[%d] sortedCombinedMns[%s]\n",
                 pCycleQuorumBaseBlockIndex->nHeight, ss.str());
    }

    std::vector<int> skipList;
    size_t firstSkippedIndex = 0;
    size_t idx{0};
    for (const size_t i : irange::range(nQuorums)) {
        auto usedMNsCount = MnsUsedAtHIndexed[i].GetAllMNsCount();
        bool updated{false};
        size_t initial_loop_idx = idx;
        while (quarterQuorumMembers[i].size() < quarterSize && (usedMNsCount + quarterQuorumMembers[i].size() < sortedCombinedMnsList.size())) {
            bool skip{true};
            if (!MnsUsedAtHIndexed[i].HasMN(sortedCombinedMnsList[idx]->proTxHash)) {
                try {
                    // NOTE: AddMN is the one that can throw exceptions, must be exicuted first
                    MnsUsedAtHIndexed[i].AddMN(sortedCombinedMnsList[idx]);
                    quarterQuorumMembers[i].push_back(sortedCombinedMnsList[idx]);
                    updated = true;
                    skip = false;
                } catch (const std::runtime_error& e) {
                }
            }
            if (skip) {
                if (firstSkippedIndex == 0) {
                    firstSkippedIndex = idx;
                    skipList.push_back(idx);
                } else {
                    skipList.push_back(idx - firstSkippedIndex);
                }
            }
            if (++idx == sortedCombinedMnsList.size()) {
                idx = 0;
            }
            if (idx == initial_loop_idx) {
                // we made full "while" loop
                if (!updated) {
                    // there are not enough MNs, there is nothing we can do here
                    return std::vector<std::vector<CDeterministicMNCPtr>>(nQuorums);
                }
                // reset and try again
                updated = false;
            }
        }
    }

    CQuorumSnapshot quorumSnapshot = {};

    BuildQuorumSnapshot(llmqParams, allMns, MnsUsedAtH, sortedCombinedMnsList, quorumSnapshot, pCycleQuorumBaseBlockIndex->nHeight, skipList, pCycleQuorumBaseBlockIndex);

    quorumSnapshotManager->StoreSnapshotForBlock(llmqParams.type, pCycleQuorumBaseBlockIndex, quorumSnapshot);

    return quarterQuorumMembers;
}

void BuildQuorumSnapshot(const Consensus::LLMQParams& llmqParams, const CDeterministicMNList& allMns,
                         const CDeterministicMNList& mnUsedAtH, std::vector<CDeterministicMNCPtr>& sortedCombinedMns,
                         CQuorumSnapshot& quorumSnapshot, int nHeight, std::vector<int>& skipList, const CBlockIndex* pCycleQuorumBaseBlockIndex)
{
    if (!llmqParams.useRotation || pCycleQuorumBaseBlockIndex->nHeight % llmqParams.dkgInterval != 0) {
        ASSERT_IF_DEBUG(false);
        return;
    }

    quorumSnapshot.activeQuorumMembers.resize(allMns.GetAllMNsCount());
    const auto modifier = GetHashModifier(llmqParams, pCycleQuorumBaseBlockIndex);
    auto sortedAllMns = allMns.CalculateQuorum(allMns.GetAllMNsCount(), modifier);

    LogPrint(BCLog::LLMQ, "BuildQuorumSnapshot h[%d] numMns[%d]\n", pCycleQuorumBaseBlockIndex->nHeight, allMns.GetAllMNsCount());

    std::fill(quorumSnapshot.activeQuorumMembers.begin(),
              quorumSnapshot.activeQuorumMembers.end(),
              false);
    size_t index = {};
    for (const auto& dmn : sortedAllMns) {
        if (mnUsedAtH.HasMN(dmn->proTxHash)) {
            quorumSnapshot.activeQuorumMembers[index] = true;
        }
        index++;
    }

    if (skipList.empty()) {
        quorumSnapshot.mnSkipListMode = SnapshotSkipMode::MODE_NO_SKIPPING;
        quorumSnapshot.mnSkipList.clear();
    } else {
        quorumSnapshot.mnSkipListMode = SnapshotSkipMode::MODE_SKIPPING_ENTRIES;
        quorumSnapshot.mnSkipList = std::move(skipList);
    }
}

std::vector<std::vector<CDeterministicMNCPtr>> GetQuorumQuarterMembersBySnapshot(const Consensus::LLMQParams& llmqParams,
                                                                                 CDeterministicMNManager& dmnman,
                                                                                 const CBlockIndex* pCycleQuorumBaseBlockIndex,
                                                                                 const llmq::CQuorumSnapshot& snapshot,
                                                                                 int nHeight)
{
    if (!llmqParams.useRotation || pCycleQuorumBaseBlockIndex->nHeight % llmqParams.dkgInterval != 0) {
        ASSERT_IF_DEBUG(false);
        return {};
    }

    std::vector<CDeterministicMNCPtr> sortedCombinedMns;
    {
        const auto modifier = GetHashModifier(llmqParams, pCycleQuorumBaseBlockIndex);
        const auto [MnsUsedAtH, MnsNotUsedAtH] = GetMNUsageBySnapshot(llmqParams, dmnman, pCycleQuorumBaseBlockIndex, snapshot, nHeight);
        // the list begins with all the unused MNs
        auto sortedMnsNotUsedAtH = MnsNotUsedAtH.CalculateQuorum(MnsNotUsedAtH.GetAllMNsCount(), modifier);
        sortedCombinedMns = std::move(sortedMnsNotUsedAtH);
        // Now add the already used MNs to the end of the list
        auto sortedMnsUsedAtH = MnsUsedAtH.CalculateQuorum(MnsUsedAtH.GetAllMNsCount(), modifier);
        std::move(sortedMnsUsedAtH.begin(), sortedMnsUsedAtH.end(), std::back_inserter(sortedCombinedMns));
    }

    if (LogAcceptCategory(BCLog::LLMQ)) {
        std::stringstream ss;
        ss << " [";
        for (const auto &m: sortedCombinedMns) {
            ss << m->proTxHash.ToString().substr(0, 4) << "|";
        }
        ss << "]";
        LogPrint(BCLog::LLMQ, "GetQuorumQuarterMembersBySnapshot h[%d] from[%d] sortedCombinedMns[%s]\n",
                 pCycleQuorumBaseBlockIndex->nHeight, nHeight, ss.str());
    }

    size_t numQuorums = static_cast<size_t>(llmqParams.signingActiveQuorumCount);
    size_t quorumSize = static_cast<size_t>(llmqParams.size);
    auto quarterSize{quorumSize / 4};

    std::vector<std::vector<CDeterministicMNCPtr>> quarterQuorumMembers(numQuorums);

    if (sortedCombinedMns.empty()) {
        return quarterQuorumMembers;
    }

    switch (snapshot.mnSkipListMode) {
        case SnapshotSkipMode::MODE_NO_SKIPPING:
        {
            auto itm = sortedCombinedMns.begin();
            for (const size_t i : irange::range(numQuorums)) {
                while (quarterQuorumMembers[i].size() < quarterSize) {
                    quarterQuorumMembers[i].push_back(*itm);
                    itm++;
                    if (itm == sortedCombinedMns.end()) {
                        itm = sortedCombinedMns.begin();
                    }
                }
            }
            return quarterQuorumMembers;
        }
        case SnapshotSkipMode::MODE_SKIPPING_ENTRIES: // List holds entries to be skipped
        {
            size_t first_entry_index{0};
            std::vector<int> processesdSkipList;
            for (const auto& s : snapshot.mnSkipList) {
                if (first_entry_index == 0) {
                    first_entry_index = s;
                    processesdSkipList.push_back(s);
                } else {
                    processesdSkipList.push_back(first_entry_index + s);
                }
            }

            int idx = 0;
            auto itsk = processesdSkipList.begin();
            for (const size_t i : irange::range(numQuorums)) {
                while (quarterQuorumMembers[i].size() < quarterSize) {
                    if (itsk != processesdSkipList.end() && idx == *itsk) {
                        itsk++;
                    } else {
                        quarterQuorumMembers[i].push_back(sortedCombinedMns[idx]);
                    }
                    idx++;
                    if (idx == static_cast<int>(sortedCombinedMns.size())) {
                        idx = 0;
                    }
                }
            }
            return quarterQuorumMembers;
        }
        case SnapshotSkipMode::MODE_NO_SKIPPING_ENTRIES: // List holds entries to be kept
        case SnapshotSkipMode::MODE_ALL_SKIPPED: // Every node was skipped. Returning empty quarterQuorumMembers
        default:
            return quarterQuorumMembers;
    }
}

std::pair<CDeterministicMNList, CDeterministicMNList> GetMNUsageBySnapshot(const Consensus::LLMQParams& llmqParams,
                                                                           CDeterministicMNManager& dmnman,
                                                                           const CBlockIndex* pCycleQuorumBaseBlockIndex,
                                                                           const llmq::CQuorumSnapshot& snapshot,
                                                                           int nHeight)
{
    if (!llmqParams.useRotation || pCycleQuorumBaseBlockIndex->nHeight % llmqParams.dkgInterval != 0) {
        ASSERT_IF_DEBUG(false);
        return {};
    }

    CDeterministicMNList usedMNs;
    CDeterministicMNList nonUsedMNs;

    const CBlockIndex* pWorkBlockIndex = pCycleQuorumBaseBlockIndex->GetAncestor(pCycleQuorumBaseBlockIndex->nHeight - 8);
    const auto modifier = GetHashModifier(llmqParams, pCycleQuorumBaseBlockIndex);

    auto allMns = dmnman.GetListForBlock(pWorkBlockIndex);
    auto sortedAllMns = allMns.CalculateQuorum(allMns.GetAllMNsCount(), modifier);

    size_t i{0};
    for (const auto& dmn : sortedAllMns) {
        if (snapshot.activeQuorumMembers[i]) {
            try {
                usedMNs.AddMN(dmn);
            } catch (const std::runtime_error& e) {
            }
        } else {
            if (!dmn->pdmnState->IsBanned()) {
                try {
                    nonUsedMNs.AddMN(dmn);
                } catch (const std::runtime_error& e) {
                }
            }
        }
        i++;
    }

    return std::make_pair(usedMNs, nonUsedMNs);
}

uint256 DeterministicOutboundConnection(const uint256& proTxHash1, const uint256& proTxHash2)
{
    // We need to deterministically select who is going to initiate the connection. The naive way would be to simply
    // return the min(proTxHash1, proTxHash2), but this would create a bias towards MNs with a numerically low
    // hash. To fix this, we return the proTxHash that has the lowest value of:
    //   hash(min(proTxHash1, proTxHash2), max(proTxHash1, proTxHash2), proTxHashX)
    // where proTxHashX is the proTxHash to compare
    uint256 h1;
    uint256 h2;
    if (proTxHash1 < proTxHash2) {
        h1 = ::SerializeHash(std::make_tuple(proTxHash1, proTxHash2, proTxHash1));
        h2 = ::SerializeHash(std::make_tuple(proTxHash1, proTxHash2, proTxHash2));
    } else {
        h1 = ::SerializeHash(std::make_tuple(proTxHash2, proTxHash1, proTxHash1));
        h2 = ::SerializeHash(std::make_tuple(proTxHash2, proTxHash1, proTxHash2));
    }
    if (h1 < h2) {
        return proTxHash1;
    }
    return proTxHash2;
}

std::set<uint256> GetQuorumConnections(const Consensus::LLMQParams& llmqParams, CDeterministicMNManager& dmnman, const CSporkManager& sporkman,
                                       gsl::not_null<const CBlockIndex*> pQuorumBaseBlockIndex, const uint256& forMember, bool onlyOutbound)
{
    if (IsAllMembersConnectedEnabled(llmqParams.type, sporkman)) {
        auto mns = GetAllQuorumMembers(llmqParams.type, dmnman, pQuorumBaseBlockIndex);
        std::set<uint256> result;

        for (const auto& dmn : mns) {
            if (dmn->proTxHash == forMember) {
                continue;
            }
            // Determine which of the two MNs (forMember vs dmn) should initiate the outbound connection and which
            // one should wait for the inbound connection. We do this in a deterministic way, so that even when we
            // end up with both connecting to each other, we know which one to disconnect
            uint256 deterministicOutbound = DeterministicOutboundConnection(forMember, dmn->proTxHash);
            if (!onlyOutbound || deterministicOutbound == dmn->proTxHash) {
                result.emplace(dmn->proTxHash);
            }
        }
        return result;
    }
    return GetQuorumRelayMembers(llmqParams, dmnman, pQuorumBaseBlockIndex, forMember, onlyOutbound);
}

std::set<uint256> GetQuorumRelayMembers(const Consensus::LLMQParams& llmqParams, CDeterministicMNManager& dmnman, gsl::not_null<const CBlockIndex*> pQuorumBaseBlockIndex,
                                        const uint256& forMember, bool onlyOutbound)
{
    auto mns = GetAllQuorumMembers(llmqParams.type, dmnman, pQuorumBaseBlockIndex);
    std::set<uint256> result;

    auto calcOutbound = [&](size_t i, const uint256& proTxHash) {
        if (mns.size() == 1) {
            // No outbound connections are needed when there is one MN only.
            // Also note that trying to calculate results via the algorithm below
            // would result in an endless loop.
            return std::set<uint256>();
        }
        // Relay to nodes at indexes (i+2^k)%n, where
        //   k: 0..max(1, floor(log2(n-1))-1)
        //   n: size of the quorum/ring
        std::set<uint256> r;
        int gap = 1;
        int gap_max = (int)mns.size() - 1;
        int k = 0;
        while ((gap_max >>= 1) || k <= 1) {
            size_t idx = (i + gap) % mns.size();
            // It doesn't matter if this node is going to be added to the resulting set or not,
            // we should always bump the gap and the k (step count) regardless.
            // Refusing to bump the gap results in an incomplete set in the best case scenario
            // (idx won't ever change again once we hit `==`). Not bumping k guarantees an endless
            // loop when the first or the second node we check is the one that should be skipped
            // (k <= 1 forever).
            gap <<= 1;
            k++;
            const auto& otherDmn = mns[idx];
            if (otherDmn->proTxHash == proTxHash) {
                continue;
            }
            r.emplace(otherDmn->proTxHash);
        }
        return r;
    };

    for (const auto i : irange::range(mns.size())) {
        const auto& dmn = mns[i];
        if (dmn->proTxHash == forMember) {
            auto r = calcOutbound(i, dmn->proTxHash);
            result.insert(r.begin(), r.end());
        } else if (!onlyOutbound) {
            auto r = calcOutbound(i, dmn->proTxHash);
            if (r.count(forMember)) {
                result.emplace(dmn->proTxHash);
            }
        }
    }

    return result;
}

std::set<size_t> CalcDeterministicWatchConnections(Consensus::LLMQType llmqType, gsl::not_null<const CBlockIndex*> pQuorumBaseBlockIndex,
                                                   size_t memberCount, size_t connectionCount)
{
    static uint256 qwatchConnectionSeed;
    static std::atomic<bool> qwatchConnectionSeedGenerated{false};
    static RecursiveMutex qwatchConnectionSeedCs;
    if (!qwatchConnectionSeedGenerated) {
        LOCK(qwatchConnectionSeedCs);
        qwatchConnectionSeed = GetRandHash();
        qwatchConnectionSeedGenerated = true;
    }

    std::set<size_t> result;
    uint256 rnd = qwatchConnectionSeed;
    for ([[maybe_unused]] const auto _ : irange::range(connectionCount)) {
        rnd = ::SerializeHash(std::make_pair(rnd, std::make_pair(llmqType, pQuorumBaseBlockIndex->GetBlockHash())));
        result.emplace(rnd.GetUint64(0) % memberCount);
    }
    return result;
}

bool EnsureQuorumConnections(const Consensus::LLMQParams& llmqParams, CConnman& connman, CDeterministicMNManager& dmnman, const CSporkManager& sporkman,
                             const CDeterministicMNList& tip_mn_list, gsl::not_null<const CBlockIndex*> pQuorumBaseBlockIndex,
                             const uint256& myProTxHash, bool is_masternode)
{
    if (!is_masternode && !IsWatchQuorumsEnabled()) {
        return false;
    }

    auto members = GetAllQuorumMembers(llmqParams.type, dmnman, pQuorumBaseBlockIndex);
    if (members.empty()) {
        return false;
    }

    bool isMember = ranges::find_if(members, [&](const auto& dmn) { return dmn->proTxHash == myProTxHash; }) != members.end();

    if (!isMember && !IsWatchQuorumsEnabled()) {
        return false;
    }

    LogPrint(BCLog::NET_NETCONN, "%s -- isMember=%d for quorum %s:\n",
            __func__, isMember, pQuorumBaseBlockIndex->GetBlockHash().ToString());

    std::set<uint256> connections;
    std::set<uint256> relayMembers;
    if (isMember) {
        connections = GetQuorumConnections(llmqParams, dmnman, sporkman, pQuorumBaseBlockIndex, myProTxHash, true);
        relayMembers = GetQuorumRelayMembers(llmqParams, dmnman, pQuorumBaseBlockIndex, myProTxHash, true);
    } else {
        auto cindexes = CalcDeterministicWatchConnections(llmqParams.type, pQuorumBaseBlockIndex, members.size(), 1);
        for (auto idx : cindexes) {
            connections.emplace(members[idx]->proTxHash);
        }
        relayMembers = connections;
    }
    if (!connections.empty()) {
        if (!connman.HasMasternodeQuorumNodes(llmqParams.type, pQuorumBaseBlockIndex->GetBlockHash()) && LogAcceptCategory(BCLog::LLMQ)) {
            std::string debugMsg = strprintf("%s -- adding masternodes quorum connections for quorum %s:\n", __func__, pQuorumBaseBlockIndex->GetBlockHash().ToString());
            for (const auto& c : connections) {
                auto dmn = tip_mn_list.GetValidMN(c);
                if (!dmn) {
                    debugMsg += strprintf("  %s (not in valid MN set anymore)\n", c.ToString());
                } else {
                    debugMsg += strprintf("  %s (%s)\n", c.ToString(), dmn->pdmnState->addr.ToString());
                }
            }
            LogPrint(BCLog::NET_NETCONN, debugMsg.c_str()); /* Continued */
        }
        connman.SetMasternodeQuorumNodes(llmqParams.type, pQuorumBaseBlockIndex->GetBlockHash(), connections);
    }
    if (!relayMembers.empty()) {
        connman.SetMasternodeQuorumRelayMembers(llmqParams.type, pQuorumBaseBlockIndex->GetBlockHash(), relayMembers);
    }
    return true;
}

void AddQuorumProbeConnections(const Consensus::LLMQParams& llmqParams, CConnman& connman, CDeterministicMNManager& dmnman,
                               CMasternodeMetaMan& mn_metaman, const CSporkManager& sporkman, const CDeterministicMNList& tip_mn_list,
                               gsl::not_null<const CBlockIndex*> pQuorumBaseBlockIndex, const uint256 &myProTxHash)
{
    assert(mn_metaman.IsValid());

    if (!IsQuorumPoseEnabled(llmqParams.type, sporkman)) {
        return;
    }

    auto members = GetAllQuorumMembers(llmqParams.type, dmnman, pQuorumBaseBlockIndex);
    auto curTime = GetTime<std::chrono::seconds>().count();

    std::set<uint256> probeConnections;
    for (const auto& dmn : members) {
        if (dmn->proTxHash == myProTxHash) {
            continue;
        }
        auto lastOutbound = mn_metaman.GetMetaInfo(dmn->proTxHash)->GetLastOutboundSuccess();
        if (curTime - lastOutbound < 10 * 60) {
            // avoid re-probing nodes too often
            continue;
        }
        probeConnections.emplace(dmn->proTxHash);
    }

    if (!probeConnections.empty()) {
        if (LogAcceptCategory(BCLog::LLMQ)) {
            std::string debugMsg = strprintf("%s -- adding masternodes probes for quorum %s:\n", __func__, pQuorumBaseBlockIndex->GetBlockHash().ToString());
            for (const auto& c : probeConnections) {
                auto dmn = tip_mn_list.GetValidMN(c);
                if (!dmn) {
                    debugMsg += strprintf("  %s (not in valid MN set anymore)\n", c.ToString());
                } else {
                    debugMsg += strprintf("  %s (%s)\n", c.ToString(), dmn->pdmnState->addr.ToString());
                }
            }
            LogPrint(BCLog::NET_NETCONN, debugMsg.c_str()); /* Continued */
        }
        connman.AddPendingProbeConnections(probeConnections);
    }
}

template <typename CacheType>
void InitQuorumsCache(CacheType& cache, bool limit_by_connections)
{
    for (const auto& llmq : Params().GetConsensus().llmqs) {
        cache.emplace(std::piecewise_construct, std::forward_as_tuple(llmq.type),
                      std::forward_as_tuple(limit_by_connections ? llmq.keepOldConnections : llmq.keepOldKeys));
    }
}
template void InitQuorumsCache<std::map<Consensus::LLMQType, unordered_lru_cache<uint256, bool, StaticSaltedHasher>>>(std::map<Consensus::LLMQType, unordered_lru_cache<uint256, bool, StaticSaltedHasher>>& cache, bool limit_by_connections);
template void InitQuorumsCache<std::map<Consensus::LLMQType, unordered_lru_cache<uint256, std::vector<CQuorumCPtr>, StaticSaltedHasher>>>(std::map<Consensus::LLMQType, unordered_lru_cache<uint256, std::vector<CQuorumCPtr>, StaticSaltedHasher>>& cache, bool limit_by_connections);
template void InitQuorumsCache<std::map<Consensus::LLMQType, unordered_lru_cache<uint256, std::shared_ptr<llmq::CQuorum>, StaticSaltedHasher, 0ul, 0ul>, std::less<Consensus::LLMQType>, std::allocator<std::pair<Consensus::LLMQType const, unordered_lru_cache<uint256, std::shared_ptr<llmq::CQuorum>, StaticSaltedHasher, 0ul, 0ul>>>>>(std::map<Consensus::LLMQType, unordered_lru_cache<uint256, std::shared_ptr<llmq::CQuorum>, StaticSaltedHasher, 0ul, 0ul>, std::less<Consensus::LLMQType>, std::allocator<std::pair<Consensus::LLMQType const, unordered_lru_cache<uint256, std::shared_ptr<llmq::CQuorum>, StaticSaltedHasher, 0ul, 0ul>>>>&cache, bool limit_by_connections);
template void InitQuorumsCache<std::map<Consensus::LLMQType, unordered_lru_cache<uint256, int, StaticSaltedHasher>>>(std::map<Consensus::LLMQType, unordered_lru_cache<uint256, int, StaticSaltedHasher>>& cache, bool limit_by_connections);
template void InitQuorumsCache<std::map<Consensus::LLMQType, unordered_lru_cache<uint256, uint256, StaticSaltedHasher>>>(std::map<Consensus::LLMQType, unordered_lru_cache<uint256, uint256, StaticSaltedHasher>>& cache, bool limit_by_connections);

} // namespace utils

} // namespace llmq
