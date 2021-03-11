/*__________________________________________________________________________________________

            (c) Hash(BEGIN(Satoshi[2010]), END(Sunny[2012])) == Videlicet[2014] ++

            (c) Copyright The Nexus Developers 2014 - 2019

            Distributed under the MIT software license, see the accompanying
            file COPYING or http://www.opensource.org/licenses/mit-license.php.

            "ad vocem populi" - To the Voice of the People

____________________________________________________________________________________________*/

#pragma once

#include <atomic>

namespace util::atomic
{
	typedef std::atomic<bool>       bool_t;
	typedef std::atomic<uint8_t>   uint8_t;
	typedef std::atomic<uint16_t> uint16_t;
	typedef std::atomic<uint32_t> uint32_t;
	typedef std::atomic<uint64_t> uint64_t;
}

