// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Copyright (c) 2014-2024 The Dash Core developers
// Copyright (c) 2016-2025 The Sparks Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addressindex.h>
#include <chainparams.h>
#include <consensus/consensus.h>
#include <deploymentstatus.h>
#include <evo/mnauth.h>
#include <httpserver.h>
#include <index/blockfilterindex.h>
#include <index/coinstatsindex.h>
#include <index/txindex.h>
#include <init.h>
#include <interfaces/chain.h>
#include <key_io.h>
#include <net.h>
#include <node/context.h>
#include <rpc/blockchain.h>
#include <rpc/index_util.h>
#include <rpc/server.h>
#include <rpc/server_util.h>
#include <rpc/util.h>
#include <scheduler.h>
#include <script/descriptor.h>
#include <txmempool.h>
#include <util/check.h>
#include <util/message.h> // For MessageSign(), MessageVerify()
#include <util/strencodings.h>
#include <util/system.h>
#include <validation.h>

#include <masternode/sync.h>
#include <spork.h>

#include <stdint.h>
#ifdef HAVE_MALLOC_INFO
#include <malloc.h>
#endif

#include <univalue.h>

static RPCHelpMan debug()
{
    return RPCHelpMan{"debug",
        "Change debug category on the fly. Specify single category or use '+' to specify many.\n"
        "The valid logging categories are: " + LogInstance().LogCategoriesString() + ".\n"
        "libevent logging is configured on startup and cannot be modified by this RPC during runtime.\n"
        "There are also a few meta-categories:\n"
        " - \"all\", \"1\" and \"\" activate all categories at once;\n"
        " - \"sparks\" activates all Sparks-specific categories at once;\n"
        " - \"none\" (or \"0\") deactivates all categories at once.\n"
        "Note: If specified category doesn't match any of the above, no error is thrown.\n",
        {
            {"category", RPCArg::Type::STR, RPCArg::Optional::NO, "The name of the debug category to turn on."},
        },
        RPCResult{
            RPCResult::Type::STR, "result", "\"Debug mode: \" followed by the specified category",
        },
        RPCExamples {
            HelpExampleCli("debug", "sparks")
    + HelpExampleRpc("debug", "sparks+net")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{

    std::string strMode = request.params[0].get_str();
    LogInstance().DisableCategory(BCLog::ALL);

    std::vector<std::string> categories = SplitString(strMode, '+');
    if (std::find(categories.begin(), categories.end(), std::string("0")) == categories.end()) {
        for (const auto& cat : categories) {
            LogInstance().EnableCategory(cat);
        }
    }

    return "Debug mode: " + LogInstance().LogCategoriesString(/*enabled_only=*/true);
},
    };
}

static RPCHelpMan mnsync()
{
    return RPCHelpMan{"mnsync",
        "Returns the sync status, updates to the next step or resets it entirely.\n",
        {
            {"mode", RPCArg::Type::STR, RPCArg::Optional::NO, "[status|next|reset]"},
        },
        {
            RPCResult{"for mode = status",
                RPCResult::Type::OBJ, "", "",
                {
                    {RPCResult::Type::NUM, "AssetID", "The asset ID"},
                    {RPCResult::Type::STR, "AssetName", "The asset name"},
                    {RPCResult::Type::NUM, "AssetStartTime", "The asset start time"},
                    {RPCResult::Type::NUM, "Attempt", "The attempt"},
                    {RPCResult::Type::BOOL, "IsBlockchainSynced", "true if the blockchain synced"},
                    {RPCResult::Type::BOOL, "IsSynced", "true if synced"},
                }},
            RPCResult{"for mode = next|reset",
                RPCResult::Type::STR, "", ""},
        },
        RPCExamples{""},
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{

    std::string strMode = request.params[0].get_str();

    const NodeContext& node = EnsureAnyNodeContext(request.context);
    CHECK_NONFATAL(node.mn_sync);
    auto& mn_sync = *node.mn_sync;

    if(strMode == "status") {
        UniValue objStatus(UniValue::VOBJ);
        objStatus.pushKV("AssetID", mn_sync.GetAssetID());
        objStatus.pushKV("AssetName", mn_sync.GetAssetName());
        objStatus.pushKV("AssetStartTime", mn_sync.GetAssetStartTime());
        objStatus.pushKV("Attempt", mn_sync.GetAttempt());
        objStatus.pushKV("IsBlockchainSynced", mn_sync.IsBlockchainSynced());
        objStatus.pushKV("IsSynced", mn_sync.IsSynced());
        return objStatus;
    }

    if(strMode == "next")
    {
        mn_sync.SwitchToNextAsset();
        return "sync updated to " + mn_sync.GetAssetName();
    }

    if(strMode == "reset")
    {
        mn_sync.Reset(true);
        return "success";
    }
    return "failure";
},
    };
}

/*
    Used for updating/reading spork settings on the network
*/
static RPCHelpMan spork()
{
    // default help, for basic mode
    return RPCHelpMan{"spork",
        "\nShows information about current state of sporks\n",
        {
            {"command", RPCArg::Type::STR, RPCArg::Optional::NO, "'show' to show all current spork values, 'active' to show which sporks are active"},
        },
        {
            RPCResult{"For 'show'",
                RPCResult::Type::OBJ_DYN, "", "keys are the sporks, and values indicates its value",
                {
                    {RPCResult::Type::NUM, "SPORK_NAME", "The value of the specific spork with the name SPORK_NAME"},
                }},
            RPCResult{"For 'active'",
                RPCResult::Type::OBJ_DYN, "", "keys are the sporks, and values indicates its status",
                {
                    {RPCResult::Type::BOOL, "SPORK_NAME", "'true' for time-based sporks if spork is active and 'false' otherwise"},
                }},
        },
        RPCExamples {
            HelpExampleCli("spork", "show")
            + HelpExampleRpc("spork", "\"show\"")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{

    // basic mode, show info
    std:: string strCommand = request.params[0].get_str();
    const NodeContext& node = EnsureAnyNodeContext(request.context);
    CHECK_NONFATAL(node.sporkman);
    if (strCommand == "show") {
        UniValue ret(UniValue::VOBJ);
        for (const auto& sporkDef : sporkDefs) {
            ret.pushKV(std::string(sporkDef.name), node.sporkman->GetSporkValue(sporkDef.sporkId));
        }
        return ret;
    } else if(strCommand == "active"){
        UniValue ret(UniValue::VOBJ);
        for (const auto& sporkDef : sporkDefs) {
            ret.pushKV(std::string(sporkDef.name), node.sporkman->IsSporkActive(sporkDef.sporkId));
        }
        return ret;
    }

    return NullUniValue;
},
    };
}

static RPCHelpMan sporkupdate()
{
    return RPCHelpMan{"sporkupdate",
        "\nUpdate the value of the specific spork. Requires \"-sporkkey\" to be set to sign the message.\n",
        {
            {"name", RPCArg::Type::STR, RPCArg::Optional::NO, "The name of the spork to update"},
            {"value", RPCArg::Type::NUM, RPCArg::Optional::NO, "The new desired value of the spork"},
        },
        RPCResult{
            RPCResult::Type::STR, "result", "\"success\" if spork value was updated or this help otherwise"
        },
        RPCExamples{
            HelpExampleCli("sporkupdate", "SPORK_2_INSTANTSEND_ENABLED 4070908800")
            + HelpExampleRpc("sporkupdate", "\"SPORK_2_INSTANTSEND_ENABLED\", 4070908800")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{

    // advanced mode, update spork values
    SporkId nSporkID = CSporkManager::GetSporkIDByName(request.params[0].get_str());
    if (nSporkID == SPORK_INVALID) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid spork name");
    }

    const NodeContext& node = EnsureAnyNodeContext(request.context);
    PeerManager& peerman = EnsurePeerman(node);
    CHECK_NONFATAL(node.sporkman);

    // SPORK VALUE
    int64_t nValue = request.params[1].get_int64();

    // broadcast new spork
    if (node.sporkman->UpdateSpork(peerman, nSporkID, nValue)) {
        return "success";
    }

    return NullUniValue;
},
    };
}

static RPCHelpMan validateaddress()
{
    return RPCHelpMan{"validateaddress",
                "\nReturn information about the given Sparks address.\n",
                {
                    {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The Sparks address to validate"},
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::BOOL, "isvalid", "If the address is valid or not"},
                        {RPCResult::Type::STR, "address", "The Sparks address validated"},
                        {RPCResult::Type::STR_HEX, "scriptPubKey", "The hex-encoded scriptPubKey generated by the address"},
                        {RPCResult::Type::BOOL, "isscript", "If the key is a script"},
                        {RPCResult::Type::STR, "error", /* optional */ true, "Error message, if any"},
                    }
                },
                RPCExamples{
                    HelpExampleCli("validateaddress", "\"" + EXAMPLE_ADDRESS[0] + "\"") +
                    HelpExampleRpc("validateaddress", "\"" + EXAMPLE_ADDRESS[0] + "\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::string error_msg;
    CTxDestination dest = DecodeDestination(request.params[0].get_str(), error_msg);
    const bool isValid = IsValidDestination(dest);
    CHECK_NONFATAL(isValid == error_msg.empty());

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("isvalid", isValid);
    if (isValid) {
        std::string currentAddress = EncodeDestination(dest);
        ret.pushKV("address", currentAddress);

        CScript scriptPubKey = GetScriptForDestination(dest);
        ret.pushKV("scriptPubKey", HexStr(scriptPubKey));;

        UniValue detail = DescribeAddress(dest);
        ret.pushKVs(detail);
    } else {
        ret.pushKV("error", error_msg);
    }

    return ret;
},
    };
}

static RPCHelpMan createmultisig()
{
    return RPCHelpMan{"createmultisig",
                "\nCreates a multi-signature address with n signature of m keys required.\n"
                "It returns a json object with the address and redeemScript.\n",
                {
                    {"nrequired", RPCArg::Type::NUM, RPCArg::Optional::NO, "The number of required signatures out of the n keys."},
                    {"keys", RPCArg::Type::ARR, RPCArg::Optional::NO, "The hex-encoded public keys.",
                        {
                            {"key", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "The hex-encoded public key"},
                        }},
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::STR, "address", "The value of the new multisig address."},
                        {RPCResult::Type::STR_HEX, "redeemScript", "The string value of the hex-encoded redemption script."},
                        {RPCResult::Type::STR, "descriptor", "The descriptor for this multisig."},
                    }
                },
                RPCExamples{
            "\nCreate a multisig address from 2 public keys\n"
            + HelpExampleCli("createmultisig", "2 \"[\\\"03789ed0bb717d88f7d321a368d905e7430207ebbd82bd342cf11ae157a7ace5fd\\\",\\\"03dbc6764b8884a92e871274b87583e6d5c2a58819473e17e107ef3f6aa5a61626\\\"]\"") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("createmultisig", "2, \"[\\\"03789ed0bb717d88f7d321a368d905e7430207ebbd82bd342cf11ae157a7ace5fd\\\",\\\"03dbc6764b8884a92e871274b87583e6d5c2a58819473e17e107ef3f6aa5a61626\\\"]\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    int required = request.params[0].get_int();

    // Get the public keys
    const UniValue& keys = request.params[1].get_array();
    std::vector<CPubKey> pubkeys;
    for (unsigned int i = 0; i < keys.size(); ++i) {
        if (IsHex(keys[i].get_str()) && (keys[i].get_str().length() == 66 || keys[i].get_str().length() == 130)) {
            pubkeys.push_back(HexToPubKey(keys[i].get_str()));
        } else {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Invalid public key: %s\n.", keys[i].get_str()));
        }
    }

    // Construct using pay-to-script-hash:
    FillableSigningProvider keystore;
    CScript inner;
    const CTxDestination dest = AddAndGetMultisigDestination(required, pubkeys, keystore, inner);

    // Make the descriptor
    std::unique_ptr<Descriptor> descriptor = InferDescriptor(GetScriptForDestination(dest), keystore);

    UniValue result(UniValue::VOBJ);
    result.pushKV("address", EncodeDestination(dest));
    result.pushKV("redeemScript", HexStr(inner));
    result.pushKV("descriptor", descriptor->ToString());

    return result;
},
    };
}

static RPCHelpMan getdescriptorinfo()
{
    return RPCHelpMan{"getdescriptorinfo",
        {"\nAnalyses a descriptor.\n"},
        {
            {"descriptor", RPCArg::Type::STR, RPCArg::Optional::NO, "The descriptor"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
                {
                {RPCResult::Type::STR, "descriptor", "The descriptor in canonical form, without private keys"},
                {RPCResult::Type::STR, "checksum", "The checksum for the input descriptor"},
                {RPCResult::Type::BOOL, "isrange", "Whether the descriptor is ranged"},
                {RPCResult::Type::BOOL, "issolvable", "Whether the descriptor is solvable"},
                {RPCResult::Type::BOOL, "hasprivatekeys", "Whether the input descriptor contained at least one private key"},
                }
        },
        RPCExamples{
        "\nAnalyse a descriptor\n"
        + HelpExampleCli("getdescriptorinfo", "\"pkh([d34db33f/84h/0h/0h]0279be667ef9dcbbac55a06295Ce870b07029Bfcdb2dce28d959f2815b16f81798)\"")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{

    RPCTypeCheck(request.params, {UniValue::VSTR});

    FlatSigningProvider provider;
    std::string error;
    auto desc = Parse(request.params[0].get_str(), provider, error);
    if (!desc) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, error);
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("descriptor", desc->ToString());
    result.pushKV("checksum", GetDescriptorChecksum(request.params[0].get_str()));
    result.pushKV("isrange", desc->IsRange());
    result.pushKV("issolvable", desc->IsSolvable());
    result.pushKV("hasprivatekeys", provider.keys.size() > 0);
    return result;
},
    };
}

static RPCHelpMan deriveaddresses()
{
    return RPCHelpMan{"deriveaddresses",
        "\nDerives one or more addresses corresponding to an output descriptor.\n"
        "Examples of output descriptors are:\n"
        "    pkh(<pubkey>)                        P2PKH outputs for the given pubkey\n"
        "    sh(multi(<n>,<pubkey>,<pubkey>,...)) P2SH-multisig outputs for the given threshold and pubkeys\n"
        "    raw(<hex script>)                    Outputs whose scriptPubKey equals the specified hex scripts\n"
        "\nIn the above, <pubkey> either refers to a fixed public key in hexadecimal notation, or to an xpub/xprv optionally followed by one\n"
        "or more path elements separated by \"/\", where \"h\" represents a hardened child key.\n"
        "For more information on output descriptors, see the documentation in the doc/descriptors.md file.\n",
            {
                {"descriptor", RPCArg::Type::STR, RPCArg::Optional::NO, "The descriptor"},
                {"range", RPCArg::Type::RANGE, RPCArg::Optional::OMITTED_NAMED_ARG, "If a ranged descriptor is used, this specifies the end or the range (in [begin,end] notation) to derive."},
            },
            RPCResult{
                RPCResult::Type::ARR, "", "",
                {
                    {RPCResult::Type::STR, "address", "the derived addresses"},
                }
            },
            RPCExamples{
        "\nFirst three receive addresses\n"
        + HelpExampleCli("deriveaddresses", "\"pkh([d34db33f/84h/0h/0h]xpub6DJ2dNUysrn5Vt36jH2KLBT2i1auw1tTSSomg8PhqNiUtx8QX2SvC9nrHu81fT41fvDUnhMjEzQgXnQjKEu3oaqMSzhSrHMxyyoEAmUHQbY/0/*)#cjjspncu\" \"[0,2]\"")
            },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValueType()}); // Range argument is checked later
    const std::string desc_str = request.params[0].get_str();

    int64_t range_begin = 0;
    int64_t range_end = 0;

    if (request.params.size() >= 2 && !request.params[1].isNull()) {
        std::tie(range_begin, range_end) = ParseDescriptorRange(request.params[1]);
    }

    FlatSigningProvider key_provider;
    std::string error;
    auto desc = Parse(desc_str, key_provider, error, /* require_checksum = */ true);
    if (!desc) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, error);
    }

    if (!desc->IsRange() && request.params.size() > 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Range should not be specified for an un-ranged descriptor");
    }

    if (desc->IsRange() && request.params.size() == 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Range must be specified for a ranged descriptor");
    }

    UniValue addresses(UniValue::VARR);

    for (int i = range_begin; i <= range_end; ++i) {
        FlatSigningProvider provider;
        std::vector<CScript> scripts;
        if (!desc->Expand(i, key_provider, scripts, provider)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Cannot derive script without private keys"));
        }

        for (const CScript &script : scripts) {
            CTxDestination dest;
            if (!ExtractDestination(script, dest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Descriptor does not have a corresponding address"));
            }

            addresses.push_back(EncodeDestination(dest));
        }
    }

    // This should not be possible, but an assert seems overkill:
    if (addresses.empty()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Unexpected empty result");
    }

    return addresses;
},
    };
}

