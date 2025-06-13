// Copyright (c) 2018-2023 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LLMQ_CONTEXT_H
#define BITCOIN_LLMQ_CONTEXT_H

#include <memory>

class CActiveMasternodeManager;
class CBLSWorker;
class CChainState;
class CConnman;
class CDeterministicMNManager;
class CDBWrapper;
class CEvoDB;
class CMasternodeMetaMan;
class CMasternodeSync;
class CMNHFManager;
class CSporkManager;
class CTxMemPool;
class PeerManager;

namespace llmq {
class CChainLocksHandler;
class CDKGDebugManager;
class CDKGSessionManager;
class CEHFSignalsHandler;
class CInstantSendManager;
class CQuorumBlockProcessor;
class CQuorumManager;
class CSigSharesManager;
class CSigningManager;
}

struct LLMQContext {
private:
    const bool is_masternode;

public:
    LLMQContext() = delete;
    LLMQContext(const LLMQContext&) = delete;
    LLMQContext(CChainState& chainstate, CConnman& connman, CDeterministicMNManager& dmnman, CEvoDB& evo_db,
                CMasternodeMetaMan& mn_metaman, CMNHFManager& mnhfman, CSporkManager& sporkman, CTxMemPool& mempool,
                const CActiveMasternodeManager* const mn_activeman, const CMasternodeSync& mn_sync,
                const std::unique_ptr<PeerManager>& peerman, bool unit_tests, bool wipe);
    ~LLMQContext();

    void Interrupt();
    void Start();
    void Stop();

    /** Guaranteed if LLMQContext is initialized then all members are valid too
     *
     *  Please note, that members here should not be re-ordered, because initialization
     *  some of them requires other member initialized.
     *  For example, constructor `qman` requires `bls_worker`.
     *
     *  Some objects are still global variables and their de-globalization is not trivial
     *  at this point. LLMQContext keeps just a pointer to them and doesn't own these objects,
     *  but it still guarantees that objects are created and valid
     */
    const std::shared_ptr<CBLSWorker> bls_worker;
    const std::unique_ptr<llmq::CDKGDebugManager> dkg_debugman;
    const std::unique_ptr<llmq::CQuorumBlockProcessor> quorum_block_processor;
    const std::unique_ptr<llmq::CDKGSessionManager> qdkgsman;
    const std::unique_ptr<llmq::CQuorumManager> qman;
    const std::unique_ptr<llmq::CSigningManager> sigman;
    const std::unique_ptr<llmq::CSigSharesManager> shareman;
    llmq::CChainLocksHandler* const clhandler;
    llmq::CInstantSendManager* const isman;
    const std::unique_ptr<llmq::CEHFSignalsHandler> ehfSignalsHandler;
};

#endif // BITCOIN_LLMQ_CONTEXT_H
