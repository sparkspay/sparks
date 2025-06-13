#!/usr/bin/env python3
# Copyright (c) 2015-2023 The Dash Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.messages import COIN, COutPoint, CTransaction, CTxIn, CTxOut
from test_framework.script import CScript, OP_CAT, OP_DROP, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_raises_rpc_error, softfork_active, satoshi_round

'''
feature_dip0020_activation.py

This test checks activation of DIP0020 opcodes
'''

DISABLED_OPCODE_ERROR = "non-mandatory-script-verify-flag (Attempted to use a disabled opcode)"


class DIP0020ActivationTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [["-acceptnonstdtxn=1"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        self.node = self.nodes[0]
        self.relayfee = satoshi_round(self.nodes[0].getnetworkinfo()["relayfee"])

        # We should have some coins already
        utxos = self.node.listunspent()
        assert len(utxos) > 0

        # Lock some coins using disabled opcodes
        utxo = utxos[len(utxos) - 1]
        value = int(satoshi_round(utxo["amount"] - self.relayfee) * COIN)
        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"])))
        tx.vout.append(CTxOut(value, CScript([b'1', b'2', OP_CAT])))
        tx_signed_hex = self.node.signrawtransactionwithwallet(tx.serialize().hex())["hex"]
        txid = self.node.sendrawtransaction(tx_signed_hex)

        # This tx should be completely valid, should be included in mempool and mined in the next block
        assert txid in set(self.node.getrawmempool())
        self.node.generate(1)
        assert txid not in set(self.node.getrawmempool())

        # Create spending tx
        value = int(value - self.relayfee * COIN)
        tx0 = CTransaction()
        tx0.vin.append(CTxIn(COutPoint(int(txid, 16), 0)))
        tx0.vout.append(CTxOut(value, CScript([OP_TRUE, OP_DROP] * 15 + [OP_TRUE])))
        tx0.rehash()
        tx0_hex = tx0.serialize().hex()

        # This tx isn't valid yet
        assert not softfork_active(self.nodes[0], 'dip0020')
        assert_raises_rpc_error(-26, DISABLED_OPCODE_ERROR, self.node.sendrawtransaction, tx0_hex)

        # Generate enough blocks to activate DIP0020 opcodes
        self.node.generate(98)
        assert softfork_active(self.nodes[0], 'dip0020')

        # Still need 1 more block for mempool to accept new opcodes
        assert_raises_rpc_error(-26, DISABLED_OPCODE_ERROR, self.node.sendrawtransaction, tx0_hex)
        self.node.generate(1)

        # Should be spendable now
        tx0id = self.node.sendrawtransaction(tx0_hex)
        assert tx0id in set(self.node.getrawmempool())


if __name__ == '__main__':
    DIP0020ActivationTest().main()