static RPCHelpMan verifymessage()
{
    return RPCHelpMan{"verifymessage",
        "\nVerify a signed message\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The Sparks address to use for the signature."},
            {"signature", RPCArg::Type::STR, RPCArg::Optional::NO, "The signature provided by the signer in base 64 encoding (see signmessage)."},
            {"message", RPCArg::Type::STR, RPCArg::Optional::NO, "The message that was signed."},
        },
        RPCResult{
            RPCResult::Type::BOOL, "", "If the signature is verified or not."
        },
        RPCExamples{
    "\nUnlock the wallet for 30 seconds\n"
    + HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") +
    "\nCreate the signature\n"
    + HelpExampleCli("signmessage", "\"" + EXAMPLE_ADDRESS[0] + "\" \"my message\"") +
    "\nVerify the signature\n"
    + HelpExampleCli("verifymessage", "\"" + EXAMPLE_ADDRESS[0] + "\" \"signature\" \"my message\"") +
    "\nAs a JSON-RPC call\n"
    + HelpExampleRpc("verifymessage", "\"" + EXAMPLE_ADDRESS[0] + "\", \"signature\", \"my message\"")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{

    LOCK(cs_main);

    std::string strAddress  = request.params[0].get_str();
    std::string strSign     = request.params[1].get_str();
    std::string strMessage  = request.params[2].get_str();

    switch (MessageVerify(strAddress, strSign, strMessage)) {
    case MessageVerificationResult::ERR_INVALID_ADDRESS:
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    case MessageVerificationResult::ERR_ADDRESS_NO_KEY:
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");
    case MessageVerificationResult::ERR_MALFORMED_SIGNATURE:
        throw JSONRPCError(RPC_TYPE_ERROR, "Malformed base64 encoding");
    case MessageVerificationResult::ERR_PUBKEY_NOT_RECOVERED:
    case MessageVerificationResult::ERR_NOT_SIGNED:
        return false;
    case MessageVerificationResult::OK:
        return true;
    }

    return false;
},
    };
}

