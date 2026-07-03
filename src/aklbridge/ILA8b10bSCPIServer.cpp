/***********************************************************************************************************************
*                                                                                                                      *
* aklbridge                                                                                                           *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg                                                                          *
* All rights reserved.                                                                                                 *
*                                                                                                                      *
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the     *
* following conditions are met:                                                                                        *
*                                                                                                                      *
*    * Redistributions of source code must retain the above copyright notice, this list of conditions, and the         *
*      following disclaimer.                                                                                           *
*                                                                                                                      *
*    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the       *
*      following disclaimer in the documentation and/or other materials provided with the distribution.                *
*                                                                                                                      *
*    * Neither the name of the author nor the names of any contributors may be used to endorse or promote products     *
*      derived from this software without specific prior written permission.                                           *
*                                                                                                                      *
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED   *
* TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL *
* THE AUTHORS BE HELD LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES        *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/

#include "aklbridge.h"
#include "ILA8b10bSCPIServer.h"
#include <stdexcept>
#include <string.h>
#include <algorithm>
#include <cinttypes>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ILA8b10bSCPIServer::ILA8b10bSCPIServer(ZSOCKET sock, uint32_t baseAddress)
	: SCPIServer(sock)
	, m_baseAddress(baseAddress)
	, m_triggerArmed(false)
{
	LogTrace("Initializing ILA8b10b at 0x%08x\n", baseAddress);
	LogIndenter li;

	//Read status
	m_dataBaseAddress = ReadRegister(baseAddress + 0x8);
	LogTrace("Data buffer is at 0x%08x\n", m_dataBaseAddress);

	m_depth = ReadRegister(baseAddress + 0xc);
	uint32_t rows = m_depth / 4;
	uint32_t words = rows * 2;
	LogTrace("Depth is %u symbols (%u rows, %u words)\n", m_depth, rows, words);

	//Read trigger status
	auto trigstat = ReadRegister(baseAddress + 0x0);
	LogTrace("Trigger status = %08x\n", trigstat);

	if(trigstat & 2)
		m_triggerArmed = true;
	else
		m_triggerArmed = false;
}

ILA8b10bSCPIServer::~ILA8b10bSCPIServer()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command processing

bool ILA8b10bSCPIServer::OnCommand(
	[[maybe_unused]] const string& line,
	const string& subject,
    const string& cmd,
    const vector<string>& args)
{
	if(subject == "TRIG")
	{
		if(cmd == "ARM")
		{
			LogTrace("Arming\n");
			WriteRegister(m_baseAddress + 0, 0x1);
			m_triggerArmed = true;
		}

		else
			return false;
	}

	else
		return false;


	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Query processing

string ILA8b10bSCPIServer::GetMake()
{
	return "Antikernel Labs";
}

string ILA8b10bSCPIServer::GetModel()
{
	return "APB ILA8b10b";
}

string ILA8b10bSCPIServer::GetSerial()
{
	return "None";
}

string ILA8b10bSCPIServer::GetFirmwareVersion()
{
	return "1.0";
}

bool ILA8b10bSCPIServer::OnQuery(
	[[maybe_unused]] const string& line,
	const string& subject,
	const string& cmd)
{
	//Read ID code
	if(cmd == "*IDN")
		SendReply(GetMake() + "," + GetModel() + "," + GetSerial() + "," + GetFirmwareVersion());

	else if(subject == "MEM")
	{
		if(cmd == "DEPTH")
			SendReply(to_string(m_depth));
		else
			return false;
	}

	else if(subject == "TRIG")
	{
		if(cmd == "STAT")
		{
			auto trigstat = ReadRegister(m_baseAddress + 0x0);
			if(trigstat & 2)
			{
				SendReply("READY");
				m_triggerArmed = false;
			}
			else if(m_triggerArmed)
				SendReply("ARM");
			else
				SendReply("STOP");
		}

		//TODO: query trigger position

		else
			return false;
	}

	else if(cmd == "DATA")
	{
		uint32_t rows = m_depth / 4;
		auto trigsample = ReadRegister(m_baseAddress + 0x4);
		LogTrace("Trigger index = %d\n", trigsample);

		//for now assume trigger position is the midpoint of the buffer
		uint32_t bufstart = (trigsample - (rows / 2)) % rows;

		//Read the entire buffer
		vector<uint32_t> rxbuf;
		rxbuf.resize(rows*2);
		ReadRegisterBulk(m_dataBaseAddress, rows*2, &rxbuf[0]);

		//Convert to int64
		vector<uint64_t> buf;
		buf.resize(rows);
		for(uint32_t i=0; i<rows; i++)
			buf[i] = (static_cast<uint64_t>(rxbuf[i*2 + 1]) << 32) | rxbuf[i*2];

		//Stream it out to the client after offsetting
		for(uint32_t i=0; i<rows; i++)
			SendReply(to_string_hex(buf[(bufstart + i) % rows]));

		//Reset the trigger system for next round
		WriteRegister(m_baseAddress + 0, 0x0);
	}

	//Nope, invalid command
	else
		return false;

	//If we get here all good
	return true;
}
