#!/bin/bash
# use testnet settings,  if you need mainnet,  use ~/.sparkscore/sparksd.pid file instead
sparks_pid=$(<~/.sparkscore/testnet3/sparksd.pid)
sudo gdb -batch -ex "source debug.gdb" sparksd ${sparks_pid}
