#!/usr/bin/env python3
# Copyright (c) 2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the send RPC command."""

from decimal import Decimal, getcontext
from test_framework.authproxy import JSONRPCException
from test_framework.descriptors import descsum_create
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_fee_amount,
    assert_greater_than,
    assert_raises_rpc_error
)

class WalletSendTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        # whitelist all peers to speed up tx relay / mempool sync
        self.extra_args = [
            ["-whitelist=127.0.0.1"],
            ["-whitelist=127.0.0.1"],
        ]
        getcontext().prec = 8 # Satoshi precision for Decimal

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def test_send(self, from_wallet, to_wallet=None, amount=None, data=None,
                  arg_conf_target=None, arg_estimate_mode=None,
                  conf_target=None, estimate_mode=None, add_to_wallet=None, psbt=None,
                  inputs=None, add_inputs=None, include_unsafe=None, change_address=None, change_position=None,
                  include_watching=None, locktime=None, lock_unspents=None, subtract_fee_from_outputs=None,
                  expect_error=None):
        assert (amount is None) != (data is None)

        from_balance_before = from_wallet.getbalances()["mine"]["trusted"]
        if include_unsafe:
            from_balance_before += from_wallet.getbalances()["mine"]["untrusted_pending"]

        if to_wallet is None:
            assert amount is None
        else:
            to_untrusted_pending_before = to_wallet.getbalances()["mine"]["untrusted_pending"]

        if amount:
            dest = to_wallet.getnewaddress()
            outputs = {dest: amount}
        else:
            outputs = {"data": data}

        # Construct options dictionary
        options = {}
        if add_to_wallet is not None:
            options["add_to_wallet"] = add_to_wallet
        else:
            if psbt:
                add_to_wallet = False
            else:
                add_to_wallet = from_wallet.getwalletinfo()["private_keys_enabled"] # Default value
        if psbt is not None:
            options["psbt"] = psbt
        if conf_target is not None:
            options["conf_target"] = conf_target
        if estimate_mode is not None:
            options["estimate_mode"] = estimate_mode
        if inputs is not None:
            options["inputs"] = inputs
        if add_inputs is not None:
            options["add_inputs"] = add_inputs
        if include_unsafe is not None:
            options["include_unsafe"] = include_unsafe
        if change_address is not None:
            options["change_address"] = change_address
        if change_position is not None:
            options["change_position"] = change_position
        if include_watching is not None:
            options["include_watching"] = include_watching
        if locktime is not None:
            options["locktime"] = locktime
        if lock_unspents is not None:
            options["lock_unspents"] = lock_unspents
        if subtract_fee_from_outputs is not None:
            options["subtract_fee_from_outputs"] = subtract_fee_from_outputs

        if len(options.keys()) == 0:
            options = None

        if expect_error is None:
            res = from_wallet.send(outputs=outputs, conf_target=arg_conf_target, estimate_mode=arg_estimate_mode, options=options)
        else:
            try:
                assert_raises_rpc_error(expect_error[0], expect_error[1], from_wallet.send,
                                        outputs=outputs, conf_target=arg_conf_target, estimate_mode=arg_estimate_mode, options=options)
            except AssertionError:
                # Provide debug info if the test fails
                self.log.error("Unexpected successful result:")
                self.log.error(options)
                res = from_wallet.send(outputs=outputs, conf_target=arg_conf_target, estimate_mode=arg_estimate_mode, options=options)
                self.log.error(res)
                if "txid" in res and add_to_wallet:
                    self.log.error("Transaction details:")
                    try:
                        tx = from_wallet.gettransaction(res["txid"])
                        self.log.error(tx)
                        self.log.error("testmempoolaccept (transaction may already be in mempool):")
                        self.log.error(from_wallet.testmempoolaccept([tx["hex"]]))
                    except JSONRPCException as exc:
                        self.log.error(exc)

                raise

            return

        if locktime:
            return res

        if from_wallet.getwalletinfo()["private_keys_enabled"] and not include_watching:
            assert_equal(res["complete"], True)
            assert "txid" in res
        else:
            assert_equal(res["complete"], False)
            assert not "txid" in res
            assert "psbt" in res

        from_balance = from_wallet.getbalances()["mine"]["trusted"]
        if include_unsafe:
            from_balance += from_wallet.getbalances()["mine"]["untrusted_pending"]

        if add_to_wallet and not include_watching:
            # Ensure transaction exists in the wallet:
            tx = from_wallet.gettransaction(res["txid"])
            assert tx
            # Ensure transaction exists in the mempool:
            tx = from_wallet.getrawtransaction(res["txid"], True)
            assert tx
            if amount:
                if subtract_fee_from_outputs:
                    assert_equal(from_balance_before - from_balance, amount)
                else:
                    assert_greater_than(from_balance_before - from_balance, amount)
            else:
                assert next((out for out in tx["vout"] if out["scriptPubKey"]["asm"] == "OP_RETURN 35"), None)
        else:
            assert_equal(from_balance_before, from_balance)

        if to_wallet:
            self.sync_mempools()
            if add_to_wallet:
                if not subtract_fee_from_outputs:
                    assert_equal(to_wallet.getbalances()["mine"]["untrusted_pending"], to_untrusted_pending_before + Decimal(amount if amount else 0))
            else:
                assert_equal(to_wallet.getbalances()["mine"]["untrusted_pending"], to_untrusted_pending_before)

        return res

    def run_test(self):
        self.log.info("Setup wallets...")
        # w0 is a wallet with coinbase rewards
        w0 = self.nodes[0].get_wallet_rpc(self.default_wallet_name)
        # w1 is a regular wallet
        self.nodes[1].createwallet(wallet_name="w1")
        w1 = self.nodes[1].get_wallet_rpc("w1")
        # w2 contains the private keys for w3
        self.nodes[1].createwallet(wallet_name="w2", blank=True)
        w2 = self.nodes[1].get_wallet_rpc("w2")
        xpriv = "tprv8ZgxMBicQKsPfHCsTwkiM1KT56RXbGGTqvc2hgqzycpwbHqqpcajQeMRZoBD35kW4RtyCemu6j34Ku5DEspmgjKdt2qe4SvRch5Kk8B8A2v"
        xpub = "tpubD6NzVbkrYhZ4YkEfMbRJkQyZe7wTkbTNRECozCtJPtdLRn6cT1QKb8yHjwAPcAr26eHBFYs5iLiFFnCbwPRsncCKUKCfubHDMGKzMVcN1Jg"
        if self.options.descriptors:
            w2.importdescriptors([{
                "desc": descsum_create("pkh(" + xpriv + "/0/0/*)"),
                "timestamp": "now",
                "range": [0, 100],
                "active": True
            },{
                "desc": descsum_create("pkh(" + xpriv + "/0/1/*)"),
                "timestamp": "now",
                "range": [0, 100],
                "active": True,
                "internal": True
            }])
        else:
            w2.upgradetohd()
            # TODO: replace upgradetohd to sethdseed when it is implemented
            # w2.sethdseed(True)

        # w3 is a watch-only wallet, based on w2
        self.nodes[1].createwallet(wallet_name="w3", disable_private_keys=True)
        w3 = self.nodes[1].get_wallet_rpc("w3")
        if self.options.descriptors:
            # Match the privkeys in w2 for descriptors
            res = w3.importdescriptors([{
                "desc": descsum_create("pkh(" + xpub + "/0/0/*)"),
                "timestamp": "now",
                "range": [0, 100],
                "keypool": True,
                "active": True,
                "watchonly": True
            },{
                "desc": descsum_create("pkh(" + xpub + "/0/1/*)"),
                "timestamp": "now",
                "range": [0, 100],
                "keypool": True,
                "active": True,
                "internal": True,
                "watchonly": True
            }])
            assert_equal(res, [{"success": True}, {"success": True}])

        for _ in range(3):
            a2_receive = w2.getnewaddress()
            if not self.options.descriptors:
                # Because legacy wallets use exclusively hardened derivation, we can't do a ranged import like we do for descriptors
                a2_change = w2.getrawchangeaddress() # doesn't actually use change derivation
                res = w3.importmulti([{
                    "desc": w2.getaddressinfo(a2_receive)["desc"],
                    "timestamp": "now",
                    "keypool": True,
                    "watchonly": True
                },{
                    "desc": w2.getaddressinfo(a2_change)["desc"],
                    "timestamp": "now",
                    "keypool": True,
                    "internal": True,
                    "watchonly": True
                }])
                assert_equal(res, [{"success": True}, {"success": True}])

        w0.sendtoaddress(a2_receive, 10) # fund w3
        self.nodes[0].generate(1)
        self.sync_blocks()

        if not self.options.descriptors:
            # w4 has private keys enabled, but only contains watch-only keys (from w2)
            # This is legacy wallet behavior only as descriptor wallets don't allow watchonly and non-watchonly things in the same wallet.
            self.nodes[1].createwallet(wallet_name="w4", disable_private_keys=False)
            w4 = self.nodes[1].get_wallet_rpc("w4")
            for _ in range(3):
                a2_receive = w2.getnewaddress()
                res = w4.importmulti([{
                    "desc": w2.getaddressinfo(a2_receive)["desc"],
                    "timestamp": "now",
                    "keypool": False,
                    "watchonly": True
                }])
                assert_equal(res, [{"success": True}])

            w0.sendtoaddress(a2_receive, 10) # fund w4
            self.nodes[0].generate(1)
            self.sync_blocks()

        self.log.info("Send to address...")
        self.test_send(from_wallet=w0, to_wallet=w1, amount=1)
        self.test_send(from_wallet=w0, to_wallet=w1, amount=1, add_to_wallet=True)

        self.log.info("Don't broadcast...")
        res = self.test_send(from_wallet=w0, to_wallet=w1, amount=1, add_to_wallet=False)
        assert(res["hex"])

        self.log.info("Return PSBT...")
        res = self.test_send(from_wallet=w0, to_wallet=w1, amount=1, psbt=True)
        assert(res["psbt"])

        self.log.info("Create transaction that spends to address, but don't broadcast...")
        self.test_send(from_wallet=w0, to_wallet=w1, amount=1, add_to_wallet=False)
        # conf_target & estimate_mode can be set as argument or option
        res1 = self.test_send(from_wallet=w0, to_wallet=w1, amount=1, arg_conf_target=1, arg_estimate_mode="economical", add_to_wallet=False)
        res2 = self.test_send(from_wallet=w0, to_wallet=w1, amount=1, conf_target=1, estimate_mode="economical", add_to_wallet=False)
        assert_equal(self.nodes[1].decodepsbt(res1["psbt"])["fee"],
                     self.nodes[1].decodepsbt(res2["psbt"])["fee"])
        # but not at the same time
        self.test_send(from_wallet=w0, to_wallet=w1, amount=1, arg_conf_target=1, arg_estimate_mode="economical",
                       conf_target=1, estimate_mode="economical", add_to_wallet=False, expect_error=(-8,"Use either conf_target and estimate_mode or the options dictionary to control fee rate"))

        self.log.info("Create PSBT from watch-only wallet w3, sign with w2...")
        res = self.test_send(from_wallet=w3, to_wallet=w1, amount=1)
        res = w2.walletprocesspsbt(res["psbt"])
        assert res["complete"]

        if not self.options.descriptors:
            # Descriptor wallets do not allow mixed watch-only and non-watch-only things in the same wallet.
            # This is specifically testing that w4 ignores its own private keys and creates a psbt with send
            # which is not something that needs to be tested in descriptor wallets.
            self.log.info("Create PSBT from wallet w4 with watch-only keys, sign with w2...")
            self.test_send(from_wallet=w4, to_wallet=w1, amount=1, expect_error=(-4, "Insufficient funds"))
            res = self.test_send(from_wallet=w4, to_wallet=w1, amount=1, include_watching=True, add_to_wallet=False)
            res = w2.walletprocesspsbt(res["psbt"])
            assert res["complete"]

        self.log.info("Create OP_RETURN...")
        self.test_send(from_wallet=w0, to_wallet=w1, amount=1)
        self.test_send(from_wallet=w0, data="Hello World", expect_error=(-8, "Data must be hexadecimal string (not 'Hello World')"))
        self.test_send(from_wallet=w0, data="23")
        res = self.test_send(from_wallet=w3, data="23")
        res = w2.walletprocesspsbt(res["psbt"])
        assert res["complete"]

        self.log.info("Set fee rate...")
        res = self.test_send(from_wallet=w0, to_wallet=w1, amount=1, conf_target=2, estimate_mode="duff/b", add_to_wallet=False)
        fee = self.nodes[1].decodepsbt(res["psbt"])["fee"]
        assert_fee_amount(fee, Decimal(len(res["hex"]) / 2), Decimal("0.00002"))
        self.test_send(from_wallet=w0, to_wallet=w1, amount=1, conf_target=-1, estimate_mode="duff/b",
                       expect_error=(-3, "Amount out of range"))

        # TODO: Return hex if fee rate is below -maxmempool
        # res = self.test_send(from_wallet=w0, to_wallet=w1, amount=1, conf_target=0.1, estimate_mode="duff/b", add_to_wallet=False)
        # assert res["hex"]
        # hex = res["hex"]
        # res = self.nodes[0].testmempoolaccept([hex])
        # assert not res[0]["allowed"]
        # assert_equal(res[0]["reject-reason"], "...") # low fee
        # assert_fee_amount(fee, Decimal(len(res["hex"]) / 2), Decimal("0.000001"))

        self.log.info("If inputs are specified, do not automatically add more...")
        res = self.test_send(from_wallet=w0, to_wallet=w1, amount=501, inputs=[], add_to_wallet=False)
        assert res["complete"]
        utxo1 = w0.listunspent()[0]
        assert_equal(utxo1["amount"], 500)
        self.test_send(from_wallet=w0, to_wallet=w1, amount=501, inputs=[utxo1],
                       expect_error=(-4, "Insufficient funds"))
        self.test_send(from_wallet=w0, to_wallet=w1, amount=501, inputs=[utxo1], add_inputs=False,
                       expect_error=(-4, "Insufficient funds"))
        res = self.test_send(from_wallet=w0, to_wallet=w1, amount=501, inputs=[utxo1], add_inputs=True, add_to_wallet=False)
        assert res["complete"]

        self.log.info("Manual change address and position...")
        self.test_send(from_wallet=w0, to_wallet=w1, amount=1, change_address="not an address",
                       expect_error=(-5, "Change address must be a valid Sparks address"))
        change_address = w0.getnewaddress()
        self.test_send(from_wallet=w0, to_wallet=w1, amount=1, add_to_wallet=False, change_address=change_address)
        assert res["complete"]
        res = self.test_send(from_wallet=w0, to_wallet=w1, amount=1, add_to_wallet=False, change_address=change_address, change_position=0)
        assert res["complete"]
        assert_equal(self.nodes[0].decodepsbt(res["psbt"])["tx"]["vout"][0]["scriptPubKey"]["address"], change_address)
        res = self.test_send(from_wallet=w0, to_wallet=w1, amount=1, add_to_wallet=False, change_position=0)
        assert res["complete"]
        change_address = self.nodes[0].decodepsbt(res["psbt"])["tx"]["vout"][0]["scriptPubKey"]["address"]
        assert change_address[0] == "y" or change_address[0] == "8" or change_address[0] == "9"

        self.log.info("Set lock time...")
        height = self.nodes[0].getblockchaininfo()["blocks"]
        res = self.test_send(from_wallet=w0, to_wallet=w1, amount=1, locktime=height + 1)
        assert res["complete"]
        assert res["txid"]
        txid = res["txid"]
        # Although the wallet finishes the transaction, it can't be added to the mempool yet:
        hex = self.nodes[0].gettransaction(res["txid"])["hex"]
        res = self.nodes[0].testmempoolaccept([hex])
        assert not res[0]["allowed"]
        assert_equal(res[0]["reject-reason"], "non-final")
        # It shouldn't be confirmed in the next block
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].gettransaction(txid)["confirmations"], 0)
        # The mempool should allow it now:
        res = self.nodes[0].testmempoolaccept([hex])
        assert res[0]["allowed"]
        # Don't wait for wallet to add it to the mempool:
        res = self.nodes[0].sendrawtransaction(hex)
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].gettransaction(txid)["confirmations"], 1)
        self.sync_all()

        self.log.info("Lock unspents...")
        utxo1 = w0.listunspent()[0]
        assert_greater_than(utxo1["amount"], 1)
        res = self.test_send(from_wallet=w0, to_wallet=w1, amount=1, inputs=[utxo1], add_to_wallet=False, lock_unspents=True)
        assert res["complete"]
        locked_coins = w0.listlockunspent()
        assert_equal(len(locked_coins), 1)
        # Locked coins are automatically unlocked when manually selected
        res = self.test_send(from_wallet=w0, to_wallet=w1, amount=1, inputs=[utxo1], add_to_wallet=False)
        assert res["complete"]

        self.log.info("Subtract fee from output")
        self.test_send(from_wallet=w0, to_wallet=w1, amount=1, subtract_fee_from_outputs=[0])

        self.log.info("Include unsafe inputs")
        self.nodes[1].createwallet(wallet_name="w5")
        w5 = self.nodes[1].get_wallet_rpc("w5")
        self.test_send(from_wallet=w0, to_wallet=w5, amount=2)
        self.test_send(from_wallet=w5, to_wallet=w0, amount=1, expect_error=(-4, "Insufficient funds"))
        res = self.test_send(from_wallet=w5, to_wallet=w0, amount=1, include_unsafe=True)
        assert res["complete"]


if __name__ == '__main__':
    WalletSendTest().main()
