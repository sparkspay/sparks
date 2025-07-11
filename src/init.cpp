// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Copyright (c) 2014-2024 The Dash Core developers
// Copyright (c) 2016-2025 The Sparks Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <init.h>

#include <addrman.h>
#include <amount.h>
#include <banman.h>
#include <base58.h>
#include <blockfilter.h>
#include <chain.h>
#include <chainparams.h>
#include <context.h>
#include <deploymentstatus.h>
#include <node/coinstats.h>
#include <fs.h>
#include <hash.h>
#include <httpserver.h>
#include <httprpc.h>
#include <interfaces/chain.h>
#include <index/blockfilterindex.h>
#include <index/coinstatsindex.h>
#include <index/txindex.h>
#include <interfaces/node.h>
#include <key.h>
#include <mapport.h>
#include <miner.h>
#include <net.h>
#include <net_permissions.h>
#include <net_processing.h>
#include <netbase.h>
#include <node/blockstorage.h>
#include <node/context.h>
#include <node/ui_interface.h>
#include <policy/feerate.h>
#include <policy/fees.h>
#include <policy/policy.h>
#include <policy/settings.h>
#include <rpc/blockchain.h>
#include <rpc/register.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <scheduler.h>
#include <script/sigcache.h>
#include <script/standard.h>
#include <shutdown.h>
#include <sync.h>
#include <timedata.h>
#include <torcontrol.h>
#include <txdb.h>
#include <txmempool.h>
#include <util/asmap.h>
#include <util/error.h>
#include <util/moneystr.h>
#include <util/strencodings.h>
#include <util/string.h>
#include <util/system.h>
#include <util/thread.h>
#include <util/threadnames.h>
#include <util/translation.h>
#include <validation.h>

#include <validationinterface.h>

#include <masternode/node.h>
#include <coinjoin/coinjoin.h>
#include <coinjoin/context.h>
#ifdef ENABLE_WALLET
#include <coinjoin/client.h>
#include <coinjoin/options.h>
#endif // ENABLE_WALLET
#include <coinjoin/server.h>
#include <dsnotificationinterface.h>
#include <flat-database.h>
#include <governance/governance.h>
#include <masternode/meta.h>
#include <masternode/sync.h>
#include <masternode/utils.h>
#include <messagesigner.h>
#include <netfulfilledman.h>
#include <spork.h>
#include <walletinitinterface.h>

#include <evo/chainhelper.h>
#include <evo/creditpool.h>
#include <evo/deterministicmns.h>
#include <evo/mnhftx.h>
#include <llmq/blockprocessor.h>
#include <llmq/chainlocks.h>
#include <llmq/context.h>
#include <llmq/instantsend.h>
#include <llmq/quorums.h>
#include <llmq/dkgsessionmgr.h>
#include <llmq/options.h>
#include <llmq/signing.h>
#include <llmq/snapshot.h>
#include <llmq/signing_shares.h>

#include <statsd_client.h>

#include <functional>
#include <set>
#include <stdint.h>
#include <stdio.h>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <bls/bls.h>

#ifndef WIN32
#include <attributes.h>
#include <cerrno>
#include <signal.h>
#include <sys/stat.h>
#endif

#include <boost/signals2/signal.hpp>

#if ENABLE_ZMQ
#include <zmq/zmqabstractnotifier.h>
#include <zmq/zmqnotificationinterface.h>
#include <zmq/zmqrpc.h>
#endif

static constexpr bool DEFAULT_PROXYRANDOMIZE{true};
static constexpr bool DEFAULT_REST_ENABLE{false};
static constexpr bool DEFAULT_I2P_ACCEPT_INCOMING{true};

static CDSNotificationInterface* pdsNotificationInterface = nullptr;

#ifdef WIN32
// Win32 LevelDB doesn't use filedescriptors, and the ones used for
// accessing block files don't count towards the fd_set size limit
// anyway.
#define MIN_CORE_FILEDESCRIPTORS 0
#else
#define MIN_CORE_FILEDESCRIPTORS 150
#endif

static const char* DEFAULT_ASMAP_FILENAME="ip_asn.map";
/**
 * The PID file facilities.
 */
static const char* BITCOIN_PID_FILENAME = "sparksd.pid";

static fs::path GetPidFile(const ArgsManager& args)
{
    return AbsPathForConfigVal(fs::path(args.GetArg("-pid", BITCOIN_PID_FILENAME)));
}

[[nodiscard]] static bool CreatePidFile(const ArgsManager& args)
{
    fsbridge::ofstream file{GetPidFile(args)};
    if (file) {
#ifdef WIN32
        tfm::format(file, "%d\n", GetCurrentProcessId());
#else
        tfm::format(file, "%d\n", getpid());
#endif
        return true;
    } else {
        return InitError(strprintf(_("Unable to create the PID file '%s': %s"), GetPidFile(args).string(), std::strerror(errno)));
    }
}

//////////////////////////////////////////////////////////////////////////////
//
// Shutdown
//

//
// Thread management and startup/shutdown:
//
// The network-processing threads are all part of a thread group
// created by AppInit() or the Qt main() function.
//
// A clean exit happens when StartShutdown() or the SIGTERM
// signal handler sets ShutdownRequested(), which makes main thread's
// WaitForShutdown() interrupts the thread group.
// And then, WaitForShutdown() makes all other on-going threads
// in the thread group join the main thread.
// Shutdown() is then called to clean up database connections, and stop other
// threads that should only be stopped after the main network-processing
// threads have exited.
//
// Shutdown for Qt is very similar, only it uses a QTimer to detect
// ShutdownRequested() getting set, and then does the normal Qt
// shutdown thing.
//

void Interrupt(NodeContext& node)
{
    InterruptHTTPServer();
    InterruptHTTPRPC();
    InterruptRPC();
    InterruptREST();
    InterruptTorControl();
    if (node.llmq_ctx) {
        node.llmq_ctx->Interrupt();
    }
    InterruptMapPort();
    if (node.connman)
        node.connman->Interrupt();
    if (g_txindex) {
        g_txindex->Interrupt();
    }
    ForEachBlockFilterIndex([](BlockFilterIndex& index) { index.Interrupt(); });
    if (g_coin_stats_index) {
        g_coin_stats_index->Interrupt();
    }
}

/** Preparing steps before shutting down or restarting the wallet */
void PrepareShutdown(NodeContext& node)
{
    static Mutex g_shutdown_mutex;
    TRY_LOCK(g_shutdown_mutex, lock_shutdown);
    if (!lock_shutdown) return;
    LogPrintf("%s: In progress...\n", __func__);
    Assert(node.args);

    /// Note: Shutdown() must be able to handle cases in which initialization failed part of the way,
    /// for example if the data directory was found to be locked.
    /// Be sure that anything that writes files or flushes caches only does this if the respective
    /// module was initialized.
    util::ThreadRename("shutoff");
    if (node.mempool) node.mempool->AddTransactionsUpdated(1);

    StopHTTPRPC();
    StopREST();
    StopRPC();
    StopHTTPServer();
    if (node.llmq_ctx) node.llmq_ctx->Stop();

    for (const auto& client : node.chain_clients) {
        client->flush();
    }
    StopMapPort();

    // Because these depend on each-other, we make sure that neither can be
    // using the other before destroying them.
    if (node.peerman) UnregisterValidationInterface(node.peerman.get());
    // Follow the lock order requirements:
    // * CheckForStaleTipAndEvictPeers locks cs_main before indirectly calling GetExtraFullOutboundCount
    //   which locks cs_vNodes.
    // * ProcessMessage locks cs_main and g_cs_orphans before indirectly calling ForEachNode which
    //   locks cs_vNodes.
    // * CConnman::Stop calls DeleteNode, which calls FinalizeNode, which locks cs_main and calls
    //   EraseOrphansFor, which locks g_cs_orphans.
    //
    // Thus the implicit locking order requirement is: (1) cs_main, (2) g_cs_orphans, (3) cs_vNodes.
    if (node.connman) {
        node.connman->StopThreads();
        LOCK2(::cs_main, ::g_cs_orphans);
        node.connman->StopNodes();
    }

    StopTorControl();

    // After everything has been shut down, but before things get flushed, stop the
    // CScheduler/checkqueue, threadGroup and load block thread.
    if (node.scheduler) node.scheduler->stop();
    if (node.chainman && node.chainman->m_load_block.joinable()) node.chainman->m_load_block.join();
    StopScriptCheckWorkerThreads();

    // After there are no more peers/RPC left to give us new data which may generate
    // CValidationInterface callbacks, flush them...
    GetMainSignals().FlushBackgroundCallbacks();

    // After the threads that potentially access these pointers have been stopped,
    // destruct and reset all to nullptr.
    node.peerman.reset();
    node.connman.reset();
    node.banman.reset();
    node.addrman.reset();

    if (node.mempool && node.mempool->IsLoaded() && node.args->GetBoolArg("-persistmempool", DEFAULT_PERSIST_MEMPOOL)) {
        DumpMempool(*node.mempool);
    }

    // Drop transactions we were still watching, and record fee estimations.
    if (node.fee_estimator) node.fee_estimator->Flush();

    // FlushStateToDisk generates a ChainStateFlushed callback, which we should avoid missing
    if (node.chainman) {
        LOCK(cs_main);
        for (CChainState* chainstate : node.chainman->GetAll()) {
            if (chainstate->CanFlushToDisk()) {
                chainstate->ForceFlushStateToDisk();
            }
        }
    }

    // After there are no more peers/RPC left to give us new data which may generate
    // CValidationInterface callbacks, flush them...
    GetMainSignals().FlushBackgroundCallbacks();

    // After all scheduled tasks have been flushed, destroy pointers
    // and reset all to nullptr.
    node.mn_sync.reset();
    node.sporkman.reset();
    node.govman.reset();
    node.netfulfilledman.reset();
    node.mn_metaman.reset();

    // Stop and delete all indexes only after flushing background callbacks.
    if (g_txindex) {
        g_txindex->Stop();
        g_txindex.reset();
    }
    if (g_coin_stats_index) {
        g_coin_stats_index->Stop();
        g_coin_stats_index.reset();
    }
    ForEachBlockFilterIndex([](BlockFilterIndex& index) { index.Stop(); });
    DestroyAllBlockFilterIndexes();

    // Any future callbacks will be dropped. This should absolutely be safe - if
    // missing a callback results in an unrecoverable situation, unclean shutdown
    // would too. The only reason to do the above flushes is to let the wallet catch
    // up with our current chain to avoid any strange pruning edge cases and make
    // next startup faster by avoiding rescan.

    if (node.chainman) {
        LOCK(cs_main);
        for (CChainState* chainstate : node.chainman->GetAll()) {
            if (chainstate->CanFlushToDisk()) {
                chainstate->ForceFlushStateToDisk();
                chainstate->ResetCoinsViews();
            }
        }
        pblocktree.reset();
        node.chain_helper.reset();
        if (node.mnhf_manager) {
            node.mnhf_manager->DisconnectManagers();
        }
        node.llmq_ctx.reset();
        llmq::quorumSnapshotManager.reset();
        node.mempool->DisconnectManagers();
        node.dmnman.reset();
        node.cpoolman.reset();
        node.mnhf_manager.reset();
        node.evodb.reset();
    }
    for (const auto& client : node.chain_clients) {
        client->stop();
    }

#if ENABLE_ZMQ
    if (g_zmq_notification_interface) {
        UnregisterValidationInterface(g_zmq_notification_interface);
        delete g_zmq_notification_interface;
        g_zmq_notification_interface = nullptr;
    }
#endif

    if (pdsNotificationInterface) {
        UnregisterValidationInterface(pdsNotificationInterface);
        delete pdsNotificationInterface;
        pdsNotificationInterface = nullptr;
    }
    if (node.mn_activeman) {
        UnregisterValidationInterface(node.mn_activeman.get());
        node.mn_activeman.reset();
    }

    node.chain_clients.clear();

    // After all wallets are removed, destroy all CoinJoin objects
    // and reset them to nullptr
    node.cj_ctx.reset();

    UnregisterAllValidationInterfaces();
    GetMainSignals().UnregisterBackgroundSignalScheduler();
}

/**
* Shutdown is split into 2 parts:
* Part 1: shut down everything but the main wallet instance (done in PrepareShutdown() )
* Part 2: delete wallet instance
*
* In case of a restart PrepareShutdown() was already called before, but this method here gets
* called implicitly when the parent object is deleted. In this case we have to skip the
* PrepareShutdown() part because it was already executed and just delete the wallet instance.
*/
void Shutdown(NodeContext& node)
{
    // Shutdown part 1: prepare shutdown
    if(!RestartRequested()) {
        PrepareShutdown(node);
    }
    // Shutdown part 2: delete wallet instance
    ECC_Stop();
    node.mempool.reset();
    node.fee_estimator.reset();
    node.chainman.reset();
    node.scheduler.reset();

    try {
        if (!fs::remove(GetPidFile(*node.args))) {
            LogPrintf("%s: Unable to remove PID file: File does not exist\n", __func__);
        }
    } catch (const fs::filesystem_error& e) {
        LogPrintf("%s: Unable to remove PID file: %s\n", __func__, fsbridge::get_filesystem_error_message(e));
    }

    node.args = nullptr;
    LogPrintf("%s: done\n", __func__);
}

/**
 * Signal handlers are very limited in what they are allowed to do.
 * The execution context the handler is invoked in is not guaranteed,
 * so we restrict handler operations to just touching variables:
 */
#ifndef WIN32
static void HandleSIGTERM(int)
{
    StartShutdown();
}

static void HandleSIGHUP(int)
{
    LogInstance().m_reopen_file = true;
}
#else
static BOOL WINAPI consoleCtrlHandler(DWORD dwCtrlType)
{
    StartShutdown();
    Sleep(INFINITE);
    return true;
}
#endif

#ifndef WIN32
static void registerSignalHandler(int signal, void(*handler)(int))
{
    struct sigaction sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(signal, &sa, nullptr);
}
#endif

static boost::signals2::connection rpc_notify_block_change_connection;
static void OnRPCStarted()
{
    rpc_notify_block_change_connection = uiInterface.NotifyBlockTip_connect(std::bind(RPCNotifyBlockChange, std::placeholders::_2));
}

static void OnRPCStopped()
{
    rpc_notify_block_change_connection.disconnect();
    RPCNotifyBlockChange(nullptr);
    g_best_block_cv.notify_all();
    LogPrint(BCLog::RPC, "RPC stopped.\n");
}

std::string GetSupportedSocketEventsStr()
{
    std::string strSupportedModes = "'select'";
#ifdef USE_POLL
    strSupportedModes += ", 'poll'";
#endif
#ifdef USE_EPOLL
    strSupportedModes += ", 'epoll'";
#endif
#ifdef USE_KQUEUE
    strSupportedModes += ", 'kqueue'";
#endif
    return strSupportedModes;
}