static RPCHelpMan signmessagewithprivkey()
{
    return RPCHelpMan{"signmessagewithprivkey",
        "\nSign a message with the private key of an address\n",
        {
            {"privkey", RPCArg::Type::STR, RPCArg::Optional::NO, "The private key to sign the message with."},
            {"message", RPCArg::Type::STR, RPCArg::Optional::NO, "The message to create a signature of."},
        },
        RPCResult{
            RPCResult::Type::STR, "signature", "The signature of the message encoded in base 64"
        },
        RPCExamples{
    "\nCreate the signature\n"
    + HelpExampleCli("signmessagewithprivkey", "\"privkey\" \"my message\"") +
    "\nVerify the signature\n"
    + HelpExampleCli("verifymessage", "\"" + EXAMPLE_ADDRESS[0] + "\" \"signature\" \"my message\"") +
    "\nAs a JSON-RPC call\n"
    + HelpExampleRpc("signmessagewithprivkey", "\"privkey\", \"my message\"")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{

    std::string strPrivkey = request.params[0].get_str();
    std::string strMessage = request.params[1].get_str();

    CKey key = DecodeSecret(strPrivkey);
    if (!key.IsValid()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
    }

    std::string signature;

    if (!MessageSign(key, strMessage, signature)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Sign failed");
    }

    return signature;
},
    };
}

static RPCHelpMan setmocktime()
{
    return RPCHelpMan{"setmocktime",
        "\nSet the local time to given timestamp (-regtest only)\n",
        {
            {"timestamp", RPCArg::Type::NUM, RPCArg::Optional::NO, UNIX_EPOCH_TIME + "\n"
             "Pass 0 to go back to using the system time."},
        },
        RPCResult{RPCResult::Type::NONE, "", ""},
        RPCExamples{""},
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{

    if (!Params().IsMockableChain()) {
        throw std::runtime_error("setmocktime is for regression testing (-regtest mode) only");
    }

    // For now, don't change mocktime if we're in the middle of validation, as
    // this could have an effect on mempool time-based eviction, as well as
    // IsCurrentForFeeEstimation() and IsInitialBlockDownload().
    // TODO: figure out the right way to synchronize around mocktime, and
    // ensure all call sites of GetTime() are accessing this safely.
    LOCK(cs_main);

    RPCTypeCheck(request.params, {UniValue::VNUM});
    const int64_t time{request.params[0].get_int64()};
    if (time < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Mocktime can not be negative: %s.", time));
    }
    SetMockTime(time);
    if (auto* node_context = GetContext<NodeContext>(request.context)) {
        for (const auto& chain_client : node_context->chain_clients) {
            chain_client->setMockTime(time);
        }
    }

    return NullUniValue;
},
    };
}

static RPCHelpMan mnauth()
{
    return RPCHelpMan{"mnauth",
        "\nOverride MNAUTH processing results for the specified node with a user provided data (-regtest only).\n",
        {
            {"nodeId", RPCArg::Type::NUM, RPCArg::Optional::NO, "Internal peer id of the node the mock data gets added to."},
            {"proTxHash", RPCArg::Type::STR, RPCArg::Optional::NO, "The authenticated proTxHash as hex string."},
            {"publicKey", RPCArg::Type::STR, RPCArg::Optional::NO, "The authenticated public key as hex string."},
        },
        RPCResult{
            RPCResult::Type::BOOL, "result", "true, if the node was updated"
        },
        RPCExamples{""},
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{

    if (!Params().MineBlocksOnDemand())
        throw std::runtime_error("mnauth for regression testing (-regtest mode) only");

    int nodeId = ParseInt64V(request.params[0], "nodeId");
    uint256 proTxHash = ParseHashV(request.params[1], "proTxHash");
    if (proTxHash.IsNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "proTxHash invalid");
    }

    ChainstateManager& chainman = EnsureAnyChainman(request.context);

    CBLSPublicKey publicKey;
    const bool bls_legacy_scheme{!DeploymentActiveAfter(chainman.ActiveChain().Tip(), Params().GetConsensus(), Consensus::DEPLOYMENT_V19)};
    publicKey.SetHexStr(request.params[2].get_str(), bls_legacy_scheme);
    if (!publicKey.IsValid()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "publicKey invalid");
    }

    const NodeContext& node = EnsureAnyNodeContext(request.context);
    bool fSuccess = node.connman->ForNode(nodeId, CConnman::AllNodes, [&](CNode* pNode){
        pNode->SetVerifiedProRegTxHash(proTxHash);
        pNode->SetVerifiedPubKeyHash(publicKey.GetHash());
        return true;
    });

    return fSuccess;
},
    };
}

