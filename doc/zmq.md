# Block and Transaction Broadcasting with ZeroMQ

[ZeroMQ](https://zeromq.org/) is a lightweight wrapper around TCP
connections, inter-process communication, and shared-memory,
providing various message-oriented semantics such as publish/subscribe,
request/reply, and push/pull.

The Sparks Core daemon can be configured to act as a trusted "border
router", implementing the sparks wire protocol and relay, making
consensus decisions, maintaining the local blockchain database,
broadcasting locally generated transactions into the network, and
providing a queryable RPC interface to interact on a polled basis for
requesting blockchain related data. However, there exists only a
limited service to notify external software of events like the arrival
of new blocks or transactions.

The ZeroMQ facility implements a notification interface through a set
of specific notifiers. Currently there are notifiers that publish
blocks and transactions. This read-only facility requires only the
connection of a corresponding ZeroMQ subscriber port in receiving
software; it is not authenticated nor is there any two-way protocol
involvement. Therefore, subscribers should validate the received data
since it may be out of date, incomplete or even invalid.

ZeroMQ sockets are self-connecting and self-healing; that is,
connections made between two endpoints will be automatically restored
after an outage, and either end may be freely started or stopped in
any order.

Because ZeroMQ is message oriented, subscribers receive transactions
and blocks all-at-once and do not need to implement any sort of
buffering or reassembly.

## Prerequisites

The ZeroMQ feature in Sparks Core requires the ZeroMQ API >= 4.0.0
[libzmq](https://github.com/zeromq/libzmq/releases).
For version information, see [dependencies.md](dependencies.md).
Typically, it is packaged by distributions as something like
*libzmq3-dev*. The C++ wrapper for ZeroMQ is *not* needed.

In order to run the example Python client scripts in the `contrib/zmq/`
directory, one must also install [PyZMQ](https://github.com/zeromq/pyzmq)
(generally with `pip install pyzmq`), though this is not necessary for daemon
operation.

## Enabling

By default, the ZeroMQ feature is automatically compiled in if the
necessary prerequisites are found.  To disable, use --disable-zmq
during the *configure* step of building sparksd:

    $ ./configure --disable-zmq (other options)

To actually enable operation, one must set the appropriate options on
the command line or in the configuration file.

## Usage

Currently, the following notifications are supported:

    -zmqpubhashblock=address
    -zmqpubhashchainlock=address
    -zmqpubhashtx=address
    -zmqpubhashtxlock=address
    -zmqpubhashgovernancevote=address
    -zmqpubhashgovernanceobject=address
    -zmqpubhashinstantsenddoublespend=address
    -zmqpubhashrecoveredsig=address
    -zmqpubrawblock=address
    -zmqpubrawchainlock=address
    -zmqpubrawchainlocksig=address
    -zmqpubrawtx=address
    -zmqpubrawtxlock=address
    -zmqpubrawtxlocksig=address
    -zmqpubrawgovernancevote=address
    -zmqpubrawgovernanceobject=address
    -zmqpubrawinstantsenddoublespend=address
    -zmqpubrawrecoveredsig=address

The socket type is PUB and the address must be a valid ZeroMQ socket
address. The same address can be used in more than one notification.
The same notification can be specified more than once.

The option to set the PUB socket's outbound message high water mark
(SNDHWM) may be set individually for each notification:

    -zmqpubhashtxhwm=n
    -zmqpubhashblockhwm=n
    -zmqpubhashchainlockhwm=n
    -zmqpubhashtxlockhwm=n
    -zmqpubhashgovernancevotehwm=n
    -zmqpubhashgovernanceobjecthwm=n
    -zmqpubhashinstantsenddoublespendhwm=n
    -zmqpubhashrecoveredsighwm=n
    -zmqpubrawblockhwm=n
    -zmqpubrawtxhwm=n
    -zmqpubrawchainlockhwm=n
    -zmqpubrawchainlocksighwm=n
    -zmqpubrawtxlockhwm=n
    -zmqpubrawtxlocksighwm=n
    -zmqpubrawgovernancevotehwm=n
    -zmqpubrawgovernanceobjecthwm=n
    -zmqpubrawinstantsenddoublespendhwm=n
    -zmqpubrawrecoveredsighwm=n

The high water mark value must be an integer greater than or equal to 0.

For instance:

    $ sparksd -zmqpubhashtx=tcp://127.0.0.1:28332 \
               -zmqpubhashtx=tcp://192.168.1.2:28332 \
               -zmqpubrawtx=ipc:///tmp/sparksd.tx.raw \
               -zmqpubhashtxhwm=10000

Each PUB notification has a topic and body, where the header
corresponds to the notification type. For instance, for the
notification `-zmqpubhashtx` the topic is `hashtx` (no null
terminator) and the body is the transaction hash (32
bytes).

These options can also be provided in sparks.conf.

ZeroMQ endpoint specifiers for TCP (and others) are documented in the
[ZeroMQ API](http://api.zeromq.org/4-0:_start).

Client side, then, the ZeroMQ subscriber socket must have the
ZMQ_SUBSCRIBE option set to one or either of these prefixes (for
instance, just `hash`); without doing so will result in no messages
arriving. Please see [`contrib/zmq/zmq_sub.py`](/contrib/zmq/zmq_sub.py) for a working example.

The ZMQ_PUB socket's ZMQ_TCP_KEEPALIVE option is enabled. This means that
the underlying SO_KEEPALIVE option is enabled when using a TCP transport.
The effective TCP keepalive values are managed through the underlying
operating system configuration and must be configured prior to connection establishment.

For example, when running on GNU/Linux, one might use the following
to lower the keepalive setting to 10 minutes:

sudo sysctl -w net.ipv4.tcp_keepalive_time=600

Setting the keepalive values appropriately for your operating environment may
improve connectivity in situations where long-lived connections are silently
dropped by network middle boxes.

## Remarks

From the perspective of sparksd, the ZeroMQ socket is write-only; PUB
sockets don't even have a read function. Thus, there is no state
introduced into sparksd directly. Furthermore, no information is
broadcast that wasn't already received from the public P2P network.

No authentication or authorization is done on connecting clients; it
is assumed that the ZeroMQ port is exposed only to trusted entities,
using other means such as firewalling.

Note that when the block chain tip changes, a reorganisation may occur
and just the tip will be notified. It is up to the subscriber to
retrieve the chain from the last known block to the new tip. Also note
that no notification occurs if the tip was in the active chain - this
is the case after calling invalidateblock RPC.

There are several possibilities that ZMQ notification can get lost
during transmission depending on the communication type you are
using. Sparksd appends an up-counting sequence number to each
notification which allows listeners to detect lost notifications.