void SetupServerArgs(NodeContext& node)
{
    assert(!node.args);
    node.args = &gArgs;
    ArgsManager& argsman = *node.args;

    SetupHelpOptions(argsman);
    argsman.AddArg("-help-debug", "Print help message with debugging options and exit", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);

    const auto defaultBaseParams = CreateBaseChainParams(CBaseChainParams::MAIN);
    const auto testnetBaseParams = CreateBaseChainParams(CBaseChainParams::TESTNET);
    const auto regtestBaseParams = CreateBaseChainParams(CBaseChainParams::REGTEST);
    const auto defaultChainParams = CreateChainParams(argsman, CBaseChainParams::MAIN);
    const auto testnetChainParams = CreateChainParams(argsman, CBaseChainParams::TESTNET);
    const auto regtestChainParams = CreateChainParams(argsman, CBaseChainParams::REGTEST);

    // Hidden Options
    std::vector<std::string> hidden_args = {"-dbcrashratio", "-forcecompactdb", "-printcrashinfo",
        // GUI args. These will be overwritten by SetupUIArgs for the GUI
        "-choosedatadir", "-lang=<lang>", "-min", "-resetguisettings", "-splash", "-uiplatform"};


    // Set all of the args and their help
    // When adding new options to the categories, please keep and ensure alphabetical ordering.
#if HAVE_SYSTEM
    argsman.AddArg("-alertnotify=<cmd>", "Execute command when an alert is raised (%s in cmd is replaced by message)", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#endif
    argsman.AddArg("-assumevalid=<hex>", strprintf("If this block is in the chain assume that it and its ancestors are valid and potentially skip their script verification (0 to verify all, default: %s, testnet: %s)", defaultChainParams->GetConsensus().defaultAssumeValid.GetHex(), testnetChainParams->GetConsensus().defaultAssumeValid.GetHex()), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-blocksdir=<dir>", "Specify directory to hold blocks subdirectory for *.dat files (default: <datadir>)", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-fastprune", "Use smaller block files and lower minimum prune height for testing purposes", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
#if HAVE_SYSTEM
    argsman.AddArg("-blocknotify=<cmd>", "Execute command when the best block changes (%s in cmd is replaced by block hash)", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#endif
    argsman.AddArg("-blockreconstructionextratxn=<n>", strprintf("Extra transactions to keep in memory for compact block reconstructions (default: %u)", DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-blocksonly", strprintf("Whether to reject transactions from network peers. Automatic broadcast and rebroadcast of any transactions from inbound peers is disabled, unless the peer has the 'forcerelay' permission. RPC transactions are not affected. (default: %u)", DEFAULT_BLOCKSONLY), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#if HAVE_SYSTEM
    argsman.AddArg("-chainlocknotify=<cmd>", "Execute command when the best chainlock changes (%s in cmd is replaced by chainlocked block hash)", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#endif
    argsman.AddArg("-coinstatsindex", strprintf("Maintain coinstats index used by the gettxoutsetinfo RPC (default: %u)", DEFAULT_COINSTATSINDEX), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-conf=<file>", strprintf("Specify path to read-only configuration file. Relative paths will be prefixed by datadir location. (default: %s)", BITCOIN_CONF_FILENAME), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-datadir=<dir>", "Specify data directory", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-dbbatchsize", strprintf("Maximum database write batch size in bytes (default: %u)", nDefaultDbBatchSize), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::OPTIONS);
    argsman.AddArg("-dbcache=<n>", strprintf("Maximum database cache size <n> MiB (%d to %d, default: %d). In addition, unused mempool memory is shared for this cache (see -maxmempool).", nMinDbCache, nMaxDbCache, nDefaultDbCache), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-debuglogfile=<file>", strprintf("Specify location of debug log file. Relative paths will be prefixed by a net-specific datadir location. (-nodebuglogfile to disable; default: %s)", DEFAULT_DEBUGLOGFILE), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-includeconf=<file>", "Specify additional configuration file, relative to the -datadir path (only useable from configuration file, not command line)", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-loadblock=<file>", "Imports blocks from external file on startup", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-maxmempool=<n>", strprintf("Keep the transaction memory pool below <n> megabytes (default: %u)", DEFAULT_MAX_MEMPOOL_SIZE), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-maxorphantxsize=<n>", strprintf("Maximum total size of all orphan transactions in megabytes (default: %u)", DEFAULT_MAX_ORPHAN_TRANSACTIONS_SIZE), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-maxrecsigsage=<n>", strprintf("Number of seconds to keep LLMQ recovery sigs (default: %u)", llmq::DEFAULT_MAX_RECOVERED_SIGS_AGE), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-mempoolexpiry=<n>", strprintf("Do not keep transactions in the mempool longer than <n> hours (default: %u)", DEFAULT_MEMPOOL_EXPIRY), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-minimumchainwork=<hex>", strprintf("Minimum work assumed to exist on a valid chain in hex (default: %s, testnet: %s)", defaultChainParams->GetConsensus().nMinimumChainWork.GetHex(), testnetChainParams->GetConsensus().nMinimumChainWork.GetHex()), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::OPTIONS);
    argsman.AddArg("-par=<n>", strprintf("Set the number of script verification threads (%u to %d, 0 = auto, <0 = leave that many cores free, default: %d)",
        -GetNumCores(), MAX_SCRIPTCHECK_THREADS, DEFAULT_SCRIPTCHECK_THREADS), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-persistmempool", strprintf("Whether to save the mempool on shutdown and load on restart (default: %u)", DEFAULT_PERSIST_MEMPOOL), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-pid=<file>", strprintf("Specify pid file. Relative paths will be prefixed by a net-specific datadir location. (default: %s)", BITCOIN_PID_FILENAME), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-prune=<n>", strprintf("Reduce storage requirements by enabling pruning (deleting) of old blocks. This allows the pruneblockchain RPC to be called to delete specific blocks, and enables automatic pruning of old blocks if a target size in MiB is provided. This mode is incompatible with -txindex, -coinstatsindex, -rescan and -disablegovernance=false. "
            "Warning: Reverting this setting requires re-downloading the entire blockchain. "
            "(default: 0 = disable pruning blocks, 1 = allow manual pruning via RPC, >%u = automatically prune block files to stay under the specified target size in MiB)", MIN_DISK_SPACE_FOR_BLOCK_FILES / 1024 / 1024), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-settings=<file>", strprintf("Specify path to dynamic settings data file. Can be disabled with -nosettings. File is written at runtime and not meant to be edited by users (use %s instead for custom settings). Relative paths will be prefixed by datadir location. (default: %s)", BITCOIN_CONF_FILENAME, BITCOIN_SETTINGS_FILENAME), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-syncmempool", strprintf("Sync mempool from other nodes on start (default: %u)", DEFAULT_SYNC_MEMPOOL), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#if HAVE_SYSTEM
    argsman.AddArg("-startupnotify=<cmd>", "Execute command on startup.", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#endif
#ifndef WIN32
    argsman.AddArg("-sysperms", "Create new files with system default permissions, instead of umask 077 (only effective with disabled wallet functionality)", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#else
    hidden_args.emplace_back("-sysperms");
#endif
    argsman.AddArg("-version", "Print version and exit", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);

    argsman.AddArg("-addressindex", strprintf("Maintain a full address index, used to query for the balance, txids and unspent outputs for addresses (default: %u)", DEFAULT_ADDRESSINDEX), ArgsManager::ALLOW_ANY, OptionsCategory::INDEXING);
    argsman.AddArg("-reindex", "Rebuild chain state and block index from the blk*.dat files on disk", ArgsManager::ALLOW_ANY, OptionsCategory::INDEXING);
    argsman.AddArg("-reindex-chainstate", "Rebuild chain state from the currently indexed blocks. When in pruning mode or if blocks on disk might be corrupted, use full -reindex instead.", ArgsManager::ALLOW_ANY, OptionsCategory::INDEXING);
    argsman.AddArg("-spentindex", strprintf("Maintain a full spent index, used to query the spending txid and input index for an outpoint (default: %u)", DEFAULT_SPENTINDEX), ArgsManager::ALLOW_ANY, OptionsCategory::INDEXING);
    argsman.AddArg("-timestampindex", strprintf("Maintain a timestamp index for block hashes, used to query blocks hashes by a range of timestamps (default: %u)", DEFAULT_TIMESTAMPINDEX), ArgsManager::ALLOW_ANY, OptionsCategory::INDEXING);
    argsman.AddArg("-txindex", strprintf("Maintain a full transaction index, used by the getrawtransaction rpc call (default: %u)", DEFAULT_TXINDEX), ArgsManager::ALLOW_ANY, OptionsCategory::INDEXING);
    argsman.AddArg("-blockfilterindex=<type>",
                 strprintf("Maintain an index of compact filters by block (default: %s, values: %s).", DEFAULT_BLOCKFILTERINDEX, ListBlockFilterTypes()) +
                 " If <type> is not supplied or if <type> = 1, indexes for all known types are enabled.",
                 ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);

    argsman.AddArg("-asmap=<file>", strprintf("Specify asn mapping used for bucketing of the peers (default: %s). Relative paths will be prefixed by the net-specific datadir location.", DEFAULT_ASMAP_FILENAME), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-addnode=<ip>", strprintf("Add a node to connect to and attempt to keep the connection open (see the addnode RPC help for more info). This option can be specified multiple times to add multiple nodes; connections are limited to %u at a time and are counted separately from the -maxconnections limit.", MAX_ADDNODE_CONNECTIONS), ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::CONNECTION);
    argsman.AddArg("-allowprivatenet", strprintf("Allow RFC1918 addresses to be relayed and connected to (default: %u)", DEFAULT_ALLOWPRIVATENET), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-bantime=<n>", strprintf("Default duration (in seconds) of manually configured bans (default: %u)", DEFAULT_MISBEHAVING_BANTIME), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-bind=<addr>[:<port>][=onion]", strprintf("Bind to given address and always listen on it (default: 0.0.0.0). Use [host]:port notation for IPv6. Append =onion to tag any incoming connections to that address and port as incoming Tor connections (default: 127.0.0.1:%u=onion, testnet: 127.0.0.1:%u=onion, regtest: 127.0.0.1:%u=onion)", defaultBaseParams->OnionServiceTargetPort(), testnetBaseParams->OnionServiceTargetPort(), regtestBaseParams->OnionServiceTargetPort()), ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::CONNECTION);
    argsman.AddArg("-cjdnsreachable", "If set, then this host is configured for CJDNS (connecting to fc00::/8 addresses would lead us to the CJDNS network, see doc/cjdns.md) (default: 0)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-connect=<ip>", "Connect only to the specified node; -noconnect disables automatic connections (the rules for this peer are the same as for -addnode). This option can be specified multiple times to connect to multiple nodes.", ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::CONNECTION);
    argsman.AddArg("-discover", "Discover own IP addresses (default: 1 when listening and no -externalip or -proxy)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-dns", strprintf("Allow DNS lookups for -addnode, -seednode and -connect (default: %u)", DEFAULT_NAME_LOOKUP), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-dnsseed", strprintf("Query for peer addresses via DNS lookup, if low on addresses (default: %u unless -connect used)", DEFAULT_DNSSEED), ArgsManager::ALLOW_BOOL, OptionsCategory::CONNECTION);
    argsman.AddArg("-externalip=<ip>", "Specify your own public address", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-fixedseeds", strprintf("Allow fixed seeds if DNS seeds don't provide peers (default: %u)", DEFAULT_FIXEDSEEDS), ArgsManager::ALLOW_BOOL, OptionsCategory::CONNECTION);
    argsman.AddArg("-forcednsseed", strprintf("Always query for peer addresses via DNS lookup (default: %u)", DEFAULT_FORCEDNSSEED), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-listen", "Accept connections from outside (default: 1 if no -proxy or -connect)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-listenonion", strprintf("Automatically create Tor onion service (default: %d)", DEFAULT_LISTEN_ONION), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-maxconnections=<n>", strprintf("Maintain at most <n> connections to peers (temporary service connections excluded) (default: %u). This limit does not apply to connections manually added via -addnode or the addnode RPC, which have a separate limit of %u.", DEFAULT_MAX_PEER_CONNECTIONS, MAX_ADDNODE_CONNECTIONS), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-maxreceivebuffer=<n>", strprintf("Maximum per-connection receive buffer, <n>*1000 bytes (default: %u)", DEFAULT_MAXRECEIVEBUFFER), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-maxsendbuffer=<n>", strprintf("Maximum per-connection send buffer, <n>*1000 bytes (default: %u)", DEFAULT_MAXSENDBUFFER), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-maxtimeadjustment", strprintf("Maximum allowed median peer time offset adjustment. Local perspective of time may be influenced by peers forward or backward by this amount. (default: %u seconds)", DEFAULT_MAX_TIME_ADJUSTMENT), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-maxuploadtarget=<n>", strprintf("Tries to keep outbound traffic under the given target (in MiB per 24h). Limit does not apply to peers with 'download' permission. 0 = no limit (default: %d)", DEFAULT_MAX_UPLOAD_TARGET), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-onion=<ip:port>", "Use separate SOCKS5 proxy to reach peers via Tor onion services, set -noonion to disable (default: -proxy)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-i2psam=<ip:port>", "I2P SAM proxy to reach I2P peers and accept I2P connections (default: none)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-i2pacceptincoming", strprintf("Whether to accept inbound I2P connections (default: %i). Ignored if -i2psam is not set. Listening for inbound I2P connections is done through the SAM proxy, not by binding to a local address and port.", DEFAULT_I2P_ACCEPT_INCOMING), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-onlynet=<net>", "Make automatic outbound connections only to network <net> (" + Join(GetNetworkNames(), ", ") + "). Inbound and manual connections are not affected by this option. It can be specified multiple times to allow multiple networks.", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-peerblockfilters", strprintf("Serve compact block filters to peers per BIP 157 (default: %u)", DEFAULT_PEERBLOCKFILTERS), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-peerbloomfilters", strprintf("Support filtering of blocks and transaction with bloom filters (default: %u)", DEFAULT_PEERBLOOMFILTERS), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-peertimeout=<n>", strprintf("Specify a p2p connection timeout delay in seconds. After connecting to a peer, wait this amount of time before considering disconnection based on inactivity (minimum: 1, default: %d)", DEFAULT_PEER_CONNECT_TIMEOUT), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-permitbaremultisig", strprintf("Relay non-P2SH multisig (default: %u)", DEFAULT_PERMIT_BAREMULTISIG), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-port=<port>", strprintf("Listen for connections on <port>. Nodes not using the default ports (default: %u, testnet: %u, regtest: %u) are unlikely to get incoming connections. Not relevant for I2P (see doc/i2p.md).", defaultChainParams->GetDefaultPort(), testnetChainParams->GetDefaultPort(), regtestChainParams->GetDefaultPort()), ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::CONNECTION);
    argsman.AddArg("-proxy=<ip:port>", "Connect through SOCKS5 proxy, set -noproxy to disable (default: disabled)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-proxyrandomize", strprintf("Randomize credentials for every proxy connection. This enables Tor stream isolation (default: %u)", DEFAULT_PROXYRANDOMIZE), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-seednode=<ip>", "Connect to a node to retrieve peer addresses, and disconnect. This option can be specified multiple times to connect to multiple nodes.", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-socketevents=<mode>", "Socket events mode, which must be one of 'select', 'poll', 'epoll' or 'kqueue', depending on your system (default: Linux - 'epoll', FreeBSD/Apple - 'kqueue', Windows - 'select')", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-networkactive", "Enable all P2P network activity (default: 1). Can be changed by the setnetworkactive RPC command", ArgsManager::ALLOW_BOOL, OptionsCategory::CONNECTION);
    argsman.AddArg("-timeout=<n>", strprintf("Specify socket connection timeout in milliseconds. If an initial attempt to connect is unsuccessful after this amount of time, drop it (minimum: 1, default: %d)", DEFAULT_CONNECT_TIMEOUT), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-torcontrol=<ip>:<port>", strprintf("Tor control port to use if onion listening enabled (default: %s)", DEFAULT_TOR_CONTROL), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    argsman.AddArg("-torpassword=<pass>", "Tor control port password (default: empty)", ArgsManager::ALLOW_ANY | ArgsManager::SENSITIVE, OptionsCategory::CONNECTION);
#ifdef USE_UPNP
#if USE_UPNP
    argsman.AddArg("-upnp", "Use UPnP to map the listening port (default: 1 when listening and no -proxy)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
#else
    argsman.AddArg("-upnp", strprintf("Use UPnP to map the listening port (default: %u)", 0), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
#endif
#else
    hidden_args.emplace_back("-upnp");
#endif
#ifdef USE_NATPMP
    argsman.AddArg("-natpmp", strprintf("Use NAT-PMP to map the listening port (default: %s)", DEFAULT_NATPMP ? "1 when listening and no -proxy" : "0"), ArgsManager::ALLOW_BOOL, OptionsCategory::CONNECTION);
#else
    hidden_args.emplace_back("-natpmp");
#endif // USE_NATPMP
    argsman.AddArg("-whitebind=<[permissions@]addr>", "Bind to the given address and add permission flags to the peers connecting to it. "
        "Use [host]:port notation for IPv6. Allowed permissions: " + Join(NET_PERMISSIONS_DOC, ", ") + ". "
        "Specify multiple permissions separated by commas (default: download,noban,mempool,relay). Can be specified multiple times.", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);

    argsman.AddArg("-whitelist=<[permissions@]IP address or network>", "Add permission flags to the peers connecting from the given IP address (e.g. 1.2.3.4) or "
        "CIDR-notated network (e.g. 1.2.3.0/24). Uses the same permissions as "
        "-whitebind. Can be specified multiple times." , ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);

    g_wallet_init_interface.AddWalletOptions(argsman);

#if ENABLE_ZMQ
    argsman.AddArg("-zmqpubhashblock=<address>", "Enable publish hash block in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubhashchainlock=<address>", "Enable publish hash block (locked via ChainLocks) in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubhashgovernanceobject=<address>", "Enable publish hash of governance objects (like proposals) in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubhashgovernancevote=<address>", "Enable publish hash of governance votes in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubhashinstantsenddoublespend=<address>", "Enable publish transaction hashes of attempted InstantSend double spend in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubhashrecoveredsig=<address>", "Enable publish message hash of recovered signatures (recovered by LLMQs) in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubhashtx=<address>", "Enable publish hash transaction in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubhashtxlock=<address>", "Enable publish hash transaction (locked via InstantSend) in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawblock=<address>", "Enable publish raw block in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawchainlock=<address>", "Enable publish raw block (locked via ChainLocks) in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawchainlocksig=<address>", "Enable publish raw block (locked via ChainLocks) and CLSIG message in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawgovernancevote=<address>", "Enable publish raw governance objects (like proposals) in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawgovernanceobject=<address>", "Enable publish raw governance votes in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawinstantsenddoublespend=<address>", "Enable publish raw transactions of attempted InstantSend double spend in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawrecoveredsig=<address>", "Enable publish raw recovered signatures (recovered by LLMQs) in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawtx=<address>", "Enable publish raw transaction in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawtxlock=<address>", "Enable publish raw transaction (locked via InstantSend) in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawtxlocksig=<address>", "Enable publish raw transaction (locked via InstantSend) and ISLOCK in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubhashblockhwm=<n>", strprintf("Set publish hash block outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubhashchainlockhwm=<n>", strprintf("Set publish hash chain lock outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubhashgovernanceobjecthwm=<n>", strprintf("Set publish hash governance object outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubhashgovernancevotehwm=<n>", strprintf("Set publish hash governance vote outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubhashinstantsenddoublespendhwm=<n>", strprintf("Set publish hash InstantSend double spend outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubhashrecoveredsighwm=<n>", strprintf("Set publish hash recovered signature outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubhashtxhwm=<n>", strprintf("Set publish hash transaction outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubhashtxlockhwm=<n>", strprintf("Set publish hash transaction lock outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawblockhwm=<n>", strprintf("Set publish raw block outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawchainlockhwm=<n>", strprintf("Set publish raw chain lock outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawchainlocksighwm=<n>", strprintf("Set publish raw chain lock signature outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawgovernanceobjecthwm=<n>", strprintf("Set publish raw governance object outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawgovernancevotehwm=<n>", strprintf("Set publish raw governance vote outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawinstantsenddoublespendhwm=<n>", strprintf("Set publish raw InstantSend double spend outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawrecoveredsighwm=<n>", strprintf("Set publish raw recovered signature outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawtxhwm=<n>", strprintf("Set publish raw transaction outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawtxlockhwm=<n>", strprintf("Set publish raw transaction lock outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    argsman.AddArg("-zmqpubrawtxlocksighwm=<n>", strprintf("Set publish raw transaction lock signature outbound message high water mark (default: %d)", CZMQAbstractNotifier::DEFAULT_ZMQ_SNDHWM), ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
#else
    hidden_args.emplace_back("-zmqpubhashblock=<address>");
    hidden_args.emplace_back("-zmqpubhashchainlock=<address>");
    hidden_args.emplace_back("-zmqpubhashgovernanceobject=<address>");
    hidden_args.emplace_back("-zmqpubhashgovernancevote=<address>");
    hidden_args.emplace_back("-zmqpubhashinstantsenddoublespend=<address>");
    hidden_args.emplace_back("-zmqpubhashrecoveredsig=<address>");
    hidden_args.emplace_back("-zmqpubhashtx=<address>");
    hidden_args.emplace_back("-zmqpubhashtxlock=<address>");
    hidden_args.emplace_back("-zmqpubrawblock=<address>");
    hidden_args.emplace_back("-zmqpubrawchainlock=<address>");
    hidden_args.emplace_back("-zmqpubrawchainlocksig=<address>");
    hidden_args.emplace_back("-zmqpubrawgovernancevote=<address>");
    hidden_args.emplace_back("-zmqpubrawgovernanceobject=<address>");
    hidden_args.emplace_back("-zmqpubrawinstantsenddoublespend=<address>");
    hidden_args.emplace_back("-zmqpubrawrecoveredsig=<address>");
    hidden_args.emplace_back("-zmqpubrawtx=<address>");
    hidden_args.emplace_back("-zmqpubrawtxlock=<address>");
    hidden_args.emplace_back("-zmqpubrawtxlocksig=<address>");
    hidden_args.emplace_back("-zmqpubhashblockhwm=<n>");
    hidden_args.emplace_back("-zmqpubhashchainlockhwm=<n>");
    hidden_args.emplace_back("-zmqpubhashgovernanceobjecthwm=<n>");
    hidden_args.emplace_back("-zmqpubhashgovernancevotehwm=<n>");
    hidden_args.emplace_back("-zmqpubhashinstantsenddoublespendhwm=<n>");
    hidden_args.emplace_back("-zmqpubhashrecoveredsighwm=<n>");
    hidden_args.emplace_back("-zmqpubhashtxhwm=<n>");
    hidden_args.emplace_back("-zmqpubhashtxlockhwm=<n>");
    hidden_args.emplace_back("-zmqpubrawblockhwm=<n>");
    hidden_args.emplace_back("-zmqpubrawchainlockhwm=<n>");
    hidden_args.emplace_back("-zmqpubrawchainlocksighwm=<n>");
    hidden_args.emplace_back("-zmqpubrawgovernanceobjecthwm=<n>");
    hidden_args.emplace_back("-zmqpubrawgovernancevotehwm=<n>");
    hidden_args.emplace_back("-zmqpubrawinstantsenddoublespendhwm=<n>");
    hidden_args.emplace_back("-zmqpubrawrecoveredsighwm=<n>");
    hidden_args.emplace_back("-zmqpubrawtxhwm=<n>");
    hidden_args.emplace_back("-zmqpubrawtxlockhwm=<n>");
    hidden_args.emplace_back("-zmqpubrawtxlocksighwm=<n>");
#endif

    argsman.AddArg("-checkblockindex", strprintf("Do a consistency check for the block tree, and  occasionally. (default: %u, regtest: %u)", defaultChainParams->DefaultConsistencyChecks(), regtestChainParams->DefaultConsistencyChecks()), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-checkblocks=<n>", strprintf("How many blocks to check at startup (default: %u, 0 = all)", DEFAULT_CHECKBLOCKS), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-checklevel=<n>", strprintf("How thorough the block verification of -checkblocks is: %s (0-4, default: %u)", Join(CHECKLEVEL_DOC, ", "), DEFAULT_CHECKLEVEL), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-checkaddrman=<n>", strprintf("Run addrman consistency checks every <n> operations. Use 0 to disable. (default: %u)", DEFAULT_ADDRMAN_CONSISTENCY_CHECKS), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-checkmempool=<n>", strprintf("Run mempool consistency checks every <n> transactions. Use 0 to disable. (default: %u, regtest: %u)", defaultChainParams->DefaultConsistencyChecks(), regtestChainParams->DefaultConsistencyChecks()), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-checkpoints", strprintf("Enable rejection of any forks from the known historical chain until block %s (default: %u)", defaultChainParams->Checkpoints().GetHeight(), DEFAULT_CHECKPOINTS_ENABLED), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-deprecatedrpc=<method>", "Allows deprecated RPC method(s) to be used", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-limitancestorcount=<n>", strprintf("Do not accept transactions if number of in-mempool ancestors is <n> or more (default: %u)", DEFAULT_ANCESTOR_LIMIT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-limitancestorsize=<n>", strprintf("Do not accept transactions whose size with all in-mempool ancestors exceeds <n> kilobytes (default: %u)", DEFAULT_ANCESTOR_SIZE_LIMIT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-limitdescendantcount=<n>", strprintf("Do not accept transactions if any ancestor would have <n> or more in-mempool descendants (default: %u)", DEFAULT_DESCENDANT_LIMIT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-limitdescendantsize=<n>", strprintf("Do not accept transactions if any ancestor would have more than <n> kilobytes of in-mempool descendants (default: %u).", DEFAULT_DESCENDANT_SIZE_LIMIT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-stopafterblockimport", strprintf("Stop running after importing blocks from disk (default: %u)", DEFAULT_STOPAFTERBLOCKIMPORT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-stopatheight", strprintf("Stop running after reaching the given height in the main chain (default: %u)", DEFAULT_STOPATHEIGHT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-watchquorums=<n>", strprintf("Watch and validate quorum communication (default: %u)", llmq::DEFAULT_WATCH_QUORUMS), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-addrmantest", "Allows to test address relay on localhost", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-capturemessages", "Capture all P2P messages to disk", ArgsManager::ALLOW_BOOL | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-debug=<category>", "Output debugging information (default: -nodebug, supplying <category> is optional). "
        "If <category> is not supplied or if <category> = 1, output all debugging information. <category> can be: " + LogInstance().LogCategoriesString() + ". This option can be specified multiple times to output multiple categories.", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-debugexclude=<category>", strprintf("Exclude debugging information for a category. Can be used in conjunction with -debug=1 to output debug logs for all categories except the specified category. This option can be specified multiple times to exclude multiple categories."), ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-disablegovernance", strprintf("Disable governance validation (0-1, default: %u)", 0), ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-logips", strprintf("Include IP addresses in debug output (default: %u)", DEFAULT_LOGIPS), ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-logsourcelocations", strprintf("Prepend debug output with name of the originating source location (source file, line number and function name) (default: %u)", DEFAULT_LOGSOURCELOCATIONS), ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-logtimemicros", strprintf("Add microsecond precision to debug timestamps (default: %u)", DEFAULT_LOGTIMEMICROS), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
#ifdef HAVE_THREAD_LOCAL
    argsman.AddArg("-logtimestamps", strprintf("Prepend debug output with timestamp (default: %u)", DEFAULT_LOGTIMESTAMPS), ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
#else
    hidden_args.emplace_back("-logthreadnames");
#endif
    argsman.AddArg("-logthreadnames", strprintf("Prepend debug output with name of the originating thread (only available on platforms supporting thread_local) (default: %u)", DEFAULT_LOGTHREADNAMES), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-maxsigcachesize=<n>", strprintf("Limit sum of signature cache and script execution cache sizes to <n> MiB (default: %u)", DEFAULT_MAX_SIG_CACHE_SIZE), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-maxtipage=<n>", strprintf("Maximum tip age in seconds to consider node in initial block download (default: %u)", DEFAULT_MAX_TIP_AGE), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-mocktime=<n>", "Replace actual time with " + UNIX_EPOCH_TIME + "(default: 0)", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-minsporkkeys=<n>", "Overrides minimum spork signers to change spork value. Only useful for regtest and devnet. Using this on mainnet or testnet will ban you.", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-printpriority", strprintf("Log transaction fee per kB when mining blocks (default: %u)", DEFAULT_PRINTPRIORITY), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-printtoconsole", "Send trace/debug info to console (default: 1 when no -daemon. To disable logging to file, set -nodebuglogfile)", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-pushversion", "Protocol version to report to other nodes", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-shrinkdebugfile", "Shrink debug.log file on client startup (default: 1 when no -debug)", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-sporkaddr=<sparksaddress>", "Override spork address. Only useful for regtest and devnet. Using this on mainnet or testnet will ban you.", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-sporkkey=<privatekey>", "Set the private key to be used for signing spork messages.", ArgsManager::ALLOW_ANY | ArgsManager::SENSITIVE, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-uacomment=<cmt>", "Append comment to the user agent string", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);

    SetupChainParamsBaseOptions(argsman);

    argsman.AddArg("-llmq-data-recovery=<n>", strprintf("Enable automated quorum data recovery (default: %u)", llmq::DEFAULT_ENABLE_QUORUM_DATA_RECOVERY), ArgsManager::ALLOW_ANY, OptionsCategory::MASTERNODE);
    argsman.AddArg("-llmq-qvvec-sync=<quorum_name>:<mode>", strprintf("Defines from which LLMQ type the masternode should sync quorum verification vectors. Can be used multiple times with different LLMQ types. <mode>: %d (sync always from all quorums of the type defined by <quorum_name>), %d (sync from all quorums of the type defined by <quorum_name> if a member of any of the quorums)", (int32_t)llmq::QvvecSyncMode::Always, (int32_t)llmq::QvvecSyncMode::OnlyIfTypeMember), ArgsManager::ALLOW_ANY, OptionsCategory::MASTERNODE);
    argsman.AddArg("-masternodeblsprivkey=<hex>", "Set the masternode BLS private key and enable the client to act as a masternode", ArgsManager::ALLOW_ANY | ArgsManager::SENSITIVE, OptionsCategory::MASTERNODE);
    argsman.AddArg("-platform-user=<user>", "Set the username for the \"platform user\", a restricted user intended to be used by Sparks Platform, to the specified username.", ArgsManager::ALLOW_ANY, OptionsCategory::MASTERNODE);

    argsman.AddArg("-acceptnonstdtxn", strprintf("Relay and mine \"non-standard\" transactions (%sdefault: %u)", "testnet/regtest only; ", !testnetChainParams->RequireStandard()), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::NODE_RELAY);
    argsman.AddArg("-dustrelayfee=<amt>", strprintf("Fee rate (in %s/kB) used to define dust, the value of an output such that it will cost more than its value in fees at this fee rate to spend it. (default: %s)", CURRENCY_UNIT, FormatMoney(DUST_RELAY_TX_FEE)), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::NODE_RELAY);
    argsman.AddArg("-incrementalrelayfee=<amt>", strprintf("Fee rate (in %s/kB) used to define cost of relay, used for mempool limiting and BIP 125 replacement. (default: %s)", CURRENCY_UNIT, FormatMoney(DEFAULT_INCREMENTAL_RELAY_FEE)), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::NODE_RELAY);
    argsman.AddArg("-bytespersigop", strprintf("Equivalent bytes per sigop in transactions for relay and mining (default: %u)", DEFAULT_BYTES_PER_SIGOP), ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);
    argsman.AddArg("-datacarrier", strprintf("Relay and mine data carrier transactions (default: %u)", DEFAULT_ACCEPT_DATACARRIER), ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);
    argsman.AddArg("-datacarriersize", strprintf("Maximum size of data in data carrier transactions we relay and mine (default: %u)", MAX_OP_RETURN_RELAY), ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);
    argsman.AddArg("-minrelaytxfee=<amt>", strprintf("Fees (in %s/kB) smaller than this are considered zero fee for relaying, mining and transaction creation (default: %s)",
        CURRENCY_UNIT, FormatMoney(DEFAULT_MIN_RELAY_TX_FEE)), ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);
    argsman.AddArg("-whitelistforcerelay", strprintf("Add 'forcerelay' permission to whitelisted inbound peers with default permissions. This will relay transactions even if the transactions were already in the mempool. (default: %d)", DEFAULT_WHITELISTFORCERELAY), ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);
    argsman.AddArg("-whitelistrelay", strprintf("Add 'relay' permission to whitelisted inbound peers with default permissions. This will accept relayed transactions even when not relaying transactions (default: %d)", DEFAULT_WHITELISTRELAY), ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);

    argsman.AddArg("-blockmaxsize=<n>", strprintf("Set maximum block size in bytes (default: %d)", DEFAULT_BLOCK_MAX_SIZE), ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    argsman.AddArg("-blockmintxfee=<amt>", strprintf("Set lowest fee rate (in %s/kB) for transactions to be included in block creation. (default: %s)", CURRENCY_UNIT, FormatMoney(DEFAULT_BLOCK_MIN_TX_FEE)), ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    argsman.AddArg("-blockversion=<n>", "Override block version to test forking scenarios", ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::BLOCK_CREATION);

    argsman.AddArg("-rest", strprintf("Accept public REST requests (default: %u)", DEFAULT_REST_ENABLE), ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    argsman.AddArg("-rpcallowip=<ip>", "Allow JSON-RPC connections from specified source. Valid for <ip> are a single IP (e.g. 1.2.3.4), a network/netmask (e.g. 1.2.3.4/255.255.255.0) or a network/CIDR (e.g. 1.2.3.4/24). This option can be specified multiple times", ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    argsman.AddArg("-rpcauth=<userpw>", "Username and HMAC-SHA-256 hashed password for JSON-RPC connections. The field <userpw> comes in the format: <USERNAME>:<SALT>$<HASH>. A canonical python script is included in share/rpcuser. The client then connects normally using the rpcuser=<USERNAME>/rpcpassword=<PASSWORD> pair of arguments. This option can be specified multiple times", ArgsManager::ALLOW_ANY | ArgsManager::SENSITIVE, OptionsCategory::RPC);
    argsman.AddArg("-rpcbind=<addr>[:port]", "Bind to given address to listen for JSON-RPC connections. Do not expose the RPC server to untrusted networks such as the public internet! This option is ignored unless -rpcallowip is also passed. Port is optional and overrides -rpcport. Use [host]:port notation for IPv6. This option can be specified multiple times (default: 127.0.0.1 and ::1 i.e., localhost, or if -rpcallowip has been specified, 0.0.0.0 and :: i.e., all addresses)", ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY | ArgsManager::SENSITIVE, OptionsCategory::RPC);
    argsman.AddArg("-rpccookiefile=<loc>", "Location of the auth cookie. Relative paths will be prefixed by a net-specific datadir location. (default: data dir)", ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    argsman.AddArg("-rpcexternaluser=<users>", "List of comma-separated usernames for JSON-RPC external connections", ArgsManager::ALLOW_ANY | ArgsManager::SENSITIVE, OptionsCategory::RPC);
    argsman.AddArg("-rpcexternalworkqueue=<n>", strprintf("Set the depth of the work queue to service external RPC calls (default: %d)", DEFAULT_HTTP_WORKQUEUE), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::RPC);
    argsman.AddArg("-rpcpassword=<pw>", "Password for JSON-RPC connections", ArgsManager::ALLOW_ANY | ArgsManager::SENSITIVE, OptionsCategory::RPC);
    argsman.AddArg("-rpcport=<port>", strprintf("Listen for JSON-RPC connections on <port> (default: %u, testnet: %u, regtest: %u)", defaultBaseParams->RPCPort(), testnetBaseParams->RPCPort(), regtestBaseParams->RPCPort()), ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::RPC);
    argsman.AddArg("-rpcservertimeout=<n>", strprintf("Timeout during HTTP requests (default: %d)", DEFAULT_HTTP_SERVER_TIMEOUT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::RPC);
    argsman.AddArg("-rpcthreads=<n>", strprintf("Set the number of threads to service RPC calls (default: %d)", DEFAULT_HTTP_THREADS), ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    argsman.AddArg("-rpcuser=<user>", "Username for JSON-RPC connections", ArgsManager::ALLOW_ANY | ArgsManager::SENSITIVE, OptionsCategory::RPC);
    argsman.AddArg("-rpcwhitelist=<whitelist>", "Set a whitelist to filter incoming RPC calls for a specific user. The field <whitelist> comes in the format: <USERNAME>:<rpc 1>,<rpc 2>,...,<rpc n>. If multiple whitelists are set for a given user, they are set-intersected. See -rpcwhitelistdefault documentation for information on default whitelist behavior.", ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    argsman.AddArg("-rpcwhitelistdefault", "Sets default behavior for rpc whitelisting. Unless rpcwhitelistdefault is set to 0, if any -rpcwhitelist is set, the rpc server acts as if all rpc users are subject to empty-unless-otherwise-specified whitelists. If rpcwhitelistdefault is set to 1 and no -rpcwhitelist is set, rpc server acts as if all rpc users are subject to empty whitelists.", ArgsManager::ALLOW_BOOL, OptionsCategory::RPC);
    argsman.AddArg("-rpcworkqueue=<n>", strprintf("Set the depth of the work queue to service RPC calls (default: %d)", DEFAULT_HTTP_WORKQUEUE), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::RPC);
    argsman.AddArg("-server", "Accept command line and JSON-RPC commands", ArgsManager::ALLOW_ANY, OptionsCategory::RPC);

    argsman.AddArg("-statsenabled", strprintf("Publish internal stats to statsd (default: %u)", DEFAULT_STATSD_ENABLE), ArgsManager::ALLOW_ANY, OptionsCategory::STATSD);
    argsman.AddArg("-statshost=<ip>", strprintf("Specify statsd host (default: %s)", DEFAULT_STATSD_HOST), ArgsManager::ALLOW_ANY, OptionsCategory::STATSD);
    argsman.AddArg("-statshostname=<ip>", strprintf("Specify statsd host name (default: %s)", DEFAULT_STATSD_HOSTNAME), ArgsManager::ALLOW_ANY, OptionsCategory::STATSD);
    argsman.AddArg("-statsport=<port>", strprintf("Specify statsd port (default: %u)", DEFAULT_STATSD_PORT), ArgsManager::ALLOW_ANY, OptionsCategory::STATSD);
    argsman.AddArg("-statsns=<ns>", strprintf("Specify additional namespace prefix (default: %s)", DEFAULT_STATSD_NAMESPACE), ArgsManager::ALLOW_ANY, OptionsCategory::STATSD);
    argsman.AddArg("-statsperiod=<seconds>", strprintf("Specify the number of seconds between periodic measurements (default: %d)", DEFAULT_STATSD_PERIOD), ArgsManager::ALLOW_ANY, OptionsCategory::STATSD);
#if HAVE_DECL_FORK
    argsman.AddArg("-daemon", strprintf("Run in the background as a daemon and accept commands (default: %d)", DEFAULT_DAEMON), ArgsManager::ALLOW_BOOL, OptionsCategory::OPTIONS);
    argsman.AddArg("-daemonwait", strprintf("Wait for initialization to be finished before exiting. This implies -daemon (default: %d)", DEFAULT_DAEMONWAIT), ArgsManager::ALLOW_BOOL, OptionsCategory::OPTIONS);
#else
    hidden_args.emplace_back("-daemon");
    hidden_args.emplace_back("-daemonwait");
#endif

    // Add the hidden options
    argsman.AddHiddenArgs(hidden_args);
}

std::string LicenseInfo()
{
    const std::string URL_SOURCE_CODE = "<https://github.com/sparkspay/sparks>";

    return CopyrightHolders(_("Copyright (C)").translated, 2018, COPYRIGHT_YEAR) + "\n" +
           "\n" +
           strprintf(_("Please contribute if you find %s useful. "
                       "Visit %s for further information about the software.").translated,
               PACKAGE_NAME, "<" PACKAGE_URL ">") +
           "\n" +
           strprintf(_("The source code is available from %s.").translated,
               URL_SOURCE_CODE) +
           "\n" +
           "\n" +
           _("This is experimental software.").translated + "\n" +
           strprintf(_("Distributed under the MIT software license, see the accompanying file %s or %s").translated, "COPYING", "<https://opensource.org/licenses/MIT>") +
           "\n";
}

static bool fHaveGenesis = false;
static Mutex g_genesis_wait_mutex;
static std::condition_variable g_genesis_wait_cv;

static void BlockNotifyGenesisWait(const CBlockIndex* pBlockIndex)
{
    if (pBlockIndex != nullptr) {
        {
            LOCK(g_genesis_wait_mutex);
            fHaveGenesis = true;
        }
        g_genesis_wait_cv.notify_all();
    }
}

// If we're using -prune with -reindex, then delete block files that will be ignored by the
// reindex.  Since reindexing works by starting at block file 0 and looping until a blockfile
// is missing, do the same here to delete any later block files after a gap.  Also delete all
// rev files since they'll be rewritten by the reindex anyway.  This ensures that vinfoBlockFile
// is in sync with what's actually on disk by the time we start downloading, so that pruning
// works correctly.
static void CleanupBlockRevFiles()
{
    std::map<std::string, fs::path> mapBlockFiles;

    // Glob all blk?????.dat and rev?????.dat files from the blocks directory.
    // Remove the rev files immediately and insert the blk file paths into an
    // ordered map keyed by block file index.
    LogPrintf("Removing unusable blk?????.dat and rev?????.dat files for -reindex with -prune\n");
    fs::path blocksdir = gArgs.GetBlocksDirPath();
    for (fs::directory_iterator it(blocksdir); it != fs::directory_iterator(); it++) {
        if (fs::is_regular_file(*it) &&
            it->path().filename().string().length() == 12 &&
            it->path().filename().string().substr(8,4) == ".dat")
        {
            if (it->path().filename().string().substr(0,3) == "blk")
                mapBlockFiles[it->path().filename().string().substr(3,5)] = it->path();
            else if (it->path().filename().string().substr(0,3) == "rev")
                remove(it->path());
        }
    }

    // Remove all block files that aren't part of a contiguous set starting at
    // zero by walking the ordered map (keys are block file indices) by
    // keeping a separate counter.  Once we hit a gap (or if 0 doesn't exist)
    // start removing block files.
    int nContigCounter = 0;
    for (const std::pair<const std::string, fs::path>& item : mapBlockFiles) {
        if (LocaleIndependentAtoi<int>(item.first) == nContigCounter) {
            nContigCounter++;
            continue;
        }
        remove(item.second);
    }
}

#if HAVE_SYSTEM
static void StartupNotify(const ArgsManager& args)
{
    std::string cmd = args.GetArg("-startupnotify", "");
    if (!cmd.empty()) {
        std::thread t(runCommand, cmd);
        t.detach(); // thread runs free
    }
}
#endif

static void PeriodicStats(ArgsManager& args, ChainstateManager& chainman, const CTxMemPool& mempool)
{
    assert(args.GetBoolArg("-statsenabled", DEFAULT_STATSD_ENABLE));
    CCoinsStats stats{CoinStatsHashType::NONE};
    chainman.ActiveChainstate().ForceFlushStateToDisk();
    if (WITH_LOCK(cs_main, return GetUTXOStats(&chainman.ActiveChainstate().CoinsDB(), std::ref(chainman.m_blockman), stats, RpcInterruptionPoint, chainman.ActiveChain().Tip()))) {
        statsClient.gauge("utxoset.tx", stats.nTransactions, 1.0f);
        statsClient.gauge("utxoset.txOutputs", stats.nTransactionOutputs, 1.0f);
        statsClient.gauge("utxoset.dbSizeBytes", stats.nDiskSize, 1.0f);
        statsClient.gauge("utxoset.blockHeight", stats.nHeight, 1.0f);
        if (stats.total_amount.has_value()) {
            statsClient.gauge("utxoset.totalAmount", (double)stats.total_amount.value() / (double)COIN, 1.0f);
        }
    } else {
        // something went wrong
        LogPrintf("%s: GetUTXOStats failed\n", __func__);
    }

    // short version of GetNetworkHashPS(120, -1);
    CBlockIndex *tip = chainman.ActiveChain().Tip();
    CBlockIndex *pindex = tip;
    int64_t minTime = pindex->GetBlockTime();
    int64_t maxTime = minTime;
    for (int i = 0; i < 120 && pindex->pprev != nullptr; i++) {
        pindex = pindex->pprev;
        int64_t time = pindex->GetBlockTime();
        minTime = std::min(time, minTime);
        maxTime = std::max(time, maxTime);
    }
    arith_uint256 workDiff = tip->nChainWork - pindex->nChainWork;
    int64_t timeDiff = maxTime - minTime;
    double nNetworkHashPS = workDiff.getdouble() / timeDiff;

    statsClient.gaugeDouble("network.hashesPerSecond", nNetworkHashPS);
    statsClient.gaugeDouble("network.terahashesPerSecond", nNetworkHashPS / 1e12);
    statsClient.gaugeDouble("network.petahashesPerSecond", nNetworkHashPS / 1e15);
    statsClient.gaugeDouble("network.exahashesPerSecond", nNetworkHashPS / 1e18);
    // No need for cs_main, we never use null tip here
    statsClient.gaugeDouble("network.difficulty", (double)GetDifficulty(tip));

    statsClient.gauge("transactions.txCacheSize", WITH_LOCK(cs_main, return chainman.ActiveChainstate().CoinsTip().GetCacheSize()), 1.0f);
    statsClient.gauge("transactions.totalTransactions", tip->nChainTx, 1.0f);

    {
        LOCK(mempool.cs);
        statsClient.gauge("transactions.mempool.totalTransactions", mempool.size(), 1.0f);
        statsClient.gauge("transactions.mempool.totalTxBytes", (int64_t) mempool.GetTotalTxSize(), 1.0f);
        statsClient.gauge("transactions.mempool.memoryUsageBytes", (int64_t) mempool.DynamicMemoryUsage(), 1.0f);
        statsClient.gauge("transactions.mempool.minFeePerKb", mempool.GetMinFee(args.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000).GetFeePerK(), 1.0f);
    }
}

/** Sanity checks
 *  Ensure that Sparks Core is running in a usable environment with all
 *  necessary library support.
 */
static bool InitSanityCheck()
{
    if (!ECC_InitSanityCheck()) {
        return InitError(Untranslated("Elliptic curve cryptography sanity check failure. Aborting."));
    }

    if (!BLSInit()) {
        return false;
    }

    if (!Random_SanityCheck()) {
        return InitError(Untranslated("OS cryptographic RNG sanity check failure. Aborting."));
    }

    if (!ChronoSanityCheck()) {
        return InitError(Untranslated("Clock epoch mismatch. Aborting."));
    }

    return true;
}

static bool AppInitServers(const CoreContext& context, NodeContext& node)
{
    const ArgsManager& args = *Assert(node.args);
    RPCServer::OnStarted(&OnRPCStarted);
    RPCServer::OnStopped(&OnRPCStopped);
    if (!InitHTTPServer())
        return false;
    StartRPC();
    node.rpc_interruption_point = RpcInterruptionPoint;
    if (!StartHTTPRPC(context))
        return false;
    if (args.GetBoolArg("-rest", DEFAULT_REST_ENABLE)) StartREST(context);
    StartHTTPServer();
    return true;
}

// Parameter interaction based on rules
void InitParameterInteraction(ArgsManager& args)
{
    // when specifying an explicit binding address, you want to listen on it
    // even when -connect or -proxy is specified
    if (args.IsArgSet("-bind")) {
        if (args.SoftSetBoolArg("-listen", true))
            LogPrintf("%s: parameter interaction: -bind set -> setting -listen=1\n", __func__);
    }
    if (args.IsArgSet("-whitebind")) {
        if (args.SoftSetBoolArg("-listen", true))
            LogPrintf("%s: parameter interaction: -whitebind set -> setting -listen=1\n", __func__);
    }

    if (args.IsArgSet("-connect")) {
        // when only connecting to trusted nodes, do not seed via DNS, or listen by default
        if (args.SoftSetBoolArg("-dnsseed", false))
            LogPrintf("%s: parameter interaction: -connect set -> setting -dnsseed=0\n", __func__);
        if (args.SoftSetBoolArg("-listen", false))
            LogPrintf("%s: parameter interaction: -connect set -> setting -listen=0\n", __func__);
    }

    std::string proxy_arg = args.GetArg("-proxy", "");
    if (proxy_arg != "" && proxy_arg != "0") {
        // to protect privacy, do not listen by default if a default proxy server is specified
        if (args.SoftSetBoolArg("-listen", false))
            LogPrintf("%s: parameter interaction: -proxy set -> setting -listen=0\n", __func__);
        // to protect privacy, do not map ports when a proxy is set. The user may still specify -listen=1
        // to listen locally, so don't rely on this happening through -listen below.
        if (args.SoftSetBoolArg("-upnp", false))
            LogPrintf("%s: parameter interaction: -proxy set -> setting -upnp=0\n", __func__);
        if (args.SoftSetBoolArg("-natpmp", false))
            LogPrintf("%s: parameter interaction: -proxy set -> setting -natpmp=0\n", __func__);
        // to protect privacy, do not discover addresses by default
        if (args.SoftSetBoolArg("-discover", false))
            LogPrintf("%s: parameter interaction: -proxy set -> setting -discover=0\n", __func__);
    }

    if (!args.GetBoolArg("-listen", DEFAULT_LISTEN)) {
        // do not map ports or try to retrieve public IP when not listening (pointless)
        if (args.SoftSetBoolArg("-upnp", false))
            LogPrintf("%s: parameter interaction: -listen=0 -> setting -upnp=0\n", __func__);
        if (args.SoftSetBoolArg("-natpmp", false))
            LogPrintf("%s: parameter interaction: -listen=0 -> setting -natpmp=0\n", __func__);
        if (args.SoftSetBoolArg("-discover", false))
            LogPrintf("%s: parameter interaction: -listen=0 -> setting -discover=0\n", __func__);
        if (args.SoftSetBoolArg("-listenonion", false))
            LogPrintf("%s: parameter interaction: -listen=0 -> setting -listenonion=0\n", __func__);
        if (args.SoftSetBoolArg("-i2pacceptincoming", false)) {
            LogPrintf("%s: parameter interaction: -listen=0 -> setting -i2pacceptincoming=0\n", __func__);
        }
    }

    if (args.IsArgSet("-externalip")) {
        // if an explicit public IP is specified, do not try to find others
        if (args.SoftSetBoolArg("-discover", false))
            LogPrintf("%s: parameter interaction: -externalip set -> setting -discover=0\n", __func__);
    }

    // disable whitelistrelay in blocksonly mode
    if (args.GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY)) {
        if (args.SoftSetBoolArg("-whitelistrelay", false))
            LogPrintf("%s: parameter interaction: -blocksonly=1 -> setting -whitelistrelay=0\n", __func__);
    }

    // Forcing relay from whitelisted hosts implies we will accept relays from them in the first place.
    if (args.GetBoolArg("-whitelistforcerelay", DEFAULT_WHITELISTFORCERELAY)) {
        if (args.SoftSetBoolArg("-whitelistrelay", true))
            LogPrintf("%s: parameter interaction: -whitelistforcerelay=1 -> setting -whitelistrelay=1\n", __func__);
    }

    int64_t nPruneArg = args.GetArg("-prune", 0);
    if (nPruneArg > 0) {
        if (args.SoftSetBoolArg("-disablegovernance", true)) {
            LogPrintf("%s: parameter interaction: -prune=%d -> setting -disablegovernance=true\n", __func__, nPruneArg);
        }
        if (args.SoftSetBoolArg("-txindex", false)) {
            LogPrintf("%s: parameter interaction: -prune=%d -> setting -txindex=false\n", __func__, nPruneArg);
        }
    }

    // Make sure additional indexes are recalculated correctly in VerifyDB
    // (we must reconnect blocks whenever we disconnect them for these indexes to work)
    bool fAdditionalIndexes =
        args.GetBoolArg("-addressindex", DEFAULT_ADDRESSINDEX) ||
        args.GetBoolArg("-spentindex", DEFAULT_SPENTINDEX) ||
        args.GetBoolArg("-timestampindex", DEFAULT_TIMESTAMPINDEX);

    if (fAdditionalIndexes && args.GetArg("-checklevel", DEFAULT_CHECKLEVEL) < 4) {
        args.ForceSetArg("-checklevel", "4");
        LogPrintf("%s: parameter interaction: additional indexes -> setting -checklevel=4\n", __func__);
    }

    if (args.IsArgSet("-masternodeblsprivkey") && args.SoftSetBoolArg("-disablewallet", true)) {
        LogPrintf("%s: parameter interaction: -masternodeblsprivkey set -> setting -disablewallet=1\n", __func__);
    }
}

/**
 * Initialize global loggers.
 *
 * Note that this is called very early in the process lifetime, so you should be
 * careful about what global state you rely on here.
 */
void InitLogging(const ArgsManager& args)
{
    LogInstance().m_print_to_file = !args.IsArgNegated("-debuglogfile");
    LogInstance().m_file_path = AbsPathForConfigVal(args.GetArg("-debuglogfile", DEFAULT_DEBUGLOGFILE));
    LogInstance().m_print_to_console = args.GetBoolArg("-printtoconsole", !args.GetBoolArg("-daemon", false));
    LogInstance().m_log_timestamps = args.GetBoolArg("-logtimestamps", DEFAULT_LOGTIMESTAMPS);
    LogInstance().m_log_time_micros = args.GetBoolArg("-logtimemicros", DEFAULT_LOGTIMEMICROS);
#ifdef HAVE_THREAD_LOCAL
    LogInstance().m_log_threadnames = args.GetBoolArg("-logthreadnames", DEFAULT_LOGTHREADNAMES);
#endif
    LogInstance().m_log_sourcelocations = args.GetBoolArg("-logsourcelocations", DEFAULT_LOGSOURCELOCATIONS);

    fLogIPs = args.GetBoolArg("-logips", DEFAULT_LOGIPS);

    std::string version_string = FormatFullVersion();
#ifdef DEBUG_CORE
    version_string += " (debug build)";
#else
    version_string += " (release build)";
#endif
    LogPrintf(PACKAGE_NAME " version %s\n", version_string);
}

namespace { // Variables internal to initialization process only

int nMaxConnections;
int nUserMaxConnections;
int nFD;
ServiceFlags nLocalServices = ServiceFlags(NODE_NETWORK | NODE_NETWORK_LIMITED | NODE_HEADERS_COMPRESSED);
int64_t peer_connect_timeout;
std::set<BlockFilterType> g_enabled_filter_types;

} // namespace

[[noreturn]] static void new_handler_terminate()
{
    // Rather than throwing std::bad-alloc if allocation fails, terminate
    // immediately to (try to) avoid chain corruption.
    // Since LogPrintf may itself allocate memory, set the handler directly
    // to terminate first.
    std::set_new_handler(std::terminate);
    LogPrintf("Error: Out of memory. Terminating.\n");

    // The log was successful, terminate now.
    std::terminate();
};

bool AppInitBasicSetup(const ArgsManager& args)
{
    // ********************************************************* Step 1: setup
#ifdef _MSC_VER
    // Turn off Microsoft heap dump noise
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, CreateFileA("NUL", GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, 0));
    // Disable confusing "helpful" text message on abort, Ctrl-C
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#ifdef WIN32
    // Enable heap terminate-on-corruption
    HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);
#endif
    if (!InitShutdownState()) {
        return InitError(Untranslated("Initializing wait-for-shutdown state failed."));
    }

    if (!SetupNetworking()) {
        return InitError(Untranslated("Initializing networking failed."));
    }

#ifndef WIN32
    if (!args.GetBoolArg("-sysperms", false)) {
        umask(077);
    }

    // Clean shutdown on SIGTERM
    registerSignalHandler(SIGTERM, HandleSIGTERM);
    registerSignalHandler(SIGINT, HandleSIGTERM);

    // Reopen debug.log on SIGHUP
    registerSignalHandler(SIGHUP, HandleSIGHUP);

    // Ignore SIGPIPE, otherwise it will bring the daemon down if the client closes unexpectedly
    signal(SIGPIPE, SIG_IGN);
#else
    SetConsoleCtrlHandler(consoleCtrlHandler, true);
#endif

    std::set_new_handler(new_handler_terminate);

    return true;
}

bool AppInitParameterInteraction(const ArgsManager& args)
{
    const CChainParams& chainparams = Params();
    // ********************************************************* Step 2: parameter interactions

    // also see: InitParameterInteraction()

    // Error if network-specific options (-addnode, -connect, etc) are
    // specified in default section of config file, but not overridden
    // on the command line or in this network's section of the config file.
    std::string network = args.GetChainName();
    bilingual_str errors;
    for (const auto& arg : args.GetUnsuitableSectionOnlyArgs()) {
        errors += strprintf(_("Config setting for %s only applied on %s network when in [%s] section.") + Untranslated("\n"), arg, network, network);
    }

    if (!errors.empty()) {
        return InitError(errors);
    }

    // Warn if unrecognized section name are present in the config file.
    bilingual_str warnings;
    for (const auto& section : args.GetUnrecognizedSections()) {
        warnings += strprintf(Untranslated("%s:%i ") + _("Section [%s] is not recognized.") + Untranslated("\n"), section.m_file, section.m_line, section.m_name);
    }

    if (!warnings.empty()) {
        InitWarning(warnings);
    }

    if (!fs::is_directory(gArgs.GetBlocksDirPath())) {
        return InitError(strprintf(_("Specified blocks directory \"%s\" does not exist."), args.GetArg("-blocksdir", "")));
    }

    // parse and validate enabled filter types
    std::string blockfilterindex_value = args.GetArg("-blockfilterindex", DEFAULT_BLOCKFILTERINDEX);
    if (blockfilterindex_value == "" || blockfilterindex_value == "1") {
        g_enabled_filter_types = AllBlockFilterTypes();
    } else if (blockfilterindex_value != "0") {
        const std::vector<std::string> names = args.GetArgs("-blockfilterindex");
        for (const auto& name : names) {
            BlockFilterType filter_type;
            if (!BlockFilterTypeByName(name, filter_type)) {
                return InitError(strprintf(_("Unknown -blockfilterindex value %s."), name));
            }
            g_enabled_filter_types.insert(filter_type);
        }
    }

    // Signal NODE_COMPACT_FILTERS if peerblockfilters and basic filters index are both enabled.
    if (args.GetBoolArg("-peerblockfilters", DEFAULT_PEERBLOCKFILTERS)) {
        if (g_enabled_filter_types.count(BlockFilterType::BASIC_FILTER) != 1) {
            return InitError(_("Cannot set -peerblockfilters without -blockfilterindex."));
        }

        nLocalServices = ServiceFlags(nLocalServices | NODE_COMPACT_FILTERS);
    }

    // if using block pruning, then disallow txindex, coinstatsindex and require disabling governance validation
    if (args.GetArg("-prune", 0)) {
        if (args.GetBoolArg("-txindex", DEFAULT_TXINDEX))
            return InitError(_("Prune mode is incompatible with -txindex."));
        if (args.GetBoolArg("-coinstatsindex", DEFAULT_COINSTATSINDEX))
            return InitError(_("Prune mode is incompatible with -coinstatsindex."));
        if (!args.GetBoolArg("-disablegovernance", !DEFAULT_GOVERNANCE_ENABLE)) {
            return InitError(_("Prune mode is incompatible with -disablegovernance=false."));
        }
    }

    if (args.IsArgSet("-devnet")) {
        // Require setting of ports when running devnet
        if (args.GetBoolArg("-listen", DEFAULT_LISTEN) && !args.IsArgSet("-port")) {
            return InitError(_("-port must be specified when -devnet and -listen are specified"));
        }
        if (args.GetBoolArg("-server", false) && !args.IsArgSet("-rpcport")) {
            return InitError(_("-rpcport must be specified when -devnet and -server are specified"));
        }
        if (args.GetArgs("-devnet").size() > 1) {
            return InitError(_("-devnet can only be specified once"));
        }
    }

    fAllowPrivateNet = args.GetBoolArg("-allowprivatenet", DEFAULT_ALLOWPRIVATENET);

    // -bind and -whitebind can't be set when not listening
    size_t nUserBind = args.GetArgs("-bind").size() + args.GetArgs("-whitebind").size();
    if (nUserBind != 0 && !args.GetBoolArg("-listen", DEFAULT_LISTEN)) {
        return InitError(Untranslated("Cannot set -bind or -whitebind together with -listen=0"));
    }

    // if listen=0, then disallow listenonion=1
    if (!args.GetBoolArg("-listen", DEFAULT_LISTEN) && args.GetBoolArg("-listenonion", DEFAULT_LISTEN_ONION)) {
        return InitError(Untranslated("Cannot set -listen=0 together with -listenonion=1"));
    }

    // Make sure enough file descriptors are available
    int nBind = std::max(nUserBind, size_t(1));
    nUserMaxConnections = args.GetArg("-maxconnections", DEFAULT_MAX_PEER_CONNECTIONS);
    nMaxConnections = std::max(nUserMaxConnections, 0);

    nFD = RaiseFileDescriptorLimit(nMaxConnections + MIN_CORE_FILEDESCRIPTORS + MAX_ADDNODE_CONNECTIONS + nBind + NUM_FDS_MESSAGE_CAPTURE);

#ifdef USE_POLL
    int fd_max = nFD;
#else
    int fd_max = FD_SETSIZE;
#endif
    // Trim requested connection counts, to fit into system limitations
    // <int> in std::min<int>(...) to work around FreeBSD compilation issue described in #2695
    nMaxConnections = std::max(std::min<int>(nMaxConnections, fd_max - nBind - MIN_CORE_FILEDESCRIPTORS - MAX_ADDNODE_CONNECTIONS - NUM_FDS_MESSAGE_CAPTURE), 0);
    if (nFD < MIN_CORE_FILEDESCRIPTORS)
        return InitError(_("Not enough file descriptors available."));
    nMaxConnections = std::min(nFD - MIN_CORE_FILEDESCRIPTORS - MAX_ADDNODE_CONNECTIONS - NUM_FDS_MESSAGE_CAPTURE, nMaxConnections);

    if (nMaxConnections < nUserMaxConnections)
        InitWarning(strprintf(_("Reducing -maxconnections from %d to %d, because of system limitations."), nUserMaxConnections, nMaxConnections));

    // ********************************************************* Step 3: parameter-to-internal-flags
    if (args.IsArgSet("-debug")) {
        // Special-case: if -debug=0/-nodebug is set, turn off debugging messages
        const std::vector<std::string> categories = args.GetArgs("-debug");

        if (std::none_of(categories.begin(), categories.end(),
            [](std::string cat){return cat == "0" || cat == "none";})) {
            for (const auto& cat : categories) {
                if (!LogInstance().EnableCategory(cat)) {
                    InitWarning(strprintf(_("Unsupported logging category %s=%s."), "-debug", cat));
                }
            }
        }
    }

    // Now remove the logging categories which were explicitly excluded
    for (const std::string& cat : args.GetArgs("-debugexclude")) {
        if (!LogInstance().DisableCategory(cat)) {
            InitWarning(strprintf(_("Unsupported logging category %s=%s."), "-debugexclude", cat));
        }
    }

    fCheckBlockIndex = args.GetBoolArg("-checkblockindex", chainparams.DefaultConsistencyChecks());
    fCheckpointsEnabled = args.GetBoolArg("-checkpoints", DEFAULT_CHECKPOINTS_ENABLED);

    hashAssumeValid = uint256S(args.GetArg("-assumevalid", chainparams.GetConsensus().defaultAssumeValid.GetHex()));
    if (!hashAssumeValid.IsNull())
        LogPrintf("Assuming ancestors of block %s have valid signatures.\n", hashAssumeValid.GetHex());
    else
        LogPrintf("Validating signatures for all blocks.\n");

    if (args.IsArgSet("-minimumchainwork")) {
        const std::string minChainWorkStr = args.GetArg("-minimumchainwork", "");
        if (!IsHexNumber(minChainWorkStr)) {
            return InitError(strprintf(Untranslated("Invalid non-hex (%s) minimum chain work value specified"), minChainWorkStr));
        }
        nMinimumChainWork = UintToArith256(uint256S(minChainWorkStr));
    } else {
        nMinimumChainWork = UintToArith256(chainparams.GetConsensus().nMinimumChainWork);
    }
    LogPrintf("Setting nMinimumChainWork=%s\n", nMinimumChainWork.GetHex());
    if (nMinimumChainWork < UintToArith256(chainparams.GetConsensus().nMinimumChainWork)) {
        LogPrintf("Warning: nMinimumChainWork set below default value of %s\n", chainparams.GetConsensus().nMinimumChainWork.GetHex());
    }

    // mempool limits
    int64_t nMempoolSizeMax = args.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    int64_t nMempoolSizeMin = args.GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT) * 1000 * 40;
    if (nMempoolSizeMax < 0 || nMempoolSizeMax < nMempoolSizeMin)
        return InitError(strprintf(_("-maxmempool must be at least %d MB"), std::ceil(nMempoolSizeMin / 1000000.0)));
    // incremental relay fee sets the minimum feerate increase necessary for BIP 125 replacement in the mempool
    // and the amount the mempool min fee increases above the feerate of txs evicted due to mempool limiting.
    if (args.IsArgSet("-incrementalrelayfee")) {
        if (std::optional<CAmount> inc_relay_fee = ParseMoney(args.GetArg("-incrementalrelayfee", ""))) {
            ::incrementalRelayFee = CFeeRate{inc_relay_fee.value()};
        } else {
            return InitError(AmountErrMsg("incrementalrelayfee", args.GetArg("-incrementalrelayfee", "")));
        }
    }

    // block pruning; get the amount of disk space (in MiB) to allot for block & undo files
    int64_t nPruneArg = args.GetArg("-prune", 0);
    if (nPruneArg < 0) {
        return InitError(_("Prune cannot be configured with a negative value."));
    }
    nPruneTarget = (uint64_t) nPruneArg * 1024 * 1024;
    if (nPruneArg == 1) {  // manual pruning: -prune=1
        LogPrintf("Block pruning enabled.  Use RPC call pruneblockchain(height) to manually prune block and undo files.\n");
        nPruneTarget = std::numeric_limits<uint64_t>::max();
        fPruneMode = true;
    } else if (nPruneTarget) {
        if (args.GetBoolArg("-regtest", false)) {
            // we use 1MB blocks to test this on regtest
            if (nPruneTarget < 550 * 1024 * 1024) {
                return InitError(strprintf(_("Prune configured below the minimum of %d MiB.  Please use a higher number."), 550));
            }
        } else {
            if (nPruneTarget < MIN_DISK_SPACE_FOR_BLOCK_FILES) {
                return InitError(strprintf(_("Prune configured below the minimum of %d MiB.  Please use a higher number."), MIN_DISK_SPACE_FOR_BLOCK_FILES / 1024 / 1024));
            }
        }
        LogPrintf("Prune configured to target %u MiB on disk for block and undo files.\n", nPruneTarget / 1024 / 1024);
        fPruneMode = true;
    }

    nConnectTimeout = args.GetArg("-timeout", DEFAULT_CONNECT_TIMEOUT);
    if (nConnectTimeout <= 0) {
        nConnectTimeout = DEFAULT_CONNECT_TIMEOUT;
    }

    peer_connect_timeout = args.GetArg("-peertimeout", DEFAULT_PEER_CONNECT_TIMEOUT);
    if (peer_connect_timeout <= 0) {
        return InitError(Untranslated("peertimeout must be a positive integer."));
    }

    if (args.IsArgSet("-minrelaytxfee")) {
        if (std::optional<CAmount> min_relay_fee = ParseMoney(args.GetArg("-minrelaytxfee", ""))) {
            // High fee check is done afterward in CWallet::Create()
            ::minRelayTxFee = CFeeRate{min_relay_fee.value()};
        } else {
            return InitError(AmountErrMsg("minrelaytxfee", args.GetArg("-minrelaytxfee", "")));
        }
    } else if (incrementalRelayFee > ::minRelayTxFee) {
        // Allow only setting incrementalRelayFee to control both
        ::minRelayTxFee = incrementalRelayFee;
        LogPrintf("Increasing minrelaytxfee to %s to match incrementalrelayfee\n",::minRelayTxFee.ToString());
    }

    // Sanity check argument for min fee for including tx in block
    // TODO: Harmonize which arguments need sanity checking and where that happens
    if (args.IsArgSet("-blockmintxfee")) {
        if (!ParseMoney(args.GetArg("-blockmintxfee", ""))) {
            return InitError(AmountErrMsg("blockmintxfee", args.GetArg("-blockmintxfee", "")));
        }
    }

    // Feerate used to define dust.  Shouldn't be changed lightly as old
    // implementations may inadvertently create non-standard transactions
    if (args.IsArgSet("-dustrelayfee")) {
        if (std::optional<CAmount> parsed = ParseMoney(args.GetArg("-dustrelayfee", ""))) {
            dustRelayFee = CFeeRate{parsed.value()};
        } else {
            return InitError(AmountErrMsg("dustrelayfee", args.GetArg("-dustrelayfee", "")));
        }
    }

    fRequireStandard = !args.GetBoolArg("-acceptnonstdtxn", !chainparams.RequireStandard());
    if (!chainparams.IsTestChain() && !fRequireStandard) {
        return InitError(strprintf(Untranslated("acceptnonstdtxn is not currently supported for %s chain"), chainparams.NetworkIDString()));
    }
    nBytesPerSigOp = args.GetArg("-bytespersigop", nBytesPerSigOp);

    if (!g_wallet_init_interface.ParameterInteraction()) return false;

    fIsBareMultisigStd = args.GetBoolArg("-permitbaremultisig", DEFAULT_PERMIT_BAREMULTISIG);
    fAcceptDatacarrier = args.GetBoolArg("-datacarrier", DEFAULT_ACCEPT_DATACARRIER);
    nMaxDatacarrierBytes = args.GetArg("-datacarriersize", nMaxDatacarrierBytes);

    // Option to startup with mocktime set (used for regression testing):
    SetMockTime(args.GetArg("-mocktime", 0)); // SetMockTime(0) is a no-op

    if (args.GetBoolArg("-peerbloomfilters", DEFAULT_PEERBLOOMFILTERS))
        nLocalServices = ServiceFlags(nLocalServices | NODE_BLOOM);

    nMaxTipAge = args.GetArg("-maxtipage", DEFAULT_MAX_TIP_AGE);

    if (args.IsArgSet("-proxy") && args.GetArg("-proxy", "").empty()) {
        return InitError(_("No proxy server specified. Use -proxy=<ip> or -proxy=<ip:port>."));
    }

    try {
        const bool fRecoveryEnabled{llmq::QuorumDataRecoveryEnabled()};
        const bool fQuorumVvecRequestsEnabled{llmq::GetEnabledQuorumVvecSyncEntries().size() > 0};
        if (!fRecoveryEnabled && fQuorumVvecRequestsEnabled) {
            InitWarning(Untranslated("-llmq-qvvec-sync set but recovery is disabled due to -llmq-data-recovery=0"));
        }
    } catch (const std::invalid_argument& e) {
        return InitError(Untranslated(e.what()));
    }

    if (args.IsArgSet("-masternodeblsprivkey")) {
        if (!args.GetBoolArg("-listen", DEFAULT_LISTEN) && Params().RequireRoutableExternalIP()) {
            return InitError(Untranslated("Masternode must accept connections from outside, set -listen=1"));
        }
        if (!args.GetBoolArg("-txindex", DEFAULT_TXINDEX)) {
            return InitError(Untranslated("Masternode must have transaction index enabled, set -txindex=1"));
        }
        if (!args.GetBoolArg("-peerbloomfilters", DEFAULT_PEERBLOOMFILTERS)) {
            return InitError(Untranslated("Masternode must have bloom filters enabled, set -peerbloomfilters=1"));
        }
        if (args.GetArg("-prune", 0) > 0) {
            return InitError(Untranslated("Masternode must have no pruning enabled, set -prune=0"));
        }
        if (args.GetArg("-maxconnections", DEFAULT_MAX_PEER_CONNECTIONS) < DEFAULT_MAX_PEER_CONNECTIONS) {
            return InitError(strprintf(Untranslated("Masternode must be able to handle at least %d connections, set -maxconnections=%d"), DEFAULT_MAX_PEER_CONNECTIONS, DEFAULT_MAX_PEER_CONNECTIONS));
        }
        if (args.GetBoolArg("-disablegovernance", !DEFAULT_GOVERNANCE_ENABLE)) {
            return InitError(_("You can not disable governance validation on a masternode."));
        }
    }

    if (args.GetBoolArg("-disablegovernance", !DEFAULT_GOVERNANCE_ENABLE)) {
        InitWarning(_("You are starting with governance validation disabled.") +
            (fPruneMode ?
                Untranslated(" ") + _("This is expected because you are running a pruned node.") :
                Untranslated("")));
    }

    return true;
}

static bool LockDataDirectory(bool probeOnly)
{
    // Make sure only a single Sparks Core process is using the data directory.
    fs::path datadir = GetDataDir();
    if (!DirIsWritable(datadir)) {
        return InitError(strprintf(_("Cannot write to data directory '%s'; check permissions."), datadir.string()));
    }
    if (!LockDirectory(datadir, ".lock", probeOnly)) {
        return InitError(strprintf(_("Cannot obtain a lock on data directory %s. %s is probably already running."), datadir.string(), PACKAGE_NAME));
    }
    return true;
}

bool AppInitSanityChecks()
{
    // ********************************************************* Step 4: sanity checks

    // Initialize elliptic curve code
    std::string sha256_algo = SHA256AutoDetect();
    LogPrintf("Using the '%s' SHA256 implementation\n", sha256_algo);
    RandomInit();
    ECC_Start();

    // Sanity check
    if (!InitSanityCheck())
        return InitError(strprintf(_("Initialization sanity check failed. %s is shutting down."), PACKAGE_NAME));

    // Probe the data directory lock to give an early error message, if possible
    // We cannot hold the data directory lock here, as the forking for daemon() hasn't yet happened,
    // and a fork will cause weird behavior to it.
    return LockDataDirectory(true);
}

bool AppInitLockDataDirectory()
{
    // After daemonization get the data directory lock again and hold on to it until exit
    // This creates a slight window for a race condition to happen, however this condition is harmless: it
    // will at most make us exit without printing a message to console.
    if (!LockDataDirectory(false)) {
        // Detailed error printed inside LockDataDirectory
        return false;
    }
    return true;
}

bool AppInitInterfaces(NodeContext& node)
{
    node.chain = interfaces::MakeChain(node);
    // Create client interfaces for wallets that are supposed to be loaded
    // according to -wallet and -disablewallet options. This only constructs
    // the interfaces, it doesn't load wallet data. Wallets actually get loaded
    // when load() and start() interface methods are called below.
    g_wallet_init_interface.Construct(node);
    return true;
}

bool AppInitMain(const CoreContext& context, NodeContext& node, interfaces::BlockAndHeaderTipInfo* tip_info)
{
    const ArgsManager& args = *Assert(node.args);
    const CChainParams& chainparams = Params();
    // ********************************************************* Step 4a: application initialization
    if (!CreatePidFile(args)) {
        // Detailed error printed inside CreatePidFile().
        return false;
    }
    if (LogInstance().m_print_to_file) {
        if (args.GetBoolArg("-shrinkdebugfile", LogInstance().DefaultShrinkDebugFile())) {
            // Do this first since it both loads a bunch of debug.log into memory,
            // and because this needs to happen before any other debug.log printing
            LogInstance().ShrinkDebugFile();
        }
    }
    if (!LogInstance().StartLogging()) {
        return InitError(strprintf(Untranslated("Could not open debug log file %s"),
            LogInstance().m_file_path.string()));
    }

    if (!LogInstance().m_log_timestamps)
        LogPrintf("Startup time: %s\n", FormatISO8601DateTime(GetTime()));
    LogPrintf("Default data directory %s\n", GetDefaultDataDir().string());
    LogPrintf("Using data directory %s\n", GetDataDir().string());

    // Only log conf file usage message if conf file actually exists.
    fs::path config_file_path = GetConfigFile(args.GetArg("-conf", BITCOIN_CONF_FILENAME));
    if (fs::exists(config_file_path)) {
        LogPrintf("Config file: %s\n", config_file_path.string());
    } else if (args.IsArgSet("-conf")) {
        // Warn if no conf file exists at path provided by user
        InitWarning(strprintf(_("The specified config file %s does not exist"), config_file_path.string()));
    } else {
        // Not categorizing as "Warning" because it's the default behavior
        LogPrintf("Config file: %s (not found, skipping)\n", config_file_path.string());
    }

    // Log the config arguments to debug.log
    args.LogArgs();

    LogPrintf("Using at most %i automatic connections (%i file descriptors available)\n", nMaxConnections, nFD);

    // Warn about relative -datadir path.
    if (args.IsArgSet("-datadir") && !fs::path(args.GetArg("-datadir", "")).is_absolute()) {
        LogPrintf("Warning: relative datadir option '%s' specified, which will be interpreted relative to the " /* Continued */
                  "current working directory '%s'. This is fragile, because if Sparks Core is started in the future "
                  "from a different location, it will be unable to locate the current data files. There could "
                  "also be data loss if Sparks Core is started while in a temporary directory.\n",
                  args.GetArg("-datadir", ""), fs::current_path().string());
    }

    InitSignatureCache();
    InitScriptExecutionCache();

    int script_threads = args.GetArg("-par", DEFAULT_SCRIPTCHECK_THREADS);
    if (script_threads <= 0) {
        // -par=0 means autodetect (number of cores - 1 script threads)
        // -par=-n means "leave n cores free" (number of cores - n - 1 script threads)
        script_threads += GetNumCores();
    }

    // Subtract 1 because the main thread counts towards the par threads
    script_threads = std::max(script_threads - 1, 0);

    // Number of script-checking threads <= MAX_SCRIPTCHECK_THREADS
    script_threads = std::min(script_threads, MAX_SCRIPTCHECK_THREADS);

    LogPrintf("Script verification uses %d additional threads\n", script_threads);
    if (script_threads >= 1) {
        g_parallel_script_checks = true;
        StartScriptCheckWorkerThreads(script_threads);
    }

    assert(!node.scheduler);
    node.scheduler = std::make_unique<CScheduler>();

    // Start the lightweight task scheduler thread
    node.scheduler->m_service_thread = std::thread(util::TraceThread, "scheduler", [&] { node.scheduler->serviceQueue(); });

    // Gather some entropy once per minute.
    node.scheduler->scheduleEvery([]{
        RandAddPeriodic();
    }, std::chrono::minutes{1});

    GetMainSignals().RegisterBackgroundSignalScheduler(*node.scheduler);

    tableRPC.InitPlatformRestrictions();

    /* Register RPC commands regardless of -server setting so they will be
     * available in the GUI RPC console even if external calls are disabled.
     */
    RegisterAllCoreRPCCommands(tableRPC);
    for (const auto& client : node.chain_clients) {
        client->registerRpcs();
    }
#if ENABLE_ZMQ
    RegisterZMQRPCCommands(tableRPC);
#endif

    /* Start the RPC server already.  It will be started in "warmup" mode
     * and not really process calls already (but it will signify connections
     * that the server is there and will be ready later).  Warmup mode will
     * be disabled when initialisation is finished.
     */
    if (args.GetBoolArg("-server", false)) {
        uiInterface.InitMessage_connect(SetRPCWarmupStatus);
        if (!AppInitServers(context, node))
            return InitError(_("Unable to start HTTP server. See debug log for details."));
    }

    // ********************************************************* Step 5: verify wallet database integrity

    if (!g_wallet_init_interface.InitAutoBackup()) return false;
    for (const auto& client : node.chain_clients) {
        if (!client->verify()) {
            return false;
        }
    }

    // ********************************************************* Step 6: network initialization
    // Note that we absolutely cannot open any actual connections
    // until the very end ("start node") as the UTXO/block state
    // is not yet setup and may end up being set up twice if we
    // need to reindex later.

    fListen = args.GetBoolArg("-listen", DEFAULT_LISTEN);
    fDiscover = args.GetBoolArg("-discover", true);
    const bool ignores_incoming_txs{args.GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY)};

    {
        // Initialize addrman
        assert(!node.addrman);

        // Read asmap file if configured
        std::vector<bool> asmap;
        if (args.IsArgSet("-asmap")) {
            fs::path asmap_path = fs::path(args.GetArg("-asmap", ""));
            if (asmap_path.empty()) {
                asmap_path = DEFAULT_ASMAP_FILENAME;
            }
            if (!asmap_path.is_absolute()) {
                asmap_path = GetDataDir() / asmap_path;
            }
            if (!fs::exists(asmap_path)) {
                InitError(strprintf(_("Could not find asmap file %s"), asmap_path));
                return false;
            }
            asmap = DecodeAsmap(asmap_path);
            if (asmap.size() == 0) {
                InitError(strprintf(_("Could not parse asmap file %s"), asmap_path));
                return false;
            }
            const uint256 asmap_version = SerializeHash(asmap);
            LogPrintf("Using asmap version %s for IP bucketing\n", asmap_version.ToString());
        } else {
            LogPrintf("Using /16 prefix for IP bucketing\n");
        }

        uiInterface.InitMessage(_("Loading P2P addresses…").translated);
        if (const auto error{LoadAddrman(asmap, args, node.addrman)}) {
            return InitError(*error);
        }
    }

    assert(!node.banman);
    node.banman = std::make_unique<BanMan>(GetDataDir() / "banlist", &uiInterface, args.GetArg("-bantime", DEFAULT_MISBEHAVING_BANTIME));
    assert(!node.connman);
    node.connman = std::make_unique<CConnman>(GetRand(std::numeric_limits<uint64_t>::max()), GetRand(std::numeric_limits<uint64_t>::max()), *node.addrman, args.GetBoolArg("-networkactive", true));

    assert(!node.fee_estimator);
    // Don't initialize fee estimation with old data if we don't relay transactions,
    // as they would never get updated.
    if (!ignores_incoming_txs) node.fee_estimator = std::make_unique<CBlockPolicyEstimator>();

    assert(!node.mempool);
    int check_ratio = std::min<int>(std::max<int>(args.GetArg("-checkmempool", chainparams.DefaultConsistencyChecks() ? 1 : 0), 0), 1000000);
    node.mempool = std::make_unique<CTxMemPool>(node.fee_estimator.get(), check_ratio);

    assert(!node.chainman);
    node.chainman = std::make_unique<ChainstateManager>();
    ChainstateManager& chainman = *node.chainman;

    assert(!node.mn_metaman);
    node.mn_metaman = std::make_unique<CMasternodeMetaMan>();

    assert(!node.netfulfilledman);
    node.netfulfilledman = std::make_unique<CNetFulfilledRequestManager>();

    /**
     * The manager needs to be constructed regardless of whether governance
     * validation is needed or not.
     *
     * Instead, we decide whether to initialize its database based on whether we
     * need it or not further down and then query if the database is initialized
     * to check if validation is enabled.
     */
    const bool is_governance_enabled{!args.GetBoolArg("-disablegovernance", !DEFAULT_GOVERNANCE_ENABLE)};
    assert(!node.govman);
    node.govman = std::make_unique<CGovernanceManager>(*node.mn_metaman, *node.netfulfilledman, *node.chainman, node.dmnman, node.mn_sync, *node.sporkman);

    assert(!node.sporkman);
    node.sporkman = std::make_unique<CSporkManager>();

    std::vector<std::string> vSporkAddresses;
    if (args.IsArgSet("-sporkaddr")) {
        vSporkAddresses = args.GetArgs("-sporkaddr");
    } else {
        vSporkAddresses = Params().SporkAddresses();
    }
    for (const auto& address: vSporkAddresses) {
        if (!node.sporkman->SetSporkAddress(address)) {
            return InitError(_("Invalid spork address specified with -sporkaddr"));
        }
    }

    int minsporkkeys = args.GetArg("-minsporkkeys", Params().MinSporkKeys());
    if (!node.sporkman->SetMinSporkKeys(minsporkkeys)) {
        return InitError(_("Invalid minimum number of spork signers specified with -minsporkkeys"));
    }


    if (args.IsArgSet("-sporkkey")) { // spork priv key
        if (!node.sporkman->SetPrivKey(args.GetArg("-sporkkey", ""))) {
            return InitError(_("Unable to sign spork message, wrong key?"));
        }
    }

    assert(!node.mn_sync);
    node.mn_sync = std::make_unique<CMasternodeSync>(*node.connman, *node.netfulfilledman, *node.govman);

    fMasternodeMode = false;
    std::string strMasterNodeBLSPrivKey = args.GetArg("-masternodeblsprivkey", "");
    if (!strMasterNodeBLSPrivKey.empty()) {
        CBLSSecretKey keyOperator(ParseHex(strMasterNodeBLSPrivKey));
        if (!keyOperator.IsValid()) {
            return InitError(_("Invalid masternodeblsprivkey. Please see documentation."));
        }
        fMasternodeMode = true;
        {
            // Create and register mn_activeman, will init later in ThreadImport
            node.mn_activeman = std::make_unique<CActiveMasternodeManager>(keyOperator, *node.connman, node.dmnman);
            RegisterValidationInterface(node.mn_activeman.get());
        }
    }

    assert(!node.peerman);
    node.peerman = PeerManager::make(chainparams, *node.connman, *node.addrman, node.banman.get(),
                                     *node.scheduler, chainman, *node.mempool, *node.mn_metaman, *node.mn_sync,
                                     *node.govman, *node.sporkman, node.mn_activeman.get(), node.dmnman,
                                     node.cj_ctx, node.llmq_ctx, ignores_incoming_txs);
    RegisterValidationInterface(node.peerman.get());

    // sanitize comments per BIP-0014, format user agent and check total size
    std::vector<std::string> uacomments;

    if (chainparams.NetworkIDString() == CBaseChainParams::DEVNET) {
        // Add devnet name to user agent. This allows to disconnect nodes immediately if they don't belong to our own devnet
        uacomments.push_back(strprintf("devnet.%s", args.GetDevNetName()));
    }

    for (const std::string& cmt : args.GetArgs("-uacomment")) {
        if (cmt != SanitizeString(cmt, SAFE_CHARS_UA_COMMENT))
            return InitError(strprintf(_("User Agent comment (%s) contains unsafe characters."), cmt));
        uacomments.push_back(cmt);
    }
    strSubVersion = FormatSubVersion(CLIENT_NAME, CLIENT_VERSION, uacomments);
    if (strSubVersion.size() > MAX_SUBVERSION_LENGTH) {
        return InitError(strprintf(_("Total length of network version string (%i) exceeds maximum length (%i). Reduce the number or size of uacomments."),
            strSubVersion.size(), MAX_SUBVERSION_LENGTH));
    }

    if (args.IsArgSet("-onlynet")) {
        std::set<enum Network> nets;
        for (const std::string& snet : args.GetArgs("-onlynet")) {
            enum Network net = ParseNetwork(snet);
            if (net == NET_UNROUTABLE)
                return InitError(strprintf(_("Unknown network specified in -onlynet: '%s'"), snet));
            nets.insert(net);
        }
        for (int n = 0; n < NET_MAX; n++) {
            enum Network net = (enum Network)n;
            assert(IsReachable(net));
            if (!nets.count(net))
                SetReachable(net, false);
        }
    }

    if (!args.IsArgSet("-cjdnsreachable")) {
        SetReachable(NET_CJDNS, false);
    }
    // Now IsReachable(NET_CJDNS) is true if:
    // 1. -cjdnsreachable is given and
    // 2.1. -onlynet is not given or
    // 2.2. -onlynet=cjdns is given

    // Check for host lookup allowed before parsing any network related parameters
    fNameLookup = args.GetBoolArg("-dns", DEFAULT_NAME_LOOKUP);

    Proxy onion_proxy;

    bool proxyRandomize = args.GetBoolArg("-proxyrandomize", DEFAULT_PROXYRANDOMIZE);
    // -proxy sets a proxy for all outgoing network traffic
    // -noproxy (or -proxy=0) as well as the empty string can be used to not set a proxy, this is the default
    std::string proxyArg = args.GetArg("-proxy", "");
    if (proxyArg != "" && proxyArg != "0") {
        CService proxyAddr;
        if (!Lookup(proxyArg, proxyAddr, 9050, fNameLookup)) {
            return InitError(strprintf(_("Invalid -proxy address or hostname: '%s'"), proxyArg));
        }

        Proxy addrProxy = Proxy(proxyAddr, proxyRandomize);
        if (!addrProxy.IsValid())
            return InitError(strprintf(_("Invalid -proxy address or hostname: '%s'"), proxyArg));

        SetProxy(NET_IPV4, addrProxy);
        SetProxy(NET_IPV6, addrProxy);
        SetProxy(NET_CJDNS, addrProxy);
        SetNameProxy(addrProxy);
        onion_proxy = addrProxy;
    }

    const bool onlynet_used_with_onion{args.IsArgSet("-onlynet") && IsReachable(NET_ONION)};

    // -onion can be used to set only a proxy for .onion, or override normal proxy for .onion addresses
    // -noonion (or -onion=0) disables connecting to .onion entirely
    // An empty string is used to not override the onion proxy (in which case it defaults to -proxy set above, or none)
    std::string onionArg = args.GetArg("-onion", "");
    if (onionArg != "") {
        if (onionArg == "0") { // Handle -noonion/-onion=0
            onion_proxy = Proxy{};
            if (onlynet_used_with_onion) {
                return InitError(
                    _("Outbound connections restricted to Tor (-onlynet=onion) but the proxy for "
                      "reaching the Tor network is explicitly forbidden: -onion=0"));
            }
        } else {
            CService addr;
            if (!Lookup(onionArg, addr, 9050, fNameLookup) || !addr.IsValid()) {
                return InitError(strprintf(_("Invalid -onion address or hostname: '%s'"), onionArg));
            }
            onion_proxy = Proxy{addr, proxyRandomize};
        }
    }

    if (onion_proxy.IsValid()) {
        SetProxy(NET_ONION, onion_proxy);
    } else {
        // If -listenonion is set, then we will (try to) connect to the Tor control port
        // later from the torcontrol thread and may retrieve the onion proxy from there.
        const bool listenonion_disabled{!args.GetBoolArg("-listenonion", DEFAULT_LISTEN_ONION)};
        if (onlynet_used_with_onion && listenonion_disabled) {
            return InitError(
                _("Outbound connections restricted to Tor (-onlynet=onion) but the proxy for "
                  "reaching the Tor network is not provided: none of -proxy, -onion or "
                  "-listenonion is given"));
        }
        SetReachable(NET_ONION, false);
    }

    for (const std::string& strAddr : args.GetArgs("-externalip")) {
        CService addrLocal;
        if (Lookup(strAddr, addrLocal, GetListenPort(), fNameLookup) && addrLocal.IsValid())
            AddLocal(addrLocal, LOCAL_MANUAL);
        else
            return InitError(ResolveErrMsg("externalip", strAddr));
    }

#if ENABLE_ZMQ
    g_zmq_notification_interface = CZMQNotificationInterface::Create();

    if (g_zmq_notification_interface) {
        RegisterValidationInterface(g_zmq_notification_interface);
    }
#endif

    pdsNotificationInterface = new CDSNotificationInterface(
        *node.connman, *node.mn_sync, *node.govman, *node.peerman, chainman, node.mn_activeman.get(), node.dmnman, node.llmq_ctx, node.cj_ctx, *node.sporkman
    );
    RegisterValidationInterface(pdsNotificationInterface);

    // ********************************************************* Step 7a: Load sporks

    if (!node.sporkman->LoadCache()) {
        auto file_path = (GetDataDir() / "sporks.dat").string();
        return InitError(strprintf(_("Failed to load sporks cache from %s"), file_path));
    }

    // ********************************************************* Step 7b: load block chain

    fReindex = args.GetBoolArg("-reindex", false);
    bool fReindexChainState = args.GetBoolArg("-reindex-chainstate", false);

    // cache size calculations
    int64_t nTotalCache = (args.GetArg("-dbcache", nDefaultDbCache) << 20);
    nTotalCache = std::max(nTotalCache, nMinDbCache << 20); // total cache cannot be less than nMinDbCache
    nTotalCache = std::min(nTotalCache, nMaxDbCache << 20); // total cache cannot be greater than nMaxDbcache
    int64_t nBlockTreeDBCache = std::min(nTotalCache / 8, nMaxBlockDBCache << 20);
    nTotalCache -= nBlockTreeDBCache;
    int64_t nTxIndexCache = std::min(nTotalCache / 8, args.GetBoolArg("-txindex", DEFAULT_TXINDEX) ? nMaxTxIndexCache << 20 : 0);
    nTotalCache -= nTxIndexCache;
    int64_t filter_index_cache = 0;
    if (!g_enabled_filter_types.empty()) {
        size_t n_indexes = g_enabled_filter_types.size();
        int64_t max_cache = std::min(nTotalCache / 8, max_filter_index_cache << 20);
        filter_index_cache = max_cache / n_indexes;
        nTotalCache -= filter_index_cache * n_indexes;
    }
    int64_t nCoinDBCache = std::min(nTotalCache / 2, (nTotalCache / 4) + (1 << 23)); // use 25%-50% of the remainder for disk cache
    nCoinDBCache = std::min(nCoinDBCache, nMaxCoinsDBCache << 20); // cap total coins db cache
    nTotalCache -= nCoinDBCache;
    int64_t nCoinCacheUsage = nTotalCache; // the rest goes to in-memory cache
    int64_t nMempoolSizeMax = args.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    int64_t nEvoDbCache = 1024 * 1024 * 64; // TODO
    LogPrintf("Cache configuration:\n");
    LogPrintf("* Using %.1f MiB for block index database\n", nBlockTreeDBCache * (1.0 / 1024 / 1024));
    if (args.GetBoolArg("-txindex", DEFAULT_TXINDEX)) {
        LogPrintf("* Using %.1f MiB for transaction index database\n", nTxIndexCache * (1.0 / 1024 / 1024));
    }
    for (BlockFilterType filter_type : g_enabled_filter_types) {
        LogPrintf("* Using %.1f MiB for %s block filter index database\n",
                  filter_index_cache * (1.0 / 1024 / 1024), BlockFilterTypeName(filter_type));
    }
    LogPrintf("* Using %.1f MiB for chain state database\n", nCoinDBCache * (1.0 / 1024 / 1024));
    LogPrintf("* Using %.1f MiB for in-memory UTXO set (plus up to %.1f MiB of unused mempool space)\n", nCoinCacheUsage * (1.0 / 1024 / 1024), nMempoolSizeMax * (1.0 / 1024 / 1024));

    bool fLoaded = false;

    while (!fLoaded && !ShutdownRequested()) {
        const bool fReset = fReindex;
        auto is_coinsview_empty = [&](CChainState* chainstate) EXCLUSIVE_LOCKS_REQUIRED(::cs_main) {
            return fReset || fReindexChainState || chainstate->CoinsTip().GetBestBlock().IsNull();
        };
        bilingual_str strLoadError;

        uiInterface.InitMessage(_("Loading block index…").translated);

        do {
            bool failed_verification = false;
            const int64_t load_block_index_start_time = GetTimeMillis();

            try {
                LOCK(cs_main);
                node.evodb.reset();
                node.evodb = std::make_unique<CEvoDB>(nEvoDbCache, false, fReset || fReindexChainState);
                node.mnhf_manager.reset();
                node.mnhf_manager = std::make_unique<CMNHFManager>(*node.evodb);


                chainman.Reset();
                chainman.InitializeChainstate(Assert(node.mempool.get()), *node.evodb, node.chain_helper, llmq::chainLocksHandler, llmq::quorumInstantSendManager, *node.sporkman);
                chainman.m_total_coinstip_cache = nCoinCacheUsage;
                chainman.m_total_coinsdb_cache = nCoinDBCache;

                UnloadBlockIndex(node.mempool.get(), chainman);

                // new CBlockTreeDB tries to delete the existing file, which
                // fails if it's still open from the previous loop. Close it first:
                pblocktree.reset();
                pblocktree.reset(new CBlockTreeDB(nBlockTreeDBCache, false, fReset));

                // Same logic as above with pblocktree
                node.dmnman.reset();
                node.dmnman = std::make_unique<CDeterministicMNManager>(chainman.ActiveChainstate(), *node.connman, *node.evodb);
                node.mempool->ConnectManagers(node.dmnman.get());

                node.cpoolman.reset();
                node.cpoolman = std::make_unique<CCreditPoolManager>(*node.evodb);

                llmq::quorumSnapshotManager.reset();
                llmq::quorumSnapshotManager.reset(new llmq::CQuorumSnapshotManager(*node.evodb));

                if (node.llmq_ctx) {
                    node.llmq_ctx->Interrupt();
                    node.llmq_ctx->Stop();
                }
                node.llmq_ctx.reset();
                node.llmq_ctx = std::make_unique<LLMQContext>(chainman.ActiveChainstate(), *node.connman, *node.dmnman, *node.evodb, *node.mn_metaman, *node.mnhf_manager, *node.sporkman,
                                                              *node.mempool, node.mn_activeman.get(), *node.mn_sync, node.peerman, /* unit_tests = */ false, /* wipe = */ fReset || fReindexChainState);
                // Enable CMNHFManager::{Process, Undo}Block
                node.mnhf_manager->ConnectManagers(node.chainman.get(), node.llmq_ctx->qman.get());
                // Have to start it early to let VerifyDB check ChainLock signatures in coinbase
                node.llmq_ctx->Start();

                node.chain_helper.reset();
                node.chain_helper = std::make_unique<CChainstateHelper>(*node.cpoolman, *node.dmnman, *node.mnhf_manager, *node.govman, *(node.llmq_ctx->quorum_block_processor), *node.chainman,
                                                                        chainparams.GetConsensus(), *node.mn_sync, *node.sporkman, *(node.llmq_ctx->clhandler), *(node.llmq_ctx->qman));

                if (fReset) {
                    pblocktree->WriteReindexing(true);
                    //If we're reindexing in prune mode, wipe away unusable block files and all undo data files
                    if (fPruneMode)
                        CleanupBlockRevFiles();
                }

                if (ShutdownRequested()) break;

                // LoadBlockIndex will load fHavePruned if we've ever removed a
                // block file from disk.
                // Note that it also sets fReindex based on the disk flag!
                // From here on out fReindex and fReset mean something different!
                if (!chainman.LoadBlockIndex()) {
                    if (ShutdownRequested()) break;
                    strLoadError = _("Error loading block database");
                    break;
                }

                if (is_governance_enabled && !args.GetBoolArg("-txindex", DEFAULT_TXINDEX) && chainparams.NetworkIDString() != CBaseChainParams::REGTEST) { // TODO remove this when pruning is fixed. See https://github.com/sparkspay/sparks/pull/1817 and https://github.com/sparkspay/sparks/pull/1743
                    return InitError(_("Transaction index can't be disabled with governance validation enabled. Either start with -disablegovernance command line switch or enable transaction index."));
                }

                // If the loaded chain has a wrong genesis, bail out immediately
                // (we're likely using a testnet datadir, or the other way around).
                if (!chainman.BlockIndex().empty() &&
                        !chainman.m_blockman.LookupBlockIndex(chainparams.GetConsensus().hashGenesisBlock)) {
                    return InitError(_("Incorrect or no genesis block found. Wrong datadir for network?"));
                }

                if (!chainparams.GetConsensus().hashDevnetGenesisBlock.IsNull() && !chainman.BlockIndex().empty() &&
                        !chainman.m_blockman.LookupBlockIndex(chainparams.GetConsensus().hashDevnetGenesisBlock)) {
                    return InitError(_("Incorrect or no devnet genesis block found. Wrong datadir for devnet specified?"));
                }

                // Check for changed -addressindex state
                if (fAddressIndex != args.GetBoolArg("-addressindex", DEFAULT_ADDRESSINDEX)) {
                    strLoadError = _("You need to rebuild the database using -reindex to change -addressindex");
                    break;
                }

                // Check for changed -timestampindex state
                if (fTimestampIndex != args.GetBoolArg("-timestampindex", DEFAULT_TIMESTAMPINDEX)) {
                    strLoadError = _("You need to rebuild the database using -reindex to change -timestampindex");
                    break;
                }

                // Check for changed -spentindex state
                if (fSpentIndex != args.GetBoolArg("-spentindex", DEFAULT_SPENTINDEX)) {
                    strLoadError = _("You need to rebuild the database using -reindex to change -spentindex");
                    break;
                }

                // Check for changed -prune state.  What we are concerned about is a user who has pruned blocks
                // in the past, but is now trying to run unpruned.
                if (fHavePruned && !fPruneMode) {
                    strLoadError = _("You need to rebuild the database using -reindex to go back to unpruned mode.  This will redownload the entire blockchain");
                    break;
                }

                // At this point blocktree args are consistent with what's on disk.
                // If we're not mid-reindex (based on disk + args), add a genesis block on disk
                // (otherwise we use the one already on disk).
                // This is called again in ThreadImport after the reindex completes.
                if (!fReindex && !chainman.ActiveChainstate().LoadGenesisBlock()) {
                    strLoadError = _("Error initializing block database");
                    break;
                }

                // At this point we're either in reindex or we've loaded a useful
                // block tree into BlockIndex()!

                bool failed_chainstate_init = false;
                for (CChainState* chainstate : chainman.GetAll()) {
                    chainstate->InitCoinsDB(
                        /* cache_size_bytes */ nCoinDBCache,
                        /* in_memory */ false,
                        /* should_wipe */ fReset || fReindexChainState);

                    chainstate->CoinsErrorCatcher().AddReadErrCallback([]() {
                        uiInterface.ThreadSafeMessageBox(
                            _("Error reading from database, shutting down."),
                            "", CClientUIInterface::MSG_ERROR);
                    });

                    // If necessary, upgrade from older database format.
                    // This is a no-op if we cleared the coinsviewdb with -reindex or -reindex-chainstate
                    if (!chainstate->CoinsDB().Upgrade()) {
                        strLoadError = _("Error upgrading chainstate database");
                        failed_chainstate_init = true;
                        break;
                    }

                    // ReplayBlocks is a no-op if we cleared the coinsviewdb with -reindex or -reindex-chainstate
                    if (!chainstate->ReplayBlocks()) {
                        strLoadError = _("Unable to replay blocks. You will need to rebuild the database using -reindex-chainstate.");
                        failed_chainstate_init = true;
                        break;
                    }

                    // The on-disk coinsdb is now in a good state, create the cache
                    chainstate->InitCoinsCache(nCoinCacheUsage);
                    assert(chainstate->CanFlushToDisk());

                    // flush evodb
                    // TODO: CEvoDB instance should probably be a part of CChainState
                    // (for multiple chainstates to actually work in parallel)
                    // and not a global
                    if (&chainman.ActiveChainstate() == chainstate && !node.evodb->CommitRootTransaction()) {
                        strLoadError = _("Failed to commit EvoDB");
                        failed_chainstate_init = true;
                        break;
                    }

                    if (!is_coinsview_empty(chainstate)) {
                        // LoadChainTip initializes the chain based on CoinsTip()'s best block
                        if (!chainstate->LoadChainTip()) {
                            strLoadError = _("Error initializing block database");
                            failed_chainstate_init = true;
                            break; // out of the per-chainstate loop
                        }
                        assert(chainstate->m_chain.Tip() != nullptr);
                    }
                }

                if (failed_chainstate_init) {
                    break; // out of the chainstate activation do-while
                }

                if (!node.dmnman->MigrateDBIfNeeded()) {
                    strLoadError = _("Error upgrading evo database");
                    break;
                }
                if (!node.dmnman->MigrateDBIfNeeded2()) {
                    strLoadError = _("Error upgrading evo database");
                    break;
                }

                for (CChainState* chainstate : chainman.GetAll()) {
                    if (!is_coinsview_empty(chainstate)) {
                        uiInterface.InitMessage(_("Verifying blocks…").translated);
                        if (fHavePruned && args.GetArg("-checkblocks", DEFAULT_CHECKBLOCKS) > MIN_BLOCKS_TO_KEEP) {
                            LogPrintf("Prune: pruned datadir may not have more than %d blocks; only checking available blocks\n",
                                MIN_BLOCKS_TO_KEEP);
                        }

                        const CBlockIndex* tip = chainstate->m_chain.Tip();
                        RPCNotifyBlockChange(tip);
                        if (tip && tip->nTime > GetAdjustedTime() + 2 * 60 * 60) {
                            strLoadError = _("The block database contains a block which appears to be from the future. "
                                    "This may be due to your computer's date and time being set incorrectly. "
                                    "Only rebuild the block database if you are sure that your computer's date and time are correct");
                            failed_verification = true;
                            break;
                        }
                        const bool v19active{DeploymentActiveAfter(tip, chainparams.GetConsensus(), Consensus::DEPLOYMENT_V19)};
                        if (v19active) {
                            bls::bls_legacy_scheme.store(false);
                            LogPrintf("%s: bls_legacy_scheme=%d\n", __func__, bls::bls_legacy_scheme.load());
                        }

                        if (!CVerifyDB().VerifyDB(
                                *chainstate, chainparams, chainstate->CoinsDB(),
                                *node.evodb,
                                args.GetArg("-checklevel", DEFAULT_CHECKLEVEL),
                                args.GetArg("-checkblocks", DEFAULT_CHECKBLOCKS), *node.sporkman)) {
                            strLoadError = _("Corrupted block database detected");
                            failed_verification = true;
                            break;
                        }

                        // VerifyDB() disconnects blocks which might result in us switching back to legacy.
                        // Make sure we use the right scheme.
                        if (v19active && bls::bls_legacy_scheme.load()) {
                            bls::bls_legacy_scheme.store(false);
                            LogPrintf("%s: bls_legacy_scheme=%d\n", __func__, bls::bls_legacy_scheme.load());
                        }

                        if (args.GetArg("-checklevel", DEFAULT_CHECKLEVEL) >= 3) {
                            chainstate->ResetBlockFailureFlags(nullptr);
                        }

                    } else {
                        // TODO: CEvoDB instance should probably be a part of CChainState
                        // (for multiple chainstates to actually work in parallel)
                        // and not a global
                        if (&chainman.ActiveChainstate() == chainstate && !node.evodb->IsEmpty()) {
                            // EvoDB processed some blocks earlier but we have no blocks anymore, something is wrong
                            strLoadError = _("Error initializing block database");
                            failed_verification = true;
                            break;
                        }
                    }
                }
            } catch (const std::exception& e) {
                LogPrintf("%s\n", e.what());
                strLoadError = _("Error opening block database");
                failed_verification = true;
                break;
            }

            if (!failed_verification) {
                fLoaded = true;
                LogPrintf(" block index %15dms\n", GetTimeMillis() - load_block_index_start_time);
            }
        } while(false);

        if (!fLoaded && !ShutdownRequested()) {
            // first suggest a reindex
            if (!fReset) {
                bool fRet = uiInterface.ThreadSafeQuestion(
                    strLoadError + Untranslated(".\n\n") + _("Do you want to rebuild the block database now?"),
                    strLoadError.original + ".\nPlease restart with -reindex or -reindex-chainstate to recover.",
                    "", CClientUIInterface::MSG_ERROR | CClientUIInterface::BTN_ABORT);
                if (fRet) {
                    fReindex = true;
                    AbortShutdown();
                } else {
                    LogPrintf("Aborted block database rebuild. Exiting.\n");
                    return false;
                }
            } else {
                return InitError(strLoadError);
            }
        }
    }

    // As LoadBlockIndex can take several minutes, it's possible the user
    // requested to kill the GUI during the last operation. If so, exit.
    // As the program has not fully started yet, Shutdown() is possibly overkill.
    if (ShutdownRequested()) {
        LogPrintf("Shutdown requested. Exiting.\n");
        return false;
    }

    // ********************************************************* Step 7c: Setup CoinJoin

    node.cj_ctx = std::make_unique<CJContext>(chainman.ActiveChainstate(), *node.connman, *node.dmnman, *node.mn_metaman, *node.mempool,
                                              node.mn_activeman.get(), *node.sporkman, *node.mn_sync, node.peerman, !ignores_incoming_txs);

#ifdef ENABLE_WALLET
    node.coinjoin_loader = interfaces::MakeCoinJoinLoader(*node.cj_ctx->walletman);
    g_wallet_init_interface.InitCoinJoinSettings(*node.cj_ctx->walletman);
#endif // ENABLE_WALLET

    // ********************************************************* Step 7d: Setup other Dash services

    bool fLoadCacheFiles = !(fReindex || fReindexChainState) && (chainman.ActiveChain().Tip() != nullptr);

    if (!node.netfulfilledman->LoadCache(fLoadCacheFiles)) {
        auto file_path = (GetDataDir() / "netfulfilled.dat").string();
        if (fLoadCacheFiles) {
            return InitError(strprintf(_("Failed to load fulfilled requests cache from %s"), file_path));
        }
        return InitError(strprintf(_("Failed to clear fulfilled requests cache at %s"), file_path));
    }

    if (!node.mn_metaman->LoadCache(fLoadCacheFiles)) {
        auto file_path = (GetDataDir() / "mncache.dat").string();
        if (fLoadCacheFiles) {
            return InitError(strprintf(_("Failed to load masternode cache from %s"), file_path));
        }
        return InitError(strprintf(_("Failed to clear masternode cache at %s"), file_path));
    }

    if (is_governance_enabled) {
        if (!node.govman->LoadCache(fLoadCacheFiles)) {
            auto file_path = (GetDataDir() / "governance.dat").string();
            if (fLoadCacheFiles) {
                return InitError(strprintf(_("Failed to load governance cache from %s"), file_path));
            }
            return InitError(strprintf(_("Failed to clear governance cache at %s"), file_path));
        }
    }

    // ********************************************************* Step 8: start indexers
    if (args.GetBoolArg("-txindex", DEFAULT_TXINDEX)) {
        g_txindex = std::make_unique<TxIndex>(nTxIndexCache, false, fReindex);
        if (!g_txindex->Start(chainman.ActiveChainstate())) {
            return false;
        }
    }

    for (const auto& filter_type : g_enabled_filter_types) {
        InitBlockFilterIndex(filter_type, filter_index_cache, false, fReindex);
        if (!GetBlockFilterIndex(filter_type)->Start(chainman.ActiveChainstate())) {
            return false;
        }
    }

    if (args.GetBoolArg("-coinstatsindex", DEFAULT_COINSTATSINDEX)) {
        g_coin_stats_index = std::make_unique<CoinStatsIndex>(/* cache size */ 0, *node.sporkman, false, fReindex);
        if (!g_coin_stats_index->Start(chainman.ActiveChainstate())) {
            return false;
        }
    }

    // ********************************************************* Step 9: load wallet
    for (const auto& client : node.chain_clients) {
        if (!client->load()) {
            return false;
        }
    }

    // As InitLoadWallet can take several minutes, it's possible the user
    // requested to kill the GUI during the last operation. If so, exit.
    if (ShutdownRequested())
    {
        LogPrintf("Shutdown requested. Exiting.\n");
        return false;
    }
    // ********************************************************* Step 10: data directory maintenance


    // if pruning, unset the service bit and perform the initial blockstore prune
    // after any wallet rescanning has taken place.
    if (fPruneMode) {
        LogPrintf("Unsetting NODE_NETWORK on prune mode\n");
        nLocalServices = ServiceFlags(nLocalServices & ~NODE_NETWORK);
        if (!fReindex) {
            LOCK(cs_main);
            for (CChainState* chainstate : chainman.GetAll()) {
                uiInterface.InitMessage(_("Pruning blockstore…").translated);
                chainstate->PruneAndFlush();
            }
        }
    }

    // As PruneAndFlush can take several minutes, it's possible the user
    // requested to kill the GUI during the last operation. If so, exit.
    if (ShutdownRequested())
    {
        LogPrintf("Shutdown requested. Exiting.\n");
        return false;
    }

    // ********************************************************* Step 10a: schedule Sparks-specific tasks

    node.scheduler->scheduleEvery(std::bind(&CNetFulfilledRequestManager::DoMaintenance, std::ref(*node.netfulfilledman)), std::chrono::minutes{1});
    node.scheduler->scheduleEvery(std::bind(&CMasternodeSync::DoMaintenance, std::ref(*node.mn_sync)), std::chrono::seconds{1});
    node.scheduler->scheduleEvery(std::bind(&CMasternodeUtils::DoMaintenance, std::ref(*node.connman), std::ref(*node.dmnman), std::ref(*node.mn_sync), std::ref(*node.cj_ctx)), std::chrono::minutes{1});
    node.scheduler->scheduleEvery(std::bind(&CDeterministicMNManager::DoMaintenance, std::ref(*node.dmnman)), std::chrono::seconds{10});

    if (node.govman->IsValid()) {
        node.scheduler->scheduleEvery(std::bind(&CGovernanceManager::DoMaintenance, std::ref(*node.govman), std::ref(*node.connman)), std::chrono::minutes{5});
    }

    if (node.mn_activeman) {
        node.scheduler->scheduleEvery(std::bind(&CCoinJoinServer::DoMaintenance, std::ref(*node.cj_ctx->server)), std::chrono::seconds{1});
        node.scheduler->scheduleEvery(std::bind(&llmq::CDKGSessionManager::CleanupOldContributions, std::ref(*node.llmq_ctx->qdkgsman)), std::chrono::hours{1});
#ifdef ENABLE_WALLET
    } else if (!ignores_incoming_txs) {
        node.scheduler->scheduleEvery(std::bind(&CCoinJoinClientQueueManager::DoMaintenance, std::ref(*node.cj_ctx->queueman)), std::chrono::seconds{1});
        node.scheduler->scheduleEvery(std::bind(&CoinJoinWalletManager::DoMaintenance, std::ref(*node.cj_ctx->walletman)), std::chrono::seconds{1});
#endif // ENABLE_WALLET
    }

    if (args.GetBoolArg("-statsenabled", DEFAULT_STATSD_ENABLE)) {
        int nStatsPeriod = std::min(std::max((int)args.GetArg("-statsperiod", DEFAULT_STATSD_PERIOD), MIN_STATSD_PERIOD), MAX_STATSD_PERIOD);
        node.scheduler->scheduleEvery(std::bind(&PeriodicStats, std::ref(*node.args), std::ref(chainman), std::cref(*node.mempool)), std::chrono::seconds{nStatsPeriod});
    }

    // ********************************************************* Step 11: import blocks

    if (!CheckDiskSpace(GetDataDir())) {
        InitError(strprintf(_("Error: Disk space is low for %s"), GetDataDir()));
        return false;
    }
    if (!CheckDiskSpace(gArgs.GetBlocksDirPath())) {
        InitError(strprintf(_("Error: Disk space is low for %s"), gArgs.GetBlocksDirPath()));
        return false;
    }

    // Either install a handler to notify us when genesis activates, or set fHaveGenesis directly.
    // No locking, as this happens before any background thread is started.
    boost::signals2::connection block_notify_genesis_wait_connection;
    if (chainman.ActiveChain().Tip() == nullptr) {
        block_notify_genesis_wait_connection = uiInterface.NotifyBlockTip_connect(std::bind(BlockNotifyGenesisWait, std::placeholders::_2));
    } else {
        fHaveGenesis = true;
    }

#if HAVE_SYSTEM
    const std::string block_notify = args.GetArg("-blocknotify", "");
    if (!block_notify.empty()) {
        uiInterface.NotifyBlockTip_connect([block_notify](SynchronizationState sync_state, const CBlockIndex* pBlockIndex) {
            if (sync_state != SynchronizationState::POST_INIT || !pBlockIndex) return;
            std::string command = block_notify;
            ReplaceAll(command, "%s", pBlockIndex->GetBlockHash().GetHex());
            std::thread t(runCommand, command);
            t.detach(); // thread runs free
        });
    }
    const std::string chainlock_notify = args.GetArg("-chainlocknotify", "");
    if (!chainlock_notify.empty()) {
        uiInterface.NotifyChainLock_connect([chainlock_notify](const std::string& bestChainLockHash, int bestChainLockHeight) {
            std::string command = chainlock_notify;
            ReplaceAll(command, "%s", bestChainLockHash);
            std::thread t(runCommand, command);
            t.detach(); // thread runs free
        });
    }
#endif

    std::vector<fs::path> vImportFiles;
    for (const std::string& strFile : args.GetArgs("-loadblock")) {
        vImportFiles.push_back(strFile);
    }

    chainman.m_load_block = std::thread(&util::TraceThread, "loadblk", [=, &args, &chainman, &node] {
        ThreadImport(chainman, *node.dmnman, *pdsNotificationInterface, vImportFiles, node.mn_activeman.get(), args, *node.sporkman);
    });

    // Wait for genesis block to be processed
    {
        WAIT_LOCK(g_genesis_wait_mutex, lock);
        // We previously could hang here if StartShutdown() is called prior to
        // ThreadImport getting started, so instead we just wait on a timer to
        // check ShutdownRequested() regularly.
        while (!fHaveGenesis && !ShutdownRequested()) {
            g_genesis_wait_cv.wait_for(lock, std::chrono::milliseconds(500));
        }
        block_notify_genesis_wait_connection.disconnect();
    }

    // As importing blocks can take several minutes, it's possible the user
    // requested to kill the GUI during one of the last operations. If so, exit.
    if (ShutdownRequested()) {
        LogPrintf("Shutdown requested. Exiting.\n");
        return false;
    }

    // ********************************************************* Step 12: start node

    int chain_active_height;

    //// debug print
    {
        LOCK(cs_main);
        LogPrintf("block tree size = %u\n", chainman.BlockIndex().size());
        chain_active_height = chainman.ActiveChain().Height();
        if (tip_info) {
            tip_info->block_height = chain_active_height;
            tip_info->block_time = chainman.ActiveChain().Tip() ? chainman.ActiveChain().Tip()->GetBlockTime() : Params().GenesisBlock().GetBlockTime();
            tip_info->block_hash = chainman.ActiveChain().Tip() ? chainman.ActiveChain().Tip()->GetBlockHash() : Params().GenesisBlock().GetHash();
            tip_info->verification_progress = GuessVerificationProgress(Params().TxData(), chainman.ActiveChain().Tip());
        }
        if (tip_info && ::pindexBestHeader) {
            tip_info->header_height = ::pindexBestHeader->nHeight;
            tip_info->header_time = ::pindexBestHeader->GetBlockTime();
        }
    }
    LogPrintf("nBestHeight = %d\n", chain_active_height);
    if (node.peerman) node.peerman->SetBestHeight(chain_active_height);

    // Map ports with UPnP or NAT-PMP.
    StartMapPort(args.GetBoolArg("-upnp", DEFAULT_UPNP), args.GetBoolArg("-natpmp", DEFAULT_NATPMP));

    CConnman::Options connOptions;
    connOptions.nLocalServices = nLocalServices;
    connOptions.nMaxConnections = nMaxConnections;
    connOptions.m_max_outbound_full_relay = std::min(MAX_OUTBOUND_FULL_RELAY_CONNECTIONS, connOptions.nMaxConnections);
    connOptions.m_max_outbound_block_relay = std::min(MAX_BLOCK_RELAY_ONLY_CONNECTIONS, connOptions.nMaxConnections-connOptions.m_max_outbound_full_relay);
    connOptions.nMaxAddnode = MAX_ADDNODE_CONNECTIONS;
    connOptions.nMaxFeeler = MAX_FEELER_CONNECTIONS;
    connOptions.uiInterface = &uiInterface;
    connOptions.m_banman = node.banman.get();
    connOptions.m_msgproc = node.peerman.get();
    connOptions.nSendBufferMaxSize = 1000 * args.GetArg("-maxsendbuffer", DEFAULT_MAXSENDBUFFER);
    connOptions.nReceiveFloodSize = 1000 * args.GetArg("-maxreceivebuffer", DEFAULT_MAXRECEIVEBUFFER);
    connOptions.m_added_nodes = args.GetArgs("-addnode");
    connOptions.nMaxOutboundLimit = 1024 * 1024 * args.GetArg("-maxuploadtarget", DEFAULT_MAX_UPLOAD_TARGET);
    connOptions.m_peer_connect_timeout = peer_connect_timeout;

    // Port to bind to if `-bind=addr` is provided without a `:port` suffix.
    const uint16_t default_bind_port =
        static_cast<uint16_t>(args.GetArg("-port", Params().GetDefaultPort()));

    for (const std::string& bind_arg : args.GetArgs("-bind")) {
        CService bind_addr;
        const size_t index = bind_arg.rfind('=');
        if (index == std::string::npos) {
            if (Lookup(bind_arg, bind_addr, default_bind_port, /*fAllowLookup=*/false)) {
                connOptions.vBinds.push_back(bind_addr);
                continue;
            }
        } else {
            const std::string network_type = bind_arg.substr(index + 1);
            if (network_type == "onion") {
                const std::string truncated_bind_arg = bind_arg.substr(0, index);
                if (Lookup(truncated_bind_arg, bind_addr, BaseParams().OnionServiceTargetPort(), false)) {
                    connOptions.onion_binds.push_back(bind_addr);
                    continue;
                }
            }
        }
        return InitError(ResolveErrMsg("bind", bind_arg));
    }

    for (const std::string& strBind : args.GetArgs("-whitebind")) {
        NetWhitebindPermissions whitebind;
        bilingual_str error;
        if (!NetWhitebindPermissions::TryParse(strBind, whitebind, error)) return InitError(error);
        connOptions.vWhiteBinds.push_back(whitebind);
    }

    // If the user did not specify -bind= or -whitebind= then we bind
    // on any address - 0.0.0.0 (IPv4) and :: (IPv6).
    connOptions.bind_on_any = args.GetArgs("-bind").empty() && args.GetArgs("-whitebind").empty();

    CService onion_service_target;
    if (!connOptions.onion_binds.empty()) {
        onion_service_target = connOptions.onion_binds.front();
    } else {
        onion_service_target = DefaultOnionServiceTarget();
        connOptions.onion_binds.push_back(onion_service_target);
    }

    if (args.GetBoolArg("-listenonion", DEFAULT_LISTEN_ONION)) {
        if (connOptions.onion_binds.size() > 1) {
            InitWarning(strprintf(_("More than one onion bind address is provided. Using %s "
                                    "for the automatically created Tor onion service."),
                                  onion_service_target.ToStringIPPort()));
        }
        StartTorControl(onion_service_target);
    }

    if (connOptions.bind_on_any) {
        // Only add all IP addresses of the machine if we would be listening on
        // any address - 0.0.0.0 (IPv4) and :: (IPv6).
        Discover();
    }

    for (const auto& net : args.GetArgs("-whitelist")) {
        NetWhitelistPermissions subnet;
        bilingual_str error;
        if (!NetWhitelistPermissions::TryParse(net, subnet, error)) return InitError(error);
        connOptions.vWhitelistedRange.push_back(subnet);
    }

    connOptions.vSeedNodes = args.GetArgs("-seednode");

    // Initiate outbound connections unless connect=0
    connOptions.m_use_addrman_outgoing = !args.IsArgSet("-connect");
    if (!connOptions.m_use_addrman_outgoing) {
        const auto connect = args.GetArgs("-connect");
        if (connect.size() != 1 || connect[0] != "0") {
            connOptions.m_specified_outgoing = connect;
        }
    }

    std::string sem_str = args.GetArg("-socketevents", DEFAULT_SOCKETEVENTS);
    const auto sem = SEMFromString(sem_str);
    if (sem == SocketEventsMode::Unknown) {
        return InitError(strprintf(_("Invalid -socketevents ('%s') specified. Only these modes are supported: %s"), sem_str, GetSupportedSocketEventsStr()));
    }
    connOptions.socketEventsMode = sem;

    const std::string& i2psam_arg = args.GetArg("-i2psam", "");
    if (!i2psam_arg.empty()) {
        CService addr;
        if (!Lookup(i2psam_arg, addr, 7656, fNameLookup) || !addr.IsValid()) {
            return InitError(strprintf(_("Invalid -i2psam address or hostname: '%s'"), i2psam_arg));
        }
        SetProxy(NET_I2P, Proxy{addr});
    } else {
        SetReachable(NET_I2P, false);
    }

    connOptions.m_i2p_accept_incoming = args.GetBoolArg("-i2pacceptincoming", DEFAULT_I2P_ACCEPT_INCOMING);

    if (!node.connman->Start(*node.dmnman, *node.mn_metaman, *node.mn_sync, *node.scheduler, connOptions)) {
        return false;
    }

    // ********************************************************* Step 13: finished

    SetRPCWarmupFinished();
    uiInterface.InitMessage(_("Done loading").translated);

    for (const auto& client : node.chain_clients) {
        client->start(*node.scheduler);
    }

    BanMan* banman = node.banman.get();
    node.scheduler->scheduleEvery([banman]{
        banman->DumpBanlist();
    }, DUMP_BANS_INTERVAL);

#if HAVE_SYSTEM
    StartupNotify(args);
#endif

    return true;
}