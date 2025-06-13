// Copyright (c) 2018-2023 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evo/dmnstate.h>
#include <evo/providertx.h>

#include <chainparams.h>
#include <consensus/validation.h>
#include <script/standard.h>
#include <validationinterface.h>

#include <univalue.h>
#include <messagesigner.h>

std::string CDeterministicMNState::ToString() const
{
    CTxDestination dest;
    std::string payoutAddress = "unknown";
    std::string operatorPayoutAddress = "none";
    if (ExtractDestination(scriptPayout, dest)) {
        payoutAddress = EncodeDestination(dest);
    }
    if (ExtractDestination(scriptOperatorPayout, dest)) {
        operatorPayoutAddress = EncodeDestination(dest);
    }

    return strprintf("CDeterministicMNState(nVersion=%d, nRegisteredHeight=%d, nLastPaidHeight=%d, nPoSePenalty=%d, nPoSeRevivedHeight=%d, nPoSeBanHeight=%d, nRevocationReason=%d, "
                     "ownerAddress=%s, pubKeyOperator=%s, votingAddress=%s, addr=%s, payoutAddress=%s, operatorPayoutAddress=%s)",
                     nVersion, nRegisteredHeight, nLastPaidHeight, nPoSePenalty, nPoSeRevivedHeight, nPoSeBanHeight, nRevocationReason,
                     EncodeDestination(PKHash(keyIDOwner)), pubKeyOperator.ToString(), EncodeDestination(PKHash(keyIDVoting)), addr.ToStringIPPort(), payoutAddress, operatorPayoutAddress);
}

UniValue CDeterministicMNState::ToJson(MnType nType) const
{
    UniValue obj;
    obj.setObject();
    obj.pushKV("version", nVersion);
    obj.pushKV("service", addr.ToStringIPPort());
    obj.pushKV("registeredHeight", nRegisteredHeight);
    obj.pushKV("lastPaidHeight", nLastPaidHeight);
    obj.pushKV("consecutivePayments", nConsecutivePayments);
    obj.pushKV("PoSePenalty", nPoSePenalty);
    obj.pushKV("PoSeRevivedHeight", nPoSeRevivedHeight);
    obj.pushKV("PoSeBanHeight", nPoSeBanHeight);
    obj.pushKV("revocationReason", nRevocationReason);
    obj.pushKV("ownerAddress", EncodeDestination(PKHash(keyIDOwner)));
    obj.pushKV("votingAddress", EncodeDestination(PKHash(keyIDVoting)));
    if (nType == MnType::Evo) {
        obj.pushKV("platformNodeID", platformNodeID.ToString());
        obj.pushKV("platformP2PPort", platformP2PPort);
        obj.pushKV("platformHTTPPort", platformHTTPPort);
    }

    CTxDestination dest;
    if (ExtractDestination(scriptPayout, dest)) {
        obj.pushKV("payoutAddress", EncodeDestination(dest));
    }
    obj.pushKV("pubKeyOperator", pubKeyOperator.ToString());
    if (ExtractDestination(scriptOperatorPayout, dest)) {
        obj.pushKV("operatorPayoutAddress", EncodeDestination(dest));
    }
    return obj;
}

UniValue CDeterministicMNStateDiff::ToJson(MnType nType) const
{
    UniValue obj;
    obj.setObject();
    if (fields & Field_nVersion) {
        obj.pushKV("version", state.nVersion);
    }
    if (fields & Field_addr) {
        obj.pushKV("service", state.addr.ToStringIPPort());
    }
    if (fields & Field_nRegisteredHeight) {
        obj.pushKV("registeredHeight", state.nRegisteredHeight);
    }
    if (fields & Field_nLastPaidHeight) {
        obj.pushKV("lastPaidHeight", state.nLastPaidHeight);
    }
    if (fields & Field_nConsecutivePayments) {
        obj.pushKV("consecutivePayments", state.nConsecutivePayments);
    }
    if (fields & Field_nPoSePenalty) {
        obj.pushKV("PoSePenalty", state.nPoSePenalty);
    }
    if (fields & Field_nPoSeRevivedHeight) {
        obj.pushKV("PoSeRevivedHeight", state.nPoSeRevivedHeight);
    }
    if (fields & Field_nPoSeBanHeight) {
        obj.pushKV("PoSeBanHeight", state.nPoSeBanHeight);
    }
    if (fields & Field_nRevocationReason) {
        obj.pushKV("revocationReason", state.nRevocationReason);
    }
    if (fields & Field_keyIDOwner) {
        obj.pushKV("ownerAddress", EncodeDestination(PKHash(state.keyIDOwner)));
    }
    if (fields & Field_keyIDVoting) {
        obj.pushKV("votingAddress", EncodeDestination(PKHash(state.keyIDVoting)));
    }
    if (fields & Field_scriptPayout) {
        CTxDestination dest;
        if (ExtractDestination(state.scriptPayout, dest)) {
            obj.pushKV("payoutAddress", EncodeDestination(dest));
        }
    }
    if (fields & Field_scriptOperatorPayout) {
        CTxDestination dest;
        if (ExtractDestination(state.scriptOperatorPayout, dest)) {
            obj.pushKV("operatorPayoutAddress", EncodeDestination(dest));
        }
    }
    if (fields & Field_pubKeyOperator) {
        obj.pushKV("pubKeyOperator", state.pubKeyOperator.ToString());
    }
    if (nType == MnType::Evo) {
        if (fields & Field_platformNodeID) {
            obj.pushKV("platformNodeID", state.platformNodeID.ToString());
        }
        if (fields & Field_platformP2PPort) {
            obj.pushKV("platformP2PPort", state.platformP2PPort);
        }
        if (fields & Field_platformHTTPPort) {
            obj.pushKV("platformHTTPPort", state.platformHTTPPort);
        }
    }
    return obj;
}
