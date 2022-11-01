Sparks Core version 0.16.0.0
==========================

Release is now available from:

  <https://www.sparkspay.io/downloads/#wallets>

This is a new hotfix release.

Please report bugs using the issue tracker at github:

  <https://github.com/sparkspay/sparks/issues>


Upgrading and downgrading
=========================

How to Upgrade
--------------

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/Sparks-Qt (on Mac) or
sparksd/sparks-qt (on Linux). If you upgrade after DIP0003 activation and you were
using version < 0.13 you will have to reindex (start with -reindex-chainstate
or -reindex) to make sure your wallet has all the new data synced. Upgrading
from version 0.13 should not require any additional actions.

When upgrading from a version prior to 0.14.0.3, the
first startup of Sparks Core will run a migration process which can take a few
minutes to finish. After the migration, a downgrade to an older version is only
possible with a reindex (or reindex-chainstate).

Downgrade warning
-----------------

### Downgrade to a version < 0.14.0.3

Downgrading to a version older than 0.14.0.3 is no longer supported due to
changes in the "evodb" database format. If you need to use an older version,
you must either reindex or re-sync the whole chain.

### Downgrade of masternodes to < 0.16

Starting with this release, masternodes will verify the protocol version of other
masternodes. This will result in PoSe punishment/banning for outdated masternodes,
so downgrading is not recommended.

Notable changes
===============

There was an unexpected behaviour of the "Encrypt wallet" menu item for unencrypted wallets
which was showing users the "Decrypt wallet" dialog instead. This was a GUI only issue,
internal encryption logic and RPC behaviour were not affected.

0.16.1.1 Change log
===================

See detailed [set of changes](https://github.com/sparkspay/sparks/compare/v0.16.1.0...sparkspay:v0.16.1.1).

- [`ccef3b4836`](https://github.com/sparkspay/sparks/commit/ccef3b48363d8bff4b919d9119355182e3902ef3) qt: Fix wallet encryption dialog (#3816)

Credits
=======

Thanks to everyone who directly contributed to this release:

- UdjinM6

As well as everyone that submitted issues and reviewed pull requests.

Older releases
==============

Sparks was previously known as Darkcoin.

Darkcoin tree 0.8.x was a fork of Litecoin tree 0.8, original name was XCoin
which was first released on Jan/18/2014.

Darkcoin tree 0.9.x was the open source implementation of masternodes based on
the 0.8.x tree and was first released on Mar/13/2014.

Darkcoin tree 0.10.x used to be the closed source implementation of Darksend
which was released open source on Sep/25/2014.

Sparks Core tree 0.11.x was a fork of Bitcoin Core tree 0.9,
Darkcoin was rebranded to Sparks.

Sparks Core tree 0.12.0.x was a fork of Bitcoin Core tree 0.10.

Sparks Core tree 0.12.1.x was a fork of Bitcoin Core tree 0.12.

Sparks Core tree 0.12.3.x was a fork of Sparks Core tree 0.12.3.x.

These release are considered obsolete. Old release notes can be found here:

  [v0.16.0.0](https://github.com/sparkspay/sparks/blob/master/doc/release-notes/sparks/release-notes-0.16.0.1.md)
- [v0.15.0.0](https://github.com/sparkspay/sparks/blob/master/doc/release-notes/sparks/release-notes-0.15.0.0.md) 
- [v0.14.0](https://github.com/sparkspay/sparks/blob/master/doc/release-notes/Sparks/release-notes-0.14.0.md)
- [v0.13.3](https://github.com/sparkspay/sparks/blob/master/doc/release-notes/Sparks/release-notes-0.13.3.md)
- [v0.13.2](https://github.com/sparkspay/sparks/blob/master/doc/release-notes/Sparks/release-notes-0.13.2.md)
- [v0.13.1](https://github.com/sparkspay/sparks/blob/master/doc/release-notes/Sparks/release-notes-0.13.1.md)
- [v0.13.0](https://github.com/sparkspay/sparks/blob/master/doc/release-notes/Sparks/release-notes-0.13.0.md)
- [v0.12.3.4](https://github.com/sparkspay/sparks/blob/master/doc/release-notes/Sparks/release-notes-0.12.3.4.md)
- [v0.12.3.3](https://github.com/sparkspay/sparks/blob/master/doc/release-notes/Sparks/release-notes-0.12.3.3.md)
- [v0.12.3.2](https://github.com/sparkspay/sparks/blob/master/doc/release-notes/Sparks/release-notes-0.12.3.2.md)
- [v0.12.3.1](https://github.com/sparkspay/sparks/blob/master/doc/release-notes/Sparks/release-notes-0.12.3.1.md)
- [v0.12.2.3](https://github.com/sparkspay/sparks/blob/master/doc/release-notes/Sparks/release-notes-0.12.2.3.md)
- [v0.12.2.2](https://github.com/sparkspay/sparks/blob/master/doc/release-notes/Sparks/release-notes-0.12.2.2.md)
- [v0.12.2](https://github.com/sparkspay/sparks/blob/master/doc/release-notes/Sparks/release-notes-0.12.2.md)
- [v0.12.1](https://github.com/sparkspay/sparks/blob/master/doc/release-notes/Sparks/release-notes-0.12.1.md)
- [v0.12.0](https://github.com/sparkspay/sparks/blob/master/doc/release-notes/Sparks/release-notes-0.12.0.md)
- [v0.11.2](https://github.com/sparkspay/sparks/blob/master/doc/release-notes/Sparks/release-notes-0.11.2.md)
- [v0.11.1](https://github.com/sparkspay/sparks/blob/master/doc/release-notes/Sparks/release-notes-0.11.1.md)
- [v0.11.0](https://github.com/sparkspay/sparks/blob/master/doc/release-notes/Sparks/release-notes-0.11.0.md)
- [v0.10.x](https://github.com/sparkspay/sparks/blob/master/doc/release-notes/Sparks/release-notes-0.10.0.md)
- [v0.9.x](https://github.com/sparkspay/sparks/blob/master/doc/release-notes/Sparks/release-notes-0.9.0.md) 
