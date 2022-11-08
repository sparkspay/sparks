Sparks Core version 0.17.0.0
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

### Downgrade of masternodes to < 0.17.0.2

Starting with the 0.16 release, masternodes verify the protocol version of other
masternodes. This results in PoSe punishment/banning for outdated masternodes,
so downgrading even prior to the activation of the introduced hard-fork changes
is not recommended.

Notable changes
===============

This release adds some missing translations and help strings. It also fixes
a couple of build issues and a rare crash on some linux systems.

0.17.0.3 Change log
===================

See detailed [set of changes](https://github.com/sparkspay/sparks/compare/v0.16.0.0...sparkspay:v0.17.0.0).

- [`96c041896b`](https://github.com/sparkspay/sparks/commit/96c041896b) feat: add tor entrypoint script for use in sparksmate (#4182)
- [`3661f36bbd`](https://github.com/sparkspay/sparks/commit/3661f36bbd) Merge #14416: Fix OSX dmg issue (10.12 to 10.14) (#4177)
- [`4f4bda0557`](https://github.com/sparkspay/sparks/commit/4f4bda0557) depends: Undefine `BLSALLOC_SODIUM` in `bls-sparks.mk` (#4176)
- [`575e0a3070`](https://github.com/sparkspay/sparks/commit/575e0a3070) qt: Add `QFont::Normal` as a supported font weight when no other font weights were found (#4175)
- [`ce4a73b790`](https://github.com/sparkspay/sparks/commit/ce4a73b790) rpc: Fix `upgradetohd` help text (#4170)
- [`2fa8ddf160`](https://github.com/sparkspay/sparks/commit/2fa8ddf160) Translations 202105 (add missing) (#4169)

Credits
=======

Thanks to everyone who directly contributed to this release:

- dustinface (xdustinface)
- strophy
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