static bool getAddressFromIndex(const AddressType& type, const uint160 &hash, std::string &address)
{
    if (type == AddressType::P2SH) {
        address = EncodeDestination(ScriptHash(hash));
    } else if (type == AddressType::P2PK_OR_P2PKH) {
        address = EncodeDestination(PKHash(hash));
    } else {
        return false;
    }
    return true;
}

static bool getIndexKey(const std::string& str, uint160& hashBytes, AddressType& type)
{
    CTxDestination dest = DecodeDestination(str);
    if (!IsValidDestination(dest)) {
        type = AddressType::UNKNOWN;
        return false;
    }
    const PKHash *pkhash = std::get_if<PKHash>(&dest);
    const ScriptHash *scriptID = std::get_if<ScriptHash>(&dest);
    type = pkhash ? AddressType::P2PK_OR_P2PKH : AddressType::P2SH;
    hashBytes = pkhash ? uint160(*pkhash) : uint160(*scriptID);
    return true;
}

static bool getAddressesFromParams(const UniValue& params, std::vector<std::pair<uint160, AddressType> > &addresses)
{
    if (params[0].isStr()) {
        uint160 hashBytes;
        AddressType type{AddressType::UNKNOWN};
        if (!getIndexKey(params[0].get_str(), hashBytes, type)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
        }
        addresses.push_back(std::make_pair(hashBytes, type));
    } else if (params[0].isObject()) {

        UniValue addressValues = find_value(params[0].get_obj(), "addresses");
        if (!addressValues.isArray()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Addresses is expected to be an array");
        }

        for (const auto& address : addressValues.getValues()) {
            uint160 hashBytes;
            AddressType type{AddressType::UNKNOWN};
            if (!getIndexKey(address.get_str(), hashBytes, type)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
            }
            addresses.push_back(std::make_pair(hashBytes, type));
        }
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    return true;
}

static RPCHelpMan getaddressmempool()
{
    return RPCHelpMan{"getaddressmempool",
        "\nReturns all mempool deltas for an address (requires addressindex to be enabled).\n",
        {
            {"addresses", RPCArg::Type::ARR, /* default */ "", "",
                {
                    {"address", RPCArg::Type::STR, /* default */ "", "The base58check encoded address"},
                },
            },
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::OBJ, "", "",
                {
                    {RPCResult::Type::STR, "address", "The base58check encoded address"},
                    {RPCResult::Type::STR_HEX, "txid", "The related txid"},
                    {RPCResult::Type::NUM, "index", "The related input or output index"},
                    {RPCResult::Type::NUM, "satoshis", "The difference of duffs"},
                    {RPCResult::Type::NUM_TIME, "timestamp", "The time the transaction entered the mempool (seconds)"},
                    {RPCResult::Type::STR_HEX, "prevtxid", "The previous txid (if spending)"},
                    {RPCResult::Type::NUM, "prevout", "The previous transaction output index (if spending)"},
                }},
            }},
        RPCExamples{
            HelpExampleCli("getaddressmempool", "'{\"addresses\": [\"" + EXAMPLE_ADDRESS[0] + "\"]}'")
    + HelpExampleRpc("getaddressmempool", "{\"addresses\": [\"" + EXAMPLE_ADDRESS[0] + "\"]}")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    CTxMemPool& mempool = EnsureAnyMemPool(request.context);

    std::vector<std::pair<uint160, AddressType>> addresses;
    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    std::vector<CMempoolAddressDeltaKey> input_addresses;
    std::vector<CMempoolAddressDeltaEntry> indexes;
    for (const auto& [hash, type] : addresses) {
        input_addresses.push_back({type, hash});
    }
    if (!GetMempoolAddressDeltaIndex(mempool, input_addresses, indexes, /* timestamp_sort = */ true)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
    }

    UniValue result(UniValue::VARR);

    for (const auto& [mempoolAddressKey, mempoolAddressDelta] : indexes) {
        std::string address;
        if (!getAddressFromIndex(mempoolAddressKey.m_address_type, mempoolAddressKey.m_address_bytes, address)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");
        }

        UniValue delta(UniValue::VOBJ);
        delta.pushKV("address", address);
        delta.pushKV("txid", mempoolAddressKey.m_tx_hash.GetHex());
        delta.pushKV("index", (int)mempoolAddressKey.m_tx_index);
        delta.pushKV("satoshis", mempoolAddressDelta.m_amount);
        delta.pushKV("timestamp", count_seconds(mempoolAddressDelta.m_time));
        if (mempoolAddressDelta.m_amount < 0) {
            delta.pushKV("prevtxid", mempoolAddressDelta.m_prev_hash.GetHex());
            delta.pushKV("prevout", (int)mempoolAddressDelta.m_prev_out);
        }
        result.push_back(delta);
    }

    return result;
},
    };
}

