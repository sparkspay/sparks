// Copyright (c) 2011-2020 The Bitcoin Core developers
// Copyright (c) 2014-2023 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_CLIENTMODEL_H
#define BITCOIN_QT_CLIENTMODEL_H

#include <interfaces/node.h>
#include <sync.h>

#include <QObject>
#include <QDateTime>

#include <atomic>
#include <memory>
#include <uint256.h>

class BanTableModel;
class CBlockIndex;
class OptionsModel;
class PeerTableModel;
enum class SynchronizationState;

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

enum class BlockSource {
    NONE,
    REINDEX,
    DISK,
    NETWORK
};

enum NumConnections {
    CONNECTIONS_NONE = 0,
    CONNECTIONS_IN   = (1U << 0),
    CONNECTIONS_OUT  = (1U << 1),
    CONNECTIONS_ALL  = (CONNECTIONS_IN | CONNECTIONS_OUT),
};

class CDeterministicMNList;
class CGovernanceObject;
typedef std::shared_ptr<CDeterministicMNList> CDeterministicMNListPtr;

/** Model for Sparks network client. */
class ClientModel : public QObject
{
    Q_OBJECT

public:
    explicit ClientModel(interfaces::Node& node, OptionsModel *optionsModel, QObject *parent = nullptr);
    ~ClientModel();

    interfaces::Node& node() const { return m_node; }
    interfaces::Masternode::Sync& masternodeSync() const { return m_node.masternodeSync(); }
    interfaces::CoinJoin::Options& coinJoinOptions() const { return m_node.coinJoinOptions(); }
    OptionsModel *getOptionsModel();
    PeerTableModel *getPeerTableModel();
    BanTableModel *getBanTableModel();

    //! Return number of connections, default is in- and outbound (total)
    int getNumConnections(unsigned int flags = CONNECTIONS_ALL) const;
    int getNumBlocks() const;
    uint256 getBestBlockHash() EXCLUSIVE_LOCKS_REQUIRED(!m_cached_tip_mutex);
    int getHeaderTipHeight() const;
    int64_t getHeaderTipTime() const;

    void setMasternodeList(const CDeterministicMNList& mnList, const CBlockIndex* tip);
    std::pair<CDeterministicMNList, const CBlockIndex*> getMasternodeList() const;
    void refreshMasternodeList();

    void getAllGovernanceObjects(std::vector<CGovernanceObject> &obj);

    //! Returns enum BlockSource of the current importing/syncing state
    enum BlockSource getBlockSource() const;
    //! Return warnings to be displayed in status bar
    QString getStatusBarWarnings() const;

    QString formatFullVersion() const;
    QString formatSubVersion() const;
    bool isReleaseVersion() const;
    QString formatClientStartupTime() const;
    QString dataDir() const;
    QString blocksDir() const;

    bool getProxyInfo(std::string& ip_port) const;

    // caches for the best header: hash, number of blocks and block time
    mutable std::atomic<int> cachedBestHeaderHeight;
    mutable std::atomic<int64_t> cachedBestHeaderTime;
    mutable std::atomic<int> m_cached_num_blocks{-1};

    Mutex m_cached_tip_mutex;
    uint256 m_cached_tip_blocks GUARDED_BY(m_cached_tip_mutex){};

private:
    interfaces::Node& m_node;
    std::unique_ptr<interfaces::Handler> m_handler_show_progress;
    std::unique_ptr<interfaces::Handler> m_handler_notify_num_connections_changed;
    std::unique_ptr<interfaces::Handler> m_handler_notify_network_active_changed;
    std::unique_ptr<interfaces::Handler> m_handler_notify_alert_changed;
    std::unique_ptr<interfaces::Handler> m_handler_banned_list_changed;
    std::unique_ptr<interfaces::Handler> m_handler_notify_block_tip;
    std::unique_ptr<interfaces::Handler> m_handler_notify_chainlock;
    std::unique_ptr<interfaces::Handler> m_handler_notify_header_tip;
    std::unique_ptr<interfaces::Handler> m_handler_notify_masternodelist_changed;
    std::unique_ptr<interfaces::Handler> m_handler_notify_additional_data_sync_progess_changed;
    OptionsModel *optionsModel;
    PeerTableModel *peerTableModel;
    BanTableModel *banTableModel;

    //! A thread to interact with m_node asynchronously
    QThread* const m_thread;

    // The cache for mn list is not technically needed because CDeterministicMNManager
    // caches it internally for recent blocks but it's not enough to get consistent
    // representation of the list in UI during initial sync/reindex, so we cache it here too.
    mutable RecursiveMutex cs_mnlinst; // protects mnListCached
    CDeterministicMNListPtr mnListCached;
    const CBlockIndex* mnListTip;

    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();

Q_SIGNALS:
    void numConnectionsChanged(int count);
    void masternodeListChanged() const;
    void chainLockChanged(const QString& bestChainLockHash, int bestChainLockHeight);
    void numBlocksChanged(int count, const QDateTime& blockDate, const QString& blockHash, double nVerificationProgress, bool header, SynchronizationState sync_state);
    void additionalDataSyncProgressChanged(double nSyncProgress);
    void mempoolSizeChanged(long count, size_t mempoolSizeInBytes);
    void islockCountChanged(size_t count);
    void networkActiveChanged(bool networkActive);
    void alertsChanged(const QString &warnings);

    //! Fired when a message should be reported to the user
    void message(const QString &title, const QString &message, unsigned int style);

    // Show progress dialog e.g. for verifychain
    void showProgress(const QString &title, int nProgress);

public Q_SLOTS:
    void updateNumConnections(int numConnections);
    void updateNetworkActive(bool networkActive);
    void updateAlert();
    void updateBanlist();
};

#endif // BITCOIN_QT_CLIENTMODEL_H
