/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#include <LLD/include/global.h>

#include <TAO/Operation/include/enum.h>
#include <TAO/Operation/include/operations.h>

#include <TAO/Register/include/enum.h>
#include <TAO/Register/include/state.h>
#include <TAO/Register/objects/account.h>
#include <TAO/Register/objects/token.h>

/* Global TAO namespace. */
namespace TAO
{

    /* Operation Layer namespace. */
    namespace Operation
    {

        /* Commits funds from an account to an account */
        bool Credit(const uint512_t &hashTx, const uint256_t &hashProof, const uint256_t &hashAccount, const uint64_t nAmount, const uint256_t &hashCaller, const uint8_t nFlags, TAO::Ledger::Transaction &tx)
        {

            /* Read the claimed transaction. */
            TAO::Ledger::Transaction txSpend;
            if(!LLD::legDB->ReadTx(hashTx, txSpend))
                return debug::error(FUNCTION, hashTx.ToString(), " tx doesn't exist");

            /* Extract the state from tx. */
            uint8_t TX_OP;
            txSpend.ssOperation >> TX_OP;

            /* Read the binary data of the Register. */
            TAO::Register::State state;

            /* Write pre-states. */
            if((nFlags & TAO::Register::FLAGS::PRESTATE))
            {
                if(!LLD::regDB->ReadState(hashAccount, state))
                    return debug::error(FUNCTION, "register address doesn't exist ", hashAccount.ToString());

                tx.ssRegister << (uint8_t)TAO::Register::STATES::PRESTATE << state;
            }

            /* Get pre-states on write. */
            if(nFlags & TAO::Register::FLAGS::WRITE  || nFlags & TAO::Register::FLAGS::MEMPOOL)
            {
                /* Get the state byte. */
                uint8_t nState = 0; //RESERVED
                tx.ssRegister >> nState;

                /* Check for the pre-state. */
                if(nState != TAO::Register::STATES::PRESTATE)
                    return debug::error(FUNCTION, "register script not in pre-state");

                /* Get the pre-state. */
                tx.ssRegister >> state;
            }

            /* Check that the creditor has permissions. */
            if(state.hashOwner != hashCaller)
                return debug::error(FUNCTION, "not authorized to credit to this register");

            /* Check that prev is coinbase. */
            if(TX_OP == TAO::Operation::OP::COINBASE) //NOTE: thie coinbase can't be spent unless flag is byte 0. Safe to use this for coinbase flag.
            {
                /* Check if this is a whole credit that the transaction is not already connected. */
                if(LLD::legDB->HasProof(hashCaller, hashTx, nFlags))
                    return debug::error(FUNCTION, "transaction is already spent");

                /* Get the coinbase amount. */
                uint64_t nCredit;
                txSpend.ssOperation >> nCredit;

                /* Make sure the claimed account is the debited account. */
                if(txSpend.hashGenesis != hashCaller)
                    return debug::error(FUNCTION, "cannot claim coinbase from different sigchain");

                /* Get the account being sent to. */
                TAO::Register::Account acctTo;
                state >> acctTo;

                /* Check the account identifier. */
                if(acctTo.nIdentifier != 0)
                    return debug::error(FUNCTION, "can't credit a coinbase for identifier other than 0");

                /* Check that the balances match. */
                if(nAmount != nCredit)
                    return debug::error(FUNCTION, "credit ", nCredit, "and coinbase ", nAmount, " amounts mismatch");

                /* Credit account balance. */
                acctTo.nBalance += nAmount;

                /* Write new state to the regdb. */
                state.ClearState();
                state.nTimestamp = tx.nTimestamp;
                state << acctTo;

                /* Check that the register is in a valid state. */
                if(!state.IsValid())
                    return debug::error(FUNCTION, "memory address ", hashAccount.ToString(), " is in invalid state");

                /* Write post-state checksum. */
                if((nFlags & TAO::Register::FLAGS::POSTSTATE))
                    tx.ssRegister << (uint8_t)TAO::Register::STATES::POSTSTATE << state.GetHash();

                /* Verify the post-state checksum. */
                if(nFlags & TAO::Register::FLAGS::WRITE || nFlags & TAO::Register::FLAGS::MEMPOOL)
                {
                    /* Get the state byte. */
                    uint8_t nState = 0; //RESERVED
                    tx.ssRegister >> nState;

                    /* Check for the pre-state. */
                    if(nState != TAO::Register::STATES::POSTSTATE)
                        return debug::error(FUNCTION, "register script not in post-state");

                    /* Get the post state checksum. */
                    uint64_t nChecksum;
                    tx.ssRegister >> nChecksum;

                    /* Check for matching post states. */
                    if(nChecksum != state.GetHash())
                        return debug::error(FUNCTION, "register script has invalid post-state");

                    /* Write the proof spend. */
                    if((nFlags & TAO::Register::FLAGS::WRITE) && !LLD::legDB->WriteProof(hashCaller, hashTx, nFlags))
                        return debug::error(FUNCTION, "failed to write proof");

                    /* Write the register to the database. */
                    if((nFlags & TAO::Register::FLAGS::WRITE) && !LLD::regDB->WriteState(hashAccount, state))
                        return debug::error(FUNCTION, "failed to write new state");
                }

                return true;
            }

            /* Check that prev is debit. */
            else if(TX_OP != TAO::Operation::OP::DEBIT)
                return debug::error(FUNCTION,  hashTx.ToString(), " tx claim is not a debit");

            /* Get the debit from account. */
            uint256_t hashFrom;
            txSpend.ssOperation >> hashFrom;

            /* Get the debit to account. */
            uint256_t hashTo;
            txSpend.ssOperation >> hashTo;

            /* Read the account to state. */
            if(!LLD::regDB->ReadState(hashTo, state))
                return debug::error(FUNCTION, "couldn't read debit to address");

            /* Credits specific to account objects. */
            if(state.nType == TAO::Register::STATE::ACCOUNT)
            {
                /* Check if this is a whole credit that the transaction is not already spent. */
                if(LLD::legDB->HasProof(hashAccount, hashTx, nFlags))
                    return debug::error(FUNCTION, "transaction is already spent");

                /* Check the proof as being the caller. */
                if(hashProof != hashCaller)
                    return debug::error(FUNCTION, "hash proof and caller mismatch");

                /* Read the to account state. */
                if(hashTo != hashAccount)
                    return debug::error(FUNCTION, "debit to account mismatch with credit to account");

                /* Read the state from. */
                TAO::Register::State stateFrom;
                if(!LLD::regDB->ReadState(hashFrom, stateFrom))
                     return debug::error(FUNCTION, "can't read state from");

                /* Check the token identifiers. */
                TAO::Register::Account acctFrom;
                stateFrom >> acctFrom;

                /* Get the account to. */
                TAO::Register::Account acctTo;
                state >> acctTo;

                /* Check token identifiers. */
                if(acctFrom.nIdentifier != acctTo.nIdentifier)
                    return debug::error(FUNCTION, "credit can't be of different identifier");

                /* Get the debit amount. */
                uint64_t nTotal;
                txSpend.ssOperation >> nTotal;

                /* Check the proper balance requirements. */
                if(nAmount != nTotal)
                     return debug::error(FUNCTION, "credit and debit totals don't match");

                /* Credit account balance. */
                acctTo.nBalance += nAmount;

                /* Write new state to the regdb. */
                state.ClearState();
                state.nTimestamp = tx.nTimestamp;
                state << acctTo;

                /* Check that the register is in a valid state. */
                if(!state.IsValid())
                    return debug::error(FUNCTION, "memory address ", hashAccount.ToString(), " is in invalid state");

                /* Write post-state checksum. */
                if((nFlags & TAO::Register::FLAGS::POSTSTATE))
                    tx.ssRegister << (uint8_t)TAO::Register::STATES::POSTSTATE << state.GetHash();

                /* Verify the post-state checksum. */
                if(nFlags & TAO::Register::FLAGS::WRITE || nFlags & TAO::Register::FLAGS::MEMPOOL)
                {
                    /* Get the state byte. */
                    uint8_t nState = 0; //RESERVED
                    tx.ssRegister >> nState;

                    /* Check for the pre-state. */
                    if(nState != TAO::Register::STATES::POSTSTATE)
                        return debug::error(FUNCTION, "register script not in post-state");

                    /* Get the post state checksum. */
                    uint64_t nChecksum;
                    tx.ssRegister >> nChecksum;

                    /* Check for matching post states. */
                    if(nChecksum != state.GetHash())
                        return debug::error(FUNCTION, "register script has invalid post-state");

                    /* Write the proof spend. */
                    if(!LLD::legDB->WriteProof(hashAccount, hashTx, nFlags))
                        return debug::error(FUNCTION, "failed to write proof");

                    /* Write the register to the database. */
                    if((nFlags & TAO::Register::FLAGS::WRITE) && !LLD::regDB->WriteState(hashAccount, state))
                        return debug::error(FUNCTION, "failed to write new state");
                }
            }
            else if(state.nType == TAO::Register::STATE::RAW || state.nType == TAO::Register::STATE::READONLY)
            {

                /* Get the state register of this register's owner. */
                TAO::Register::State stateOwner;
                if(!LLD::regDB->ReadState(state.hashOwner, stateOwner))
                    return debug::error(FUNCTION, "credit from raw object can't be without owner");

                /* Disable any account that's not owned by a token (for now). */
                if(stateOwner.nType != TAO::Register::STATE::TOKEN)
                    return debug::error(FUNCTION, "credit from raw object can't be owned by non token");

                /* Get the token object. */
                TAO::Register::Token token;
                stateOwner >> token;

                /* Check that this proof has not been used in a partial credit. */
                if(LLD::legDB->HasProof(hashProof, hashTx, nFlags))
                    return debug::error(FUNCTION, "temporal proof has already been spent");

                /* Check the state register that is being used as proof from creditor. */
                TAO::Register::State stateProof;
                if(!LLD::regDB->ReadState(hashProof, stateProof))
                    return debug::error(FUNCTION, "temporal proof register is not found");

                /* Compare the last timestamp update to transaction timestamp. */
                if(stateProof.nTimestamp > txSpend.nTimestamp)
                    return debug::error(FUNCTION, "temporal proof is stale");

                /* Check that the proof is an account being used. */
                if(stateProof.nType != TAO::Register::STATE::ACCOUNT)
                    return debug::error(FUNCTION, "temporal proof register must be account");

                /* Check the ownership of proof register. */
                if(stateProof.hashOwner != hashCaller)
                    return debug::error(FUNCTION, "not authorized to use this temporal proof");

                /* Get the proof account object. */
                TAO::Register::Account acctProof;
                stateProof >> acctProof;

                /* Check that the token indetifier matches token identifier. */
                if(acctProof.nIdentifier != token.nIdentifier)
                    return debug::error(FUNCTION, "account proof identifier not token identifier");

                /* Make sure the account to is an object account (for now - otherwise you can have chans of chains of chains). */
                if(state.nType != TAO::Register::STATE::ACCOUNT)
                    return debug::error(FUNCTION, "credit register is not of account type");

                /* Get the total amount of the debit. */
                uint64_t nDebit;
                txSpend.ssOperation >> nDebit;

                /* Get the total tokens to be distributed. */
                uint64_t nTotal = (acctProof.nBalance * nDebit) / token.nMaxSupply;
                //NOTE: ISSUE here, temporal proofs can't be used if post timestamped. This prevents double spending,
                //but it doesn't prevent coins from getting locked if a temporal proof has been changed. Possible to
                //check tokens to ensure tokens aren't moved if a proof is able to be claimed.
                //find a way to unlock the unspent tokens possibly with validation regitser.
                //We might need to check back the history to find timestamp before proof to use previous state.
                //This will work for now, but is not production ready.

                /* Check that the required credit claim is accurate. */
                if(nTotal != nAmount)
                    return debug::error(FUNCTION, "claimed credit ", nAmount, " mismatch with token holdings ", nTotal);

                /* Get the account being credited. */
                TAO::Register::Account acctTo;
                state >> acctTo;

                /* Read the state from. */
                TAO::Register::State stateFrom;
                if(!LLD::regDB->ReadState(hashFrom, stateFrom))
                    return debug::error(FUNCTION, "can't read state from");

                /* Get the debiter's register. */
                TAO::Register::Account acctFrom;
                stateFrom >> acctFrom;

                /* Check that the debit to credit identifiers match. */
                if(acctFrom.nIdentifier != acctTo.nIdentifier)
                    return debug::error(FUNCTION, "credit can't be of different identifier");

                /* Update the state account balance. */
                acctTo.nBalance += nAmount;

                /* Update the register. */
                state.ClearState();
                state.nTimestamp = tx.nTimestamp;
                state << acctTo;

                /* Check that the register is in a valid state. */
                if(!state.IsValid())
                    return debug::error(FUNCTION, "memory address ", hashAccount.ToString(), " is in invalid state");

                /* Write post-state checksum. */
                if((nFlags & TAO::Register::FLAGS::POSTSTATE))
                    tx.ssRegister << (uint8_t)TAO::Register::STATES::POSTSTATE << state.GetHash();

                /* Verify the post-state checksum. */
                if(nFlags & TAO::Register::FLAGS::WRITE || nFlags & TAO::Register::FLAGS::MEMPOOL)
                {
                    /* Get the state byte. */
                    uint8_t nState = 0; //RESERVED
                    tx.ssRegister >> nState;

                    /* Check for the pre-state. */
                    if(nState != TAO::Register::STATES::POSTSTATE)
                        return debug::error(FUNCTION, "register script not in post-state");

                    /* Get the post state checksum. */
                    uint64_t nChecksum;
                    tx.ssRegister >> nChecksum;

                    /* Check for matching post states. */
                    if(nChecksum != state.GetHash())
                        return debug::error(FUNCTION, "register script has invalid post-state");

                    /* Write the proof spend. */
                    if(!LLD::legDB->WriteProof(hashAccount, hashTx, nFlags))
                        return debug::error(FUNCTION, "failed to write proof");

                    /* Write the register to the database. */
                    if((nFlags & TAO::Register::FLAGS::WRITE) && !LLD::regDB->WriteState(hashAccount, state))
                        return debug::error(FUNCTION, "failed to write new state");
                }
            }

            return true;
        }
    }
}