static RPCHelpMan getaddressutxos()
{
    return RPCHelpMan{"getaddressutxos",
        "\nReturns all unspent outputs for an address (requires addressindex to be enabled).\n",
        {
            {"addresses", RPCArg::Type::ARR, /* default */ "", "",
                {
                    {"address", RPCArg::Type::STR, /* default */ "", "The base58check encoded address"},
                },
            },
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::OBJ, "", "",
                {
                    {RPCResult::Type::STR, "address", "The address base58check encoded"},
                    {RPCResult::Type::STR_HEX, "txid", "The output txid"},
                    {RPCResult::Type::NUM, "index", "The output index"},
                    {RPCResult::Type::STR_HEX, "script", "The script hex-encoded"},
                    {RPCResult::Type::NUM, "satoshis", "The number of duffs of the output"},
                    {RPCResult::Type::NUM, "height", "The block height"},
                }},
            }},
        RPCExamples{
            HelpExampleCli("getaddressutxos", "'{\"addresses\": [\"" + EXAMPLE_ADDRESS[0] + "\"]}'")
    + HelpExampleRpc("getaddressutxos", "{\"addresses\": [\"" + EXAMPLE_ADDRESS[0] + "\"]}")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{

    std::vector<std::pair<uint160, AddressType> > addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    std::vector<CAddressUnspentIndexEntry> unspentOutputs;

    {
        LOCK(::cs_main);
        for (const auto& address : addresses) {
            if (!GetAddressUnspentIndex(*pblocktree, address.first, address.second, unspentOutputs,
                                        /* height_sort = */ true)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
            }
        }
    }

    UniValue result(UniValue::VARR);

    for (const auto& [unspentKey, unspentValue] : unspentOutputs) {
        UniValue output(UniValue::VOBJ);
        std::string address;
        if (!getAddressFromIndex(unspentKey.m_address_type, unspentKey.m_address_bytes, address)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");
        }

        output.pushKV("address", address);
        output.pushKV("txid", unspentKey.m_tx_hash.GetHex());
        output.pushKV("outputIndex", (int)unspentKey.m_tx_index);
        output.pushKV("script", HexStr(unspentValue.m_tx_script));
        output.pushKV("satoshis", unspentValue.m_amount);
        output.pushKV("height", unspentValue.m_block_height);
        result.push_back(output);
    }

    return result;
},
    };
}

static RPCHelpMan getaddressdeltas()
{
    return RPCHelpMan{"getaddressdeltas",
        "\nReturns all changes for an address (requires addressindex to be enabled).\n",
        {
            {"addresses", RPCArg::Type::ARR, /* default */ "", "",
                {
                    {"address", RPCArg::Type::STR, /* default */ "", "The base58check encoded address"},
                },
            },
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::OBJ, "", "",
                {
                    {RPCResult::Type::NUM, "satoshis", "The difference of duffs"},
                    {RPCResult::Type::STR_HEX, "txid", "The related txid"},
                    {RPCResult::Type::NUM, "index", "The related input or output index"},
                    {RPCResult::Type::NUM, "blockindex", "The related block index"},
                    {RPCResult::Type::NUM, "height", "The block height"},
                    {RPCResult::Type::STR, "address", "The base58check encoded address"},
                }},
            }},
        RPCExamples{
            HelpExampleCli("getaddressdeltas", "'{\"addresses\": [\"" + EXAMPLE_ADDRESS[0] + "\"]}'")
    + HelpExampleRpc("getaddressdeltas", "{\"addresses\": [\"" + EXAMPLE_ADDRESS[0] + "\"]}")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{


    UniValue startValue = find_value(request.params[0].get_obj(), "start");
    UniValue endValue = find_value(request.params[0].get_obj(), "end");

    int start = 0;
    int end = 0;

    if (startValue.isNum() && endValue.isNum()) {
        start = startValue.get_int();
        end = endValue.get_int();
        if (end < start) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "End value is expected to be greater than start");
        }
    }

    std::vector<std::pair<uint160, AddressType> > addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    std::vector<CAddressIndexEntry> addressIndex;

    {
        LOCK(::cs_main);
        for (const auto& address : addresses) {
            if (start > 0 && end > 0) {
                if (!GetAddressIndex(*pblocktree, address.first, address.second, addressIndex, start, end)) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
                }
            } else {
                if (!GetAddressIndex(*pblocktree, address.first, address.second, addressIndex)) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
                }
            }
        }
    }

    UniValue result(UniValue::VARR);

    for (const auto& [indexKey, indexDelta] : addressIndex) {
        std::string address;
        if (!getAddressFromIndex(indexKey.m_address_type, indexKey.m_address_bytes, address)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");
        }

        UniValue delta(UniValue::VOBJ);
        delta.pushKV("satoshis", indexDelta);
        delta.pushKV("txid", indexKey.m_tx_hash.GetHex());
        delta.pushKV("index", (int)indexKey.m_tx_index);
        delta.pushKV("blockindex", (int)indexKey.m_block_tx_pos);
        delta.pushKV("height", indexKey.m_block_height);
        delta.pushKV("address", address);
        result.push_back(delta);
    }

    return result;
},
    };
}

