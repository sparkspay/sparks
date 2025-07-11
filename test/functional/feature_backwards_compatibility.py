#!/usr/bin/env python3
# Copyright (c) 2018-2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Backwards compatibility functional test

Test various backwards compatibility scenarios. Requires previous releases binaries,
see test/README.md.

v0.15.0.0 is not required by this test, but it is used in wallet_upgradewallet.py.
Due to a hardfork in regtest, it can't be used to sync nodes.

Due to RPC changes introduced in various versions the below tests
won't work for older versions without some patches or workarounds.

Use only the latest patch version of each release, unless a test specifically
needs an older patch version.
"""

import os
import shutil

from test_framework.test_framework import BitcoinTestFramework

from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)


class BackwardsCompatibilityTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 7
        # Add new version after each release:
        self.extra_args = [
            [], # Pre-release: use to mine blocks
            ["-nowallet"], # Pre-release: use to receive coins, swap wallets, etc
            ["-nowallet"], # v19.3.0
            ["-nowallet"], # v18.2.3
            ["-nowallet"], # v0.17.3.0
            ["-nowallet"], # v0.16.0.0
        ]
        self.wallet_names = [self.default_wallet_name]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()
        self.skip_if_no_previous_releases()

    def setup_nodes(self):
        self.add_nodes(self.num_nodes, extra_args=self.extra_args, versions=[
            None,
            None,
            19030000,
            19030000,
            18020200,
            170003,
            160101,
        ])

        self.start_nodes()
        self.import_deterministic_coinbase_privkeys()

    def run_test(self):
        self.nodes[0].generatetoaddress(101, self.nodes[0].getnewaddress())

        self.sync_blocks()

        # Sanity check the test framework:
        res = self.nodes[self.num_nodes - 1].getblockchaininfo()
        assert_equal(res['blocks'], 101)

        node_master = self.nodes[self.num_nodes - 6]
        node_v20 = self.nodes[self.num_nodes - 5]
        node_v19 = self.nodes[self.num_nodes - 4]
        node_v18 = self.nodes[self.num_nodes - 3]
        node_v17 = self.nodes[self.num_nodes - 2]
        node_v16 = self.nodes[self.num_nodes - 1]

        self.log.info("Test wallet backwards compatibility...")
        # Create a number of wallets and open them in older versions:

        # w1: regular wallet, created on master: update this test when default
        #     wallets can no longer be opened by older versions.
        node_master.createwallet(wallet_name="w1")
        wallet = node_master.get_wallet_rpc("w1")
        info = wallet.getwalletinfo()
        assert info['private_keys_enabled']
        assert info['keypoolsize'] > 0
        # Create a confirmed transaction, receiving coins
        address = wallet.getnewaddress()
        self.nodes[0].sendtoaddress(address, 1)
        self.sync_mempools()
        self.nodes[0].generate(1)
        self.sync_blocks()

        # w1_v19: regular wallet, created with v0.19
        node_v19.rpc.createwallet(wallet_name="w1_v19")
        wallet = node_v19.get_wallet_rpc("w1_v19")
        info = wallet.getwalletinfo()
        assert info['private_keys_enabled']
        assert info['keypoolsize'] > 0
        # Use addmultisigaddress (see #18075)
        address_18075 = wallet.addmultisigaddress(1, ["0296b538e853519c726a2c91e61ec11600ae1390813a627c66fb8be7947be63c52", "037211a824f55b505228e4c3d5194c1fcfaa15a456abdf37f9b9d97a4040afc073"], "")["address"]
        assert wallet.getaddressinfo(address_18075)["solvable"]

        # w1_v18: regular wallet, created with v0.18
        node_v18.rpc.createwallet(wallet_name="w1_v18")
        wallet = node_v18.get_wallet_rpc("w1_v18")
        info = wallet.getwalletinfo()
        assert info['private_keys_enabled']
        assert info['keypoolsize'] > 0

        # w2: wallet with private keys disabled, created on master: update this
        #     test when default wallets private keys disabled can no longer be
        #     opened by older versions.
        node_master.createwallet(wallet_name="w2", disable_private_keys=True)
        wallet = node_master.get_wallet_rpc("w2")
        info = wallet.getwalletinfo()
        assert info['private_keys_enabled'] == False
        assert info['keypoolsize'] == 0

        # w1_v20: regular wallet, created with v20.0
        node_v20.rpc.createwallet(wallet_name="w1_v20")
        wallet = node_v20.get_wallet_rpc("w1_v20")
        info = wallet.getwalletinfo()
        assert info['private_keys_enabled']
        assert info['keypoolsize'] > 0

        # w2_v19: wallet with private keys disabled, created with v0.19
        node_v19.rpc.createwallet(wallet_name="w2_v19", disable_private_keys=True)
        wallet = node_v19.get_wallet_rpc("w2_v19")
        info = wallet.getwalletinfo()
        assert info['private_keys_enabled'] == False
        assert info['keypoolsize'] == 0

        # w2_v18: wallet with private keys disabled, created with v0.18
        node_v18.rpc.createwallet(wallet_name="w2_v18", disable_private_keys=True)
        wallet = node_v18.get_wallet_rpc("w2_v18")
        info = wallet.getwalletinfo()
        assert info['private_keys_enabled'] == False
        assert info['keypoolsize'] == 0

        # w3: blank wallet, created on master: update this
        #     test when default blank wallets can no longer be opened by older versions.
        node_master.createwallet(wallet_name="w3", blank=True)
        wallet = node_master.get_wallet_rpc("w3")
        info = wallet.getwalletinfo()
        assert info['private_keys_enabled']
        assert info['keypoolsize'] == 0

        # w3_v19: blank wallet, created with v0.19
        node_v19.rpc.createwallet(wallet_name="w3_v19", blank=True)
        wallet = node_v19.get_wallet_rpc("w3_v19")
        info = wallet.getwalletinfo()
        assert info['private_keys_enabled']
        assert info['keypoolsize'] == 0

        # w3_v18: blank wallet, created with v0.18
        node_v18.rpc.createwallet(wallet_name="w3_v18", blank=True)
        wallet = node_v18.get_wallet_rpc("w3_v18")
        info = wallet.getwalletinfo()
        assert info['private_keys_enabled']
        assert info['keypoolsize'] == 0

        # Copy the wallets to older nodes:
        node_master_wallets_dir = os.path.join(node_master.datadir, "regtest/wallets")
        node_v20_wallets_dir = os.path.join(node_v20.datadir, "regtest/wallets")
        node_v19_wallets_dir = os.path.join(node_v19.datadir, "regtest/wallets")
        node_v18_wallets_dir = os.path.join(node_v18.datadir, "regtest/wallets")
        node_v17_wallets_dir = os.path.join(node_v17.datadir, "regtest/wallets")
        node_v16_wallets_dir = os.path.join(node_v16.datadir, "regtest")
        node_master.unloadwallet("w1")
        node_master.unloadwallet("w2")
        node_v19.unloadwallet("w1_v19")
        node_v19.unloadwallet("w2_v19")
        node_v18.unloadwallet("w1_v18")
        node_v18.unloadwallet("w2_v18")

        # Copy wallets to v0.16
        for wallet in os.listdir(node_master_wallets_dir):
            shutil.copytree(
                os.path.join(node_master_wallets_dir, wallet),
                os.path.join(node_v16_wallets_dir, wallet)
            )

        # Copy wallets to v0.17
        for wallet in os.listdir(node_master_wallets_dir):
            shutil.copytree(
                os.path.join(node_master_wallets_dir, wallet),
                os.path.join(node_v17_wallets_dir, wallet)
            )
        for wallet in os.listdir(node_v18_wallets_dir):
            shutil.copytree(
                os.path.join(node_v18_wallets_dir, wallet),
                os.path.join(node_v17_wallets_dir, wallet)
            )

        # Copy wallets to v0.18
        for wallet in os.listdir(node_master_wallets_dir):
            shutil.copytree(
                os.path.join(node_master_wallets_dir, wallet),
                os.path.join(node_v18_wallets_dir, wallet)
            )

        # Copy wallets to v0.19
        for wallet in os.listdir(node_master_wallets_dir):
            shutil.copytree(
                os.path.join(node_master_wallets_dir, wallet),
                os.path.join(node_v19_wallets_dir, wallet)
            )

        # Copy wallets to v0.20
        for wallet in os.listdir(node_master_wallets_dir):
            shutil.copytree(
                os.path.join(node_master_wallets_dir, wallet),
                os.path.join(node_v20_wallets_dir, wallet)
            )

        if not self.options.descriptors:
            # Descriptor wallets break compatibility, only run this test for legacy wallet
            # Open the wallets in v0.20
            node_v20.loadwallet("w1")
            wallet = node_v20.get_wallet_rpc("w1")
            info = wallet.getwalletinfo()
            assert info['private_keys_enabled']
            assert info['keypoolsize'] > 0
            txs = wallet.listtransactions()
            assert_equal(len(txs), 1)

            node_v20.loadwallet("w2")
            wallet = node_v20.get_wallet_rpc("w2")
            info = wallet.getwalletinfo()
            assert info['private_keys_enabled'] == False
            assert info['keypoolsize'] == 0

            node_v20.loadwallet("w3")
            wallet = node_v20.get_wallet_rpc("w3")
            info = wallet.getwalletinfo()
            assert info['private_keys_enabled']
            assert info['keypoolsize'] == 0

            # Open the wallets in v0.19
            node_v19.loadwallet("w1")
            wallet = node_v19.get_wallet_rpc("w1")
            info = wallet.getwalletinfo()
            assert info['private_keys_enabled']
            assert info['keypoolsize'] > 0
            txs = wallet.listtransactions()
            assert_equal(len(txs), 1)

            node_v19.loadwallet("w2")
            wallet = node_v19.get_wallet_rpc("w2")
            info = wallet.getwalletinfo()
            assert info['private_keys_enabled'] == False
            assert info['keypoolsize'] == 0

            node_v19.loadwallet("w3")
            wallet = node_v19.get_wallet_rpc("w3")
            info = wallet.getwalletinfo()
            assert info['private_keys_enabled']
            assert info['keypoolsize'] == 0

            # Open the wallets in v0.18
            node_v18.loadwallet("w1")
            wallet = node_v18.get_wallet_rpc("w1")
            info = wallet.getwalletinfo()
            assert info['private_keys_enabled']
            assert info['keypoolsize'] > 0
            txs = wallet.listtransactions()
            assert_equal(len(txs), 1)

            node_v18.loadwallet("w2")
            wallet = node_v18.get_wallet_rpc("w2")
            info = wallet.getwalletinfo()
            assert info['private_keys_enabled'] == False
            assert info['keypoolsize'] == 0

            node_v18.loadwallet("w3")
            wallet = node_v18.get_wallet_rpc("w3")
            info = wallet.getwalletinfo()
            assert info['private_keys_enabled']
            assert info['keypoolsize'] == 0

            node_v17.loadwallet("w1")
            wallet = node_v17.get_wallet_rpc("w1")
            info = wallet.getwalletinfo()
            # doesn't have private_keys_enabled in v17
            #assert info['private_keys_enabled']
            assert info['keypoolsize'] > 0

            node_v17.loadwallet("w2")
            wallet = node_v17.get_wallet_rpc("w2")
            info = wallet.getwalletinfo()
            # doesn't have private_keys_enabled in v17
            # TODO enable back when HD wallets are created by default
            # assert info['private_keys_enabled'] == False
            # assert info['keypoolsize'] == 0
        else:
            # Descriptor wallets appear to be corrupted wallets to old software
            assert_raises_rpc_error(-4, "Wallet requires newer version of Sparks Core", node_v19.loadwallet, "w1")
            assert_raises_rpc_error(-4, "Wallet requires newer version of Sparks Core", node_v19.loadwallet, "w2")
            assert_raises_rpc_error(-4, "Wallet requires newer version of Sparks Core", node_v19.loadwallet, "w3")
            assert_raises_rpc_error(-18, "Data is not in recognized format", node_v18.loadwallet, "w1")
            assert_raises_rpc_error(-18, "Data is not in recognized format", node_v18.loadwallet, "w2")
            assert_raises_rpc_error(-18, "Data is not in recognized format", node_v18.loadwallet, "w3")

        # Open the wallets in v0.17
        node_v17.loadwallet("w1_v18")
        wallet = node_v17.get_wallet_rpc("w1_v18")
        info = wallet.getwalletinfo()
        # doesn't have private_keys_enabled in v17
        # assert info['private_keys_enabled']
        assert info['keypoolsize'] > 0

        node_v17.loadwallet("w2_v18")
        wallet = node_v17.get_wallet_rpc("w2_v18")
        info = wallet.getwalletinfo()
        # doesn't have private_keys_enabled in v17
        # TODO enable back when HD wallets are created by default
        #assert info['private_keys_enabled'] == False
        #assert info['keypoolsize'] == 0

        # RPC loadwallet failure causes bitcoind to exit, in addition to the RPC
        # call failure, so the following test won't work:
        # assert_raises_rpc_error(-4, "Wallet loading failed.", node_v17.loadwallet, 'w3_v18')

        # Instead, we stop node and try to launch it with the wallet:
        self.stop_node(5)
        # it expected to fail with error 'DBErrors::TOO_NEW' but Sparks Core can open v18 by version 17
        # can be implemented in future if there's any incompatible versions
        #node_v17.assert_start_raises_init_error(["-wallet=w3_v18"], "Error: Error loading w3_v18: Wallet requires newer version of Sparks Core")
        #node_v17.assert_start_raises_init_error(["-wallet=w3"], "Error: Error loading w3: Wallet requires newer version of Sparks Core")
        self.start_node(5)

        # Open most recent wallet in v0.16 (no loadwallet RPC)
        self.restart_node(6, extra_args=["-wallet=w2"])
        wallet = node_v16.get_wallet_rpc("w2")
        info = wallet.getwalletinfo()
        assert info['keypoolsize'] == 1

        self.log.info("Test wallet upgrade path...")
        # u1: regular wallet, created with v0.17
        node_v17.rpc.createwallet(wallet_name="u1_v17")
        wallet = node_v17.get_wallet_rpc("u1_v17")
        address = wallet.getnewaddress()
        v17_info = wallet.getaddressinfo(address)
        # TODO enable back when HD wallets are created by default
        #v17_hdkeypath = v17_info["hdkeypath"]
        v17_pubkey = v17_info["pubkey"]

        if self.is_bdb_compiled():
            # Old wallets are BDB and will only work if BDB is compiled
            # Copy the 0.16 wallet to the last Bitcoin Core version and open it:
            node_v17.unloadwallet("u1_v17")
            shutil.copytree(
                os.path.join(node_v17_wallets_dir, "u1_v17"),
                os.path.join(node_master_wallets_dir, "u1_v17")
            )
            node_master.loadwallet("u1_v17")
            wallet = node_master.get_wallet_rpc("u1_v17")
            info = wallet.getaddressinfo(address)
            # TODO enable back when HD wallets are created by default
            #descriptor = "pkh([" + info["hdmasterfingerprint"] + hdkeypath[1:] + "]" + v17_pubkey + ")"
            #assert_equal(info["desc"], descsum_create(descriptor))
            assert_equal(info["pubkey"], v17_pubkey)

            # Now copy that same wallet back to 0.17 to make sure no automatic upgrade breaks it
            node_master.unloadwallet("u1_v17")
            shutil.rmtree(os.path.join(node_v17_wallets_dir, "u1_v17"))
            shutil.copytree(
                os.path.join(node_master_wallets_dir, "u1_v17"),
                os.path.join(node_v17_wallets_dir, "u1_v17")
            )
            node_v17.loadwallet("u1_v17")
            wallet = node_v17.get_wallet_rpc("u1_v17")
            info = wallet.getaddressinfo(address)
            assert_equal(info, v17_info)

            # Copy the 0.19 wallet to the last Bitcoin Core version and open it:
            shutil.copytree(
                os.path.join(node_v19_wallets_dir, "w1_v19"),
                os.path.join(node_master_wallets_dir, "w1_v19")
            )
            node_master.loadwallet("w1_v19")
            wallet = node_master.get_wallet_rpc("w1_v19")
            assert wallet.getaddressinfo(address_18075)["solvable"]

            # Now copy that same wallet back to 0.19 to make sure no automatic upgrade breaks it
            node_master.unloadwallet("w1_v19")
            shutil.rmtree(os.path.join(node_v19_wallets_dir, "w1_v19"))
            shutil.copytree(
                os.path.join(node_master_wallets_dir, "w1_v19"),
                os.path.join(node_v19_wallets_dir, "w1_v19")
            )
            node_v19.loadwallet("w1_v19")
            wallet = node_v19.get_wallet_rpc("w1_v19")
            assert wallet.getaddressinfo(address_18075)["solvable"]

if __name__ == '__main__':
    BackwardsCompatibilityTest().main()
