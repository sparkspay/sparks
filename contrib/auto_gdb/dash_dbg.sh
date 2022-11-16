#!/usr/bin/env bash
# use testnet settings,  if you need mainnet,  use ~/.sparkscore/sparksd.pid file instead
export LC_ALL=C

sparks_pid=$(<~/.sparkscore/testnet3/sparksd.pid)
sudo gdb -batch -ex "source debug.gdb" sparksd ${sparks_pid}