static RPCHelpMan getaddressbalance()
{
    return RPCHelpMan{"getaddressbalance",
        "\nReturns the balance for an address(es) (requires addressindex to be enabled).\n",
        {
            {"addresses", RPCArg::Type::ARR, /* default */ "", "",
                {
                    {"address", RPCArg::Type::STR, /* default */ "", "The base58check encoded address"},
                },
            },
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
                {
                    {RPCResult::Type::NUM, "balance", "The current total balance in duffs"},
                    {RPCResult::Type::NUM, "balance_immature", "The current immature balance in duffs"},
                    {RPCResult::Type::NUM, "balance_spendable", "The current spendable balance in duffs"},
                    {RPCResult::Type::NUM, "received", "The total number of duffs received (including change)"},
                }},
        RPCExamples{
            HelpExampleCli("getaddressbalance", "'{\"addresses\": [\"" + EXAMPLE_ADDRESS[0] + "\"]}'")
    + HelpExampleRpc("getaddressbalance", "{\"addresses\": [\"" + EXAMPLE_ADDRESS[0] + "\"]}")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{

    std::vector<std::pair<uint160, AddressType> > addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    std::vector<CAddressIndexEntry> addressIndex;

    ChainstateManager& chainman = EnsureAnyChainman(request.context);

    int nHeight;
    {
        LOCK(::cs_main);
        for (const auto& address : addresses) {
            if (!GetAddressIndex(*pblocktree, address.first, address.second, addressIndex)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
            }
        }
        nHeight = chainman.ActiveChain().Height();
    }


    CAmount balance = 0;
    CAmount balance_spendable = 0;
    CAmount balance_immature = 0;
    CAmount received = 0;

    for (const auto& [indexKey, indexDelta] : addressIndex) {
        if (indexDelta > 0) {
            received += indexDelta;
        }
        if (indexKey.m_block_tx_pos == 0 && nHeight - indexKey.m_block_height < COINBASE_MATURITY) {
            balance_immature += indexDelta;
        } else {
            balance_spendable += indexDelta;
        }
        balance += indexDelta;
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("balance", balance);
    result.pushKV("balance_immature", balance_immature);
    result.pushKV("balance_spendable", balance_spendable);
    result.pushKV("received", received);

    return result;

},
    };
}

static RPCHelpMan getaddresstxids()
{
    return RPCHelpMan{"getaddresstxids",
        "\nReturns the txids for an address(es) (requires addressindex to be enabled).\n",
        {
            {"addresses", RPCArg::Type::ARR, /* default */ "", "",
                {
                    {"address", RPCArg::Type::STR, /* default */ "", "The base58check encoded address"},
                },
            },
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {{RPCResult::Type::STR_HEX, "transactionid", "The transaction id"}}
        },
        RPCExamples{
            HelpExampleCli("getaddresstxids", "'{\"addresses\": [\"" + EXAMPLE_ADDRESS[0] + "\"]}'")
    + HelpExampleRpc("getaddresstxids", "{\"addresses\": [\"" + EXAMPLE_ADDRESS[0] + "\"]}")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{

    std::vector<std::pair<uint160, AddressType> > addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    int start = 0;
    int end = 0;
    if (request.params[0].isObject()) {
        UniValue startValue = find_value(request.params[0].get_obj(), "start");
        UniValue endValue = find_value(request.params[0].get_obj(), "end");
        if (startValue.isNum() && endValue.isNum()) {
            start = startValue.get_int();
            end = endValue.get_int();
        }
    }

    std::vector<CAddressIndexEntry> addressIndex;

    {
        LOCK(::cs_main);
        for (const auto& address : addresses) {
            if (start > 0 && end > 0) {
                if (!GetAddressIndex(*pblocktree, address.first, address.second, addressIndex, start, end)) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
                }
            } else {
                if (!GetAddressIndex(*pblocktree, address.first, address.second, addressIndex)) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
                }
            }
        }
    }

    std::set<std::pair<int, std::string> > txids;
    UniValue result(UniValue::VARR);

    for (const auto& [indexKey, _]: addressIndex) {
        int height = indexKey.m_block_height;
        std::string txid = indexKey.m_tx_hash.GetHex();

        if (addresses.size() > 1) {
            txids.insert(std::make_pair(height, txid));
        } else {
            if (txids.insert(std::make_pair(height, txid)).second) {
                result.push_back(txid);
            }
        }
    }

    if (addresses.size() > 1) {
        for (const auto& tx : txids) {
            result.push_back(tx.second);
        }
    }

    return result;

},
    };
}

static RPCHelpMan getspentinfo()
{
    return RPCHelpMan{"getspentinfo",
        "\nReturns the txid and index where an output is spent.\n",
        {
            {"request", RPCArg::Type::OBJ, /* default */ "", "",
                {
                    {"txid", RPCArg::Type::STR_HEX, /* default */ "", "The hex string of the txid"},
                    {"index", RPCArg::Type::NUM, /* default */ "", "The start block height"},
                },
            },
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "txid", "The transaction id"},
                {RPCResult::Type::NUM, "index", "The spending input index"},
            }},
        RPCExamples{
            HelpExampleCli("getspentinfo", "'{\"txid\": \"0437cd7f8525ceed2324359c2d0ba26006d92d856a9c20fa0241106ee5a597c9\", \"index\": 0}'")
    + HelpExampleRpc("getspentinfo", "{\"txid\": \"0437cd7f8525ceed2324359c2d0ba26006d92d856a9c20fa0241106ee5a597c9\", \"index\": 0}")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{

    UniValue txidValue = find_value(request.params[0].get_obj(), "txid");
    UniValue indexValue = find_value(request.params[0].get_obj(), "index");

    if (!txidValue.isStr() || !indexValue.isNum()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid txid or index");
    }

    uint256 txid = ParseHashV(txidValue, "txid");
    int outputIndex = indexValue.get_int();

    CSpentIndexKey key(txid, outputIndex);
    CSpentIndexValue value;

    CTxMemPool& mempool = EnsureAnyMemPool(request.context);
    if (LOCK(::cs_main); !GetSpentIndex(*pblocktree, mempool, key, value)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unable to get spent info");
    }

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("txid", value.m_tx_hash.GetHex());
    obj.pushKV("index", (int)value.m_tx_index);
    obj.pushKV("height", value.m_block_height);

    return obj;
},
    };
}

