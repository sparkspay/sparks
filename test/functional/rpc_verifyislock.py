#!/usr/bin/env python3
# Copyright (c) 2020-2023 The Dash Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.messages import CTransaction, from_hex, hash256, ser_compact_size, ser_string
from test_framework.test_framework import SparksTestFramework
from test_framework.util import assert_raises_rpc_error, bytes_to_hex_str, satoshi_round

'''
rpc_verifyislock.py

Test verifyislock rpc

'''

class RPCVerifyISLockTest(SparksTestFramework):
    def set_test_params(self):
        # -whitelist is needed to avoid the trickling logic on node0
        self.set_sparks_test_params(6, 5, [["-whitelist=127.0.0.1"], [], [], [], [], []], fast_dip3_enforcement=True)

    def get_request_id(self, tx_hex):
        tx = from_hex(CTransaction(), tx_hex)

        request_id_buf = ser_string(b"islock") + ser_compact_size(len(tx.vin))
        for txin in tx.vin:
            request_id_buf += txin.prevout.serialize()
        return hash256(request_id_buf)[::-1].hex()

    def run_test(self):

        node = self.nodes[0]
        node.spork("SPORK_18_QUORUM_DKG_ENABLED", 0)
        self.wait_for_sporks_same()

        self.activate_v19(expected_activation_height=900)
        self.log.info("Activated v19 at height:" + str(self.nodes[0].getblockcount()))
        self.move_to_next_cycle()
        self.log.info("Cycle H height:" + str(self.nodes[0].getblockcount()))
        self.move_to_next_cycle()
        self.log.info("Cycle H+C height:" + str(self.nodes[0].getblockcount()))
        self.move_to_next_cycle()
        self.log.info("Cycle H+2C height:" + str(self.nodes[0].getblockcount()))

        self.mine_cycle_quorum(llmq_type_name='llmq_test_dip0024', llmq_type=103)
        self.bump_mocktime(1)
        self.nodes[0].generate(8)
        self.sync_blocks()

        txid = node.sendtoaddress(node.getnewaddress(), 1)
        self.wait_for_instantlock(txid, node)

        request_id = self.get_request_id(self.nodes[0].getrawtransaction(txid))
        self.wait_until(lambda: node.quorum("hasrecsig", 103, request_id, txid))

        rec_sig = node.quorum("getrecsig", 103, request_id, txid)['sig']
        assert node.verifyislock(request_id, txid, rec_sig)
        # Not mined, should use maxHeight
        assert not node.verifyislock(request_id, txid, rec_sig, 1)
        node.generate(1)
        assert txid not in node.getrawmempool()
        # Mined but at higher height, should use maxHeight
        assert not node.verifyislock(request_id, txid, rec_sig, 1)
        # Mined, should ignore higher maxHeight
        assert node.verifyislock(request_id, txid, rec_sig, node.getblockcount() + 100)

        # Mine one more cycle of rotated quorums
        self.mine_cycle_quorum(llmq_type_name='llmq_test_dip0024', llmq_type=103)
        # Create an ISLOCK using an active quorum which will be replaced when a new cycle happens
        request_id = None
        utxos = node.listunspent()
        fee = 0.001
        amount = 1
        # Try all available utxo's until we have one valid in_amount
        for utxo in utxos:
            in_amount = float(utxo['amount'])
            if in_amount < amount + fee:
                continue
            outputs = dict()
            outputs[node.getnewaddress()] = satoshi_round(amount)
            change = in_amount - amount - fee
            if change > 0:
                outputs[node.getnewaddress()] = satoshi_round(change)
            rawtx = node.createrawtransaction([utxo], outputs)
            rawtx = node.signrawtransactionwithwallet(rawtx)["hex"]
            request_id = self.get_request_id(rawtx)
            break
        # Create the ISDLOCK, then mine a cycle quorum to move renew active set
        isdlock = self.create_isdlock(rawtx)
        # Mine one block to trigger the "signHeight + dkgInterval" verification for the ISDLOCK
        self.mine_cycle_quorum(llmq_type_name='llmq_test_dip0024', llmq_type=103)
        # Verify the ISLOCK for a transaction that is not yet known by the node
        rawtx_txid = node.decoderawtransaction(rawtx)["txid"]
        assert_raises_rpc_error(-5, "No such mempool or blockchain transaction", node.getrawtransaction, rawtx_txid)
        assert node.verifyislock(request_id, rawtx_txid, isdlock.sig.hex(), node.getblockcount())
        # Send the tx and verify the ISDLOCK for a now known transaction
        assert rawtx_txid == node.sendrawtransaction(rawtx)
        assert node.verifyislock(request_id, rawtx_txid, isdlock.sig.hex(), node.getblockcount())


if __name__ == '__main__':
    RPCVerifyISLockTest().main()
