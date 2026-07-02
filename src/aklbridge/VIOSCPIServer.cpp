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
#include "VIOSCPIServer.h"
#include <stdexcept>
#include <string.h>
#include <algorithm>
#include <cinttypes>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

VIOSCPIServer::VIOSCPIServer(ZSOCKET sock, uint32_t baseAddress)
	: SCPIServer(sock)
	, m_baseAddress(baseAddress)
{
	LogTrace("Initializing VIO at 0x%08x\n", baseAddress);
	LogIndenter li;

	//Enumerate output ports
	LogTrace("Outputs\n");
	for(uint32_t iport = 0; iport < 8; iport ++)
	{
		LogIndenter li2;

		uint32_t nameBlock[8];
		for(uint32_t j=0; j < 8; j ++)
			nameBlock[j] = ReadRegister(baseAddress + 0x000 + 0x40*iport + 4*j);

		char name[33] = {0};
		memcpy(name, nameBlock, 32);

		//Extract width packed into the same register
		uint32_t width = name[31];
		name[31] = '\0';

		string sname(name);
		if(sname.empty())
			break;
		reverse(sname.begin(), sname.end());

		m_outputNames.push_back(sname);
		m_outputWidths.push_back(width);

		LogTrace("[%u] name=%-8s width=%d\n", iport, sname.c_str(), width);
	}

	//Enumerate input ports
	LogTrace("Inputs\n");
	for(uint32_t iport = 0; iport < 8; iport ++)
	{
		LogIndenter li2;

		uint32_t nameBlock[8];
		for(uint32_t j=0; j < 8; j ++)
			nameBlock[j] = ReadRegister(baseAddress + 0x200 + 0x40*iport + 4*j);

		char name[33] = {0};
		memcpy(name, nameBlock, 32);

		//Extract width packed into the same register
		uint32_t width = name[31];
		name[31] = '\0';

		string sname(name);
		if(sname.empty())
			break;
		reverse(sname.begin(), sname.end());

		m_inputNames.push_back(sname);
		m_inputWidths.push_back(width);

		LogTrace("[%u] name=%-8s width=%d\n", iport, sname.c_str(), width);
	}

	//Pad to full width with empty values
	while(m_inputNames.size() < 8)
		m_inputNames.push_back("");
	while(m_inputWidths.size() < 8)
		m_inputWidths.push_back(0);
	while(m_outputNames.size() < 8)
		m_outputNames.push_back("");
	while(m_outputWidths.size() < 8)
		m_outputWidths.push_back(0);
}

VIOSCPIServer::~VIOSCPIServer()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command processing

bool VIOSCPIServer::OnCommand(
	[[maybe_unused]] const string& line,
	const string& subject,
    const string& cmd,
    const vector<string>& args)
{
	if(subject.find("OUT") == 0)
	{
		int idx = atoi(subject.c_str() + 3);
		if( (idx >= 8) || (idx < 0) )
			return false;

		if( (cmd == "VALUE") && (args.size() == 1) )
		{
			uint64_t value;
			sscanf(args[0].c_str(), "%" SCNx64, &value);

			WriteRegister(m_baseAddress + 0x40*idx + 0x20, value & 0xffffffff);
			WriteRegister(m_baseAddress + 0x40*idx + 0x24, value >> 32);
		}
	}

	else
		return false;


	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Query processing

string VIOSCPIServer::GetMake()
{
	return "Antikernel Labs";
}

string VIOSCPIServer::GetModel()
{
	return "APB VIO";
}

string VIOSCPIServer::GetSerial()
{
	return "None";
}

string VIOSCPIServer::GetFirmwareVersion()
{
	return "1.0";
}

bool VIOSCPIServer::OnQuery(
	[[maybe_unused]] const string& line,
	const string& subject,
	const string& cmd)
{
	//Read ID code
	if(cmd == "*IDN")
		SendReply(GetMake() + "," + GetModel() + "," + GetSerial() + "," + GetFirmwareVersion());

	else if(subject.find("OUT") == 0)
	{
		int idx = atoi(subject.c_str() + 3);
		if( (idx >= 8) || (idx < 0) )
			return false;

		if(cmd == "VALUE")
		{
			uint64_t value = ReadRegister(m_baseAddress + 0x40*idx + 0x24);
			value <<= 32;
			value |= ReadRegister(m_baseAddress + 0x40*idx + 0x20);

			//Bitmask off dontcare values from address decoding of high bits
			auto width = m_outputWidths[idx];
			uint64_t mask = 0xffffffff'ffffffffLL;
			if(width < 64)
			{
				mask >>= width;
				mask <<= width;
				value &= ~mask;
			}

			SendReply(to_string_hex(value));
		}

		else if(cmd == "WIDTH")
			SendReply(to_string(m_outputWidths[idx]));

		else if(cmd == "NAME")
			SendReply(m_outputNames[idx]);

		else
			return false;
	}

	else if(subject.find("IN") == 0)
	{
		int idx = atoi(subject.c_str() + 2);
		if( (idx >= 8) || (idx < 0) )
			return false;

		if(cmd == "VALUE")
		{
			uint64_t value = ReadRegister(m_baseAddress + 0x200 + 0x40*idx + 0x24);
			value <<= 32;
			value |= ReadRegister(m_baseAddress + 0x200 + 0x40*idx + 0x20);

			//Bitmask off dontcare values from address decoding of high bits
			auto width = m_inputWidths[idx];
			uint64_t mask = 0xffffffff'ffffffffLL;
			if(width < 64)
			{
				mask >>= width;
				mask <<= width;
				value &= ~mask;
			}

			SendReply(to_string_hex(value));
		}

		else if(cmd == "WIDTH")
			SendReply(to_string(m_inputWidths[idx]));

		else if(cmd == "NAME")
			SendReply(m_inputNames[idx]);

		else
			return false;
	}

	//Nope, invalid command
	else
		return false;

	//If we get here all good
	return true;
}
