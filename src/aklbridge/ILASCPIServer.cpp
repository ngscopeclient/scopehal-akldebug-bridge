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
#include "ILASCPIServer.h"
#include <stdexcept>
#include <string.h>
#include <algorithm>
#include <cinttypes>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ILASCPIServer::ILASCPIServer(ZSOCKET sock, uint32_t baseAddress)
	: SCPIServer(sock)
	, m_baseAddress(baseAddress)
	, m_depth(0)
	, m_period(0)
	, m_triggerArmed(false)
{
	LogTrace("Initializing ILA at 0x%08x\n", baseAddress);
	LogIndenter li;

	//Read status
	m_dataBaseAddress = ReadRegister(baseAddress + 0x8);
	LogTrace("Data buffer is at 0x%08x\n", m_dataBaseAddress);

	m_depth = ReadRegister(baseAddress + 0xc);
	LogTrace("Depth is %u words\n", m_depth);

	m_period = ReadRegister(baseAddress + 0x10);
	LogTrace("Sample period is %u ps\n", m_period);

	m_triggerIdx = ReadRegister(baseAddress + 0x14);
	LogTrace("Trigger is at sample %u\n", m_triggerIdx);

	//Read trigger status
	auto trigstat = ReadRegister(baseAddress + 0x0);
	LogTrace("Trigger status = %08x\n", trigstat);

	if(trigstat & 2)
		m_triggerArmed = true;
	else
		m_triggerArmed = false;

	//Enumerate probe ports
	LogTrace("Probes\n");
	uint32_t totalProbeWidth = 0;
	for(uint32_t iport = 0; iport < 16; iport ++)
	{
		LogIndenter li2;

		uint32_t nameBlock[8];
		for(uint32_t j=0; j < 8; j ++)
			nameBlock[j] = ReadRegister(baseAddress + 0x1000 + 0x20*iport + 4*j);

		char name[33] = {0};
		memcpy(name, nameBlock, 32);

		//Extract width packed into the same register
		uint32_t width = name[31];
		name[31] = '\0';

		string sname(name);
		if(sname.empty())
			break;
		reverse(sname.begin(), sname.end());

		m_names.push_back(sname);
		m_widths.push_back(width);

		LogTrace("[%u] name=%-16s width=%d\n", iport, sname.c_str(), width);
		totalProbeWidth += width;
	}

	LogTrace("Total probe width is %u bits\n", totalProbeWidth);

	uint32_t probeWidthRounded = totalProbeWidth;
	if(probeWidthRounded & 0x1f)
	{
		probeWidthRounded |= 0x1f;
		probeWidthRounded ++;
	}
	m_wordsPerRow = probeWidthRounded / 32;
	LogTrace("Rounded width is %u bits (%u 32-bit words) per sample\n", probeWidthRounded, m_wordsPerRow);

	//Round word count up to next power of two
	m_wordsPerRowRounded = bit_ceil(m_wordsPerRow);
	uint32_t actualBits = m_wordsPerRowRounded * 32;
	float overhead = 100.0 * totalProbeWidth / actualBits;
	LogTrace("Final rounded word count per row: %u (%u bits, %.1f%% efficiency)\n",
		m_wordsPerRowRounded, actualBits, overhead);
}

ILASCPIServer::~ILASCPIServer()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command processing

bool ILASCPIServer::OnCommand(
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

		//TODO: stop without arming

		//Trigger position
		else if( (cmd == "POS") && args.size() == 1)
		{
			uint32_t idx = stoul(args[0]);
			if(idx > (m_depth / 4) )
				idx = (m_depth / 4) - 1;

			m_triggerIdx = idx;
			WriteRegister(m_baseAddress + 0x14, idx);
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

string ILASCPIServer::GetMake()
{
	return "Antikernel Labs";
}

string ILASCPIServer::GetModel()
{
	return "APB ILA";
}

string ILASCPIServer::GetSerial()
{
	return "None";
}

string ILASCPIServer::GetFirmwareVersion()
{
	return "1.0";
}

bool ILASCPIServer::OnQuery(
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
		else if(cmd == "PERIOD")
			SendReply(to_string(m_period));
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

		else if(cmd == "POS")
			SendReply(to_string(m_triggerIdx));

		else
			return false;
	}

	else if(subject.find("PROBE") == 0)
	{
		int idx = atoi(subject.c_str() + 5);
		if( (idx >= 8) || (idx < 0) )
			return false;

		if(cmd == "WIDTH")
			SendReply(to_string(m_widths[idx]));

		else if(cmd == "NAME")
			SendReply(m_names[idx]);

		else
			return false;
	}


	else if(cmd == "DATA")
	{
		//Figure out the offset in the circular buffer
		auto trigsample = ReadRegister(m_baseAddress + 0x4);
		uint32_t bufstart = (trigsample - m_triggerIdx) % m_depth;
		LogTrace("Trigger index = %d\n", trigsample);

		//Read the entire buffer
		vector<uint32_t> rxbuf;
		rxbuf.resize(m_wordsPerRowRounded * m_depth);
		ReadRegisterBulk(m_dataBaseAddress, rxbuf.size(), &rxbuf[0]);

		//Print it
		string ret;
		for(size_t i=0; i<m_depth; i++)
		{
			//Get the sample index of the row
			uint32_t rowbase = (bufstart + i) % m_depth;
			rowbase *= m_wordsPerRowRounded;

			for(ssize_t j=m_wordsPerRowRounded-1; j>=0; j--)
				ret += to_string_hex(rxbuf[rowbase + j], true, 8);
			ret += ",";
		}
		SendReply(ret);

		//Reset the trigger system for next round
		WriteRegister(m_baseAddress + 0, 0x0);
	}

	//Nope, invalid command
	else
		return false;

	//If we get here all good
	return true;
}