static RPCHelpMan mockscheduler()
{
    return RPCHelpMan{"mockscheduler",
        "\nBump the scheduler into the future (-regtest only)\n",
        {
            {"delta_time", RPCArg::Type::NUM, RPCArg::Optional::NO, "Number of seconds to forward the scheduler into the future." },
        },
        RPCResults{},
        RPCExamples{""},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    if (!Params().IsMockableChain()) {
        throw std::runtime_error("mockscheduler is for regression testing (-regtest mode) only");
    }

    // check params are valid values
    RPCTypeCheck(request.params, {UniValue::VNUM});
    int64_t delta_seconds = request.params[0].get_int64();
    if (delta_seconds <= 0 || delta_seconds > 3600) {
        throw std::runtime_error("delta_time must be between 1 and 3600 seconds (1 hr)");
    }

    auto* node_context = GetContext<NodeContext>(request.context);
    // protect against null pointer dereference
    CHECK_NONFATAL(node_context);
    CHECK_NONFATAL(node_context->scheduler);
    node_context->scheduler->MockForward(std::chrono::seconds(delta_seconds));

    return NullUniValue;
},
    };
}

static UniValue RPCLockedMemoryInfo()
{
    LockedPool::Stats stats = LockedPoolManager::Instance().stats();
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("used", uint64_t(stats.used));
    obj.pushKV("free", uint64_t(stats.free));
    obj.pushKV("total", uint64_t(stats.total));
    obj.pushKV("locked", uint64_t(stats.locked));
    obj.pushKV("chunks_used", uint64_t(stats.chunks_used));
    obj.pushKV("chunks_free", uint64_t(stats.chunks_free));
    return obj;
}

#ifdef HAVE_MALLOC_INFO
static std::string RPCMallocInfo()
{
    char *ptr = nullptr;
    size_t size = 0;
    FILE *f = open_memstream(&ptr, &size);
    if (f) {
        malloc_info(0, f);
        fclose(f);
        if (ptr) {
            std::string rv(ptr, size);
            free(ptr);
            return rv;
        }
    }
    return "";
}
#endif

static RPCHelpMan getmemoryinfo()
{
    /* Please, avoid using the word "pool" here in the RPC interface or help,
     * as users will undoubtedly confuse it with the other "memory pool"
     */
    return RPCHelpMan{"getmemoryinfo",
        "Returns an object containing information about memory usage.\n",
        {
            {"mode", RPCArg::Type::STR, /* default */ "\"stats\"", "determines what kind of information is returned.\n"
    "  - \"stats\" returns general statistics about memory usage in the daemon.\n"
    "  - \"mallocinfo\" returns an XML string describing low-level heap state (only available if compiled with glibc 2.10+)."},
        },
        {
            RPCResult{"mode \"stats\"",
                RPCResult::Type::OBJ, "", "",
                {
                    {RPCResult::Type::OBJ, "locked", "Information about locked memory manager",
                    {
                        {RPCResult::Type::NUM, "used", "Number of bytes used"},
                        {RPCResult::Type::NUM, "free", "Number of bytes available in current arenas"},
                        {RPCResult::Type::NUM, "total", "Total number of bytes managed"},
                        {RPCResult::Type::NUM, "locked", "Amount of bytes that succeeded locking. If this number is smaller than total, locking pages failed at some point and key data could be swapped to disk."},
                        {RPCResult::Type::NUM, "chunks_used", "Number allocated chunks"},
                        {RPCResult::Type::NUM, "chunks_free", "Number unused chunks"},
                    }},
                }
            },
            RPCResult{"mode \"mallocinfo\"",
                RPCResult::Type::STR, "", "\"<malloc version=\"1\">...\""
            },
        },
        RPCExamples{
            HelpExampleCli("getmemoryinfo", "")
    + HelpExampleRpc("getmemoryinfo", "")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{

    std::string mode = request.params[0].isNull() ? "stats" : request.params[0].get_str();
    if (mode == "stats") {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("locked", RPCLockedMemoryInfo());
        return obj;
    } else if (mode == "mallocinfo") {
#ifdef HAVE_MALLOC_INFO
        return RPCMallocInfo();
#else
        throw JSONRPCError(RPC_INVALID_PARAMETER, "mallocinfo mode not available");
#endif
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "unknown mode " + mode);
    }
},
    };
}

static void EnableOrDisableLogCategories(UniValue cats, bool enable) {
    cats = cats.get_array();
    for (unsigned int i = 0; i < cats.size(); ++i) {
        std::string cat = cats[i].get_str();

        bool success;
        if (enable) {
            success = LogInstance().EnableCategory(cat);
        } else {
            success = LogInstance().DisableCategory(cat);
        }

        if (!success) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "unknown logging category " + cat);
        }
    }
}

static RPCHelpMan logging()
{
    return RPCHelpMan{"logging",
    "Gets and sets the logging configuration.\n"
    "When called without an argument, returns the list of categories with status that are currently being debug logged or not.\n"
    "When called with arguments, adds or removes categories from debug logging and return the lists above.\n"
    "The arguments are evaluated in order \"include\", \"exclude\".\n"
    "If an item is both included and excluded, it will thus end up being excluded.\n"
    "The valid logging categories are: " + LogInstance().LogCategoriesString() + "\n"
    "In addition, the following are available as category names with special meanings:\n"
    "  - \"all\",  \"1\" : represent all logging categories.\n"
    "  - \"sparks\" activates all Sparks-specific categories at once.\n"
    "To deactivate all categories at once you can specify \"all\" in <exclude>.\n"
    "  - \"none\", \"0\" : even if other logging categories are specified, ignore all of them.\n"
    ,
        {
            {"include", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG, "The of categories to add to debug logging",
                {
                    {"include_category", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "the valid logging category"},
                }},
            {"exclude", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG, "The categories to remove from debug logging",
                {
                    {"exclude_category", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "the valid logging category"},
                }},
        },
        RPCResult{
            RPCResult::Type::OBJ_DYN, "", "keys are the logging categories, and values indicates its status",
            {
                {RPCResult::Type::BOOL, "category", "if being debug logged or not. false:inactive, true:active"},
                    }
        },
        RPCExamples{
            HelpExampleCli("logging", "\"[\\\"all\\\"]\" \"[\\\"http\\\"]\"")
    + HelpExampleRpc("logging", "[\"all\"], \"[libevent]\"")
        },
    [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{

    uint64_t original_log_categories = LogInstance().GetCategoryMask();
    if (request.params[0].isArray()) {
        EnableOrDisableLogCategories(request.params[0], true);
    }
    if (request.params[1].isArray()) {
        EnableOrDisableLogCategories(request.params[1], false);
    }
    uint64_t updated_log_categories = LogInstance().GetCategoryMask();
    uint64_t changed_log_categories = original_log_categories ^ updated_log_categories;

    // Update libevent logging if BCLog::LIBEVENT has changed.
    // If the library version doesn't allow it, UpdateHTTPServerLogging() returns false,
    // in which case we should clear the BCLog::LIBEVENT flag.
    // Throw an error if the user has explicitly asked to change only the libevent
    // flag and it failed.
    if (changed_log_categories & BCLog::LIBEVENT) {
        if (!UpdateHTTPServerLogging(LogInstance().WillLogCategory(BCLog::LIBEVENT))) {
            LogInstance().DisableCategory(BCLog::LIBEVENT);
            if (changed_log_categories == BCLog::LIBEVENT) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "libevent logging cannot be updated when using libevent before v2.1.1.");
            }
        }
    }

    UniValue result(UniValue::VOBJ);
    for (const auto& logCatActive : LogInstance().LogCategoriesList()) {
        result.pushKV(logCatActive.category, logCatActive.active);
    }

    return result;
},
    };
}

static RPCHelpMan echo(const std::string& name)
{
    return RPCHelpMan{name,
                "\nSimply echo back the input arguments. This command is for testing.\n"
                "\nIt will return an internal bug report when arg9='trigger_internal_bug' is passed.\n"
                "\nThe difference between echo and echojson is that echojson has argument conversion enabled in the client-side table in "
                "sparks-cli and the GUI. There is no server-side difference.",
                {
                    {"arg0", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, ""},
                    {"arg1", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, ""},
                    {"arg2", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, ""},
                    {"arg3", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, ""},
                    {"arg4", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, ""},
                    {"arg5", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, ""},
                    {"arg6", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, ""},
                    {"arg7", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, ""},
                    {"arg8", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, ""},
                    {"arg9", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, ""},
                },
                RPCResult{RPCResult::Type::NONE, "", "Returns whatever was passed in"},
                RPCExamples{""},
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    if (request.fHelp) throw std::runtime_error(self.ToString());

    if (request.params[9].isStr()) {
        CHECK_NONFATAL(request.params[9].get_str() != "trigger_internal_bug");
    }
    return request.params;
},
    };
}

static UniValue SummaryToJSON(const IndexSummary&& summary, std::string index_name)
{
    UniValue ret_summary(UniValue::VOBJ);
    if (!index_name.empty() && index_name != summary.name) return ret_summary;

    UniValue entry(UniValue::VOBJ);
    entry.pushKV("synced", summary.synced);
    entry.pushKV("best_block_height", summary.best_block_height);
    ret_summary.pushKV(summary.name, entry);
    return ret_summary;
}

static RPCHelpMan getindexinfo()
{
    return RPCHelpMan{"getindexinfo",
                "\nReturns the status of one or all available indices currently running in the node.\n",
                {
                    {"index_name", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "Filter results for an index with a specific name."},
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "", {
                        {
                            RPCResult::Type::OBJ, "name", "The name of the index",
                            {
                                {RPCResult::Type::BOOL, "synced", "Whether the index is synced or not"},
                                {RPCResult::Type::NUM, "best_block_height", "The block height to which the index is synced"},
                            }
                        },
                    },
                },
                RPCExamples{
                    HelpExampleCli("getindexinfo", "")
                  + HelpExampleRpc("getindexinfo", "")
                  + HelpExampleCli("getindexinfo", "txindex")
                  + HelpExampleRpc("getindexinfo", "txindex")
                },
                [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    UniValue result(UniValue::VOBJ);
    const std::string index_name = request.params[0].isNull() ? "" : request.params[0].get_str();

    if (g_txindex) {
        result.pushKVs(SummaryToJSON(g_txindex->GetSummary(), index_name));
    }

    if (g_coin_stats_index) {
        result.pushKVs(SummaryToJSON(g_coin_stats_index->GetSummary(), index_name));
    }

    ForEachBlockFilterIndex([&result, &index_name](const BlockFilterIndex& index) {
        result.pushKVs(SummaryToJSON(index.GetSummary(), index_name));
    });

    return result;
}
    };
}

static RPCHelpMan echo() { return echo("echo"); }
static RPCHelpMan echojson() { return echo("echojson"); }

void RegisterMiscRPCCommands(CRPCTable &t)
{
static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
    { "control",            "debug",                  &debug,                  {"category"} },
    { "control",            "getmemoryinfo",          &getmemoryinfo,          {"mode"} },
    { "control",            "logging",                &logging,                {"include", "exclude"}},
    { "util",               "validateaddress",        &validateaddress,        {"address"} },
    { "util",               "createmultisig",         &createmultisig,         {"nrequired","keys"} },
    { "util",               "deriveaddresses",        &deriveaddresses,        {"descriptor", "range"} },
    { "util",               "getdescriptorinfo",      &getdescriptorinfo,      {"descriptor"} },
    { "util",               "verifymessage",          &verifymessage,          {"address","signature","message"} },
    { "util",               "signmessagewithprivkey", &signmessagewithprivkey, {"privkey","message"} },
    { "util",               "getindexinfo",           &getindexinfo,           {"index_name"} },
    { "blockchain",         "getspentinfo",           &getspentinfo,           {"request"} },

    /* Address index */
    { "addressindex",       "getaddressmempool",      &getaddressmempool,      {"addresses"}  },
    { "addressindex",       "getaddressutxos",        &getaddressutxos,        {"addresses"} },
    { "addressindex",       "getaddressdeltas",       &getaddressdeltas,       {"addresses"} },
    { "addressindex",       "getaddresstxids",        &getaddresstxids,        {"addresses"} },
    { "addressindex",       "getaddressbalance",      &getaddressbalance,      {"addresses"} },

    /* Sparks features */
    { "sparks",               "mnsync",                 &mnsync,                 {"mode"} },
    { "sparks",               "spork",                  &spork,                  {"command"} },
    { "sparks",               "sporkupdate",            &sporkupdate,            {"name","value"} },

    /* Not shown in help */
    { "hidden",             "setmocktime",            &setmocktime,            {"timestamp"}},
    { "hidden",             "mockscheduler",          &mockscheduler,          {"delta_time"}},
    { "hidden",             "echo",                   &echo,                   {"arg0","arg1","arg2","arg3","arg4","arg5","arg6","arg7","arg8","arg9"}},
    { "hidden",             "echojson",               &echojson,               {"arg0","arg1","arg2","arg3","arg4","arg5","arg6","arg7","arg8","arg9"}},
    { "hidden",             "mnauth",                 &mnauth,                 {"nodeId", "proTxHash", "publicKey"}},
};
// clang-format on
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}