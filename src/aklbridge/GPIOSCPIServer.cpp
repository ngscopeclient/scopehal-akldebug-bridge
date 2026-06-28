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
#include "GPIOSCPIServer.h"
#include <stdexcept>

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

GPIOSCPIServer::GPIOSCPIServer(ZSOCKET sock, uint32_t baseAddress)
	: SCPIServer(sock)
	, m_baseAddress(baseAddress)
{
}

GPIOSCPIServer::~GPIOSCPIServer()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command processing

bool GPIOSCPIServer::OnCommand(
	[[maybe_unused]] const string& line,
	const string& subject,
    const string& cmd,
    const vector<string>& args)
{
	if(subject == "GPIO")
	{
		//Can't write to the input value, so skip that

		//GPIO output value
		if((cmd == "OUTVAL") && (args.size() == 1) )
		{
			uint32_t value;
			sscanf(args[0].c_str(), "%x", &value);
			WriteRegister(m_baseAddress + 0x0, value);
		}

		//GPIO tristate value
		else if( (cmd == "TRIS") && (args.size() == 1) )
		{
			uint32_t value;
			sscanf(args[0].c_str(), "%x", &value);
			WriteRegister(m_baseAddress + 0x8, value);
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

string GPIOSCPIServer::GetMake()
{
	return "Antikernel Labs";
}

string GPIOSCPIServer::GetModel()
{
	return "APB GPIO";
}

string GPIOSCPIServer::GetSerial()
{
	return "None";
}

string GPIOSCPIServer::GetFirmwareVersion()
{
	return "1.0";
}

bool GPIOSCPIServer::OnQuery(
	[[maybe_unused]] const string& line,
	const string& subject,
	const string& cmd)
{
	//Read ID code
	if(cmd == "*IDN")
		SendReply(GetMake() + "," + GetModel() + "," + GetSerial() + "," + GetFirmwareVersion());

	else if(subject == "GPIO")
	{
		//Read the GPIO input value
		if(cmd == "INVAL")
		{
			char tmp[32];
			snprintf(tmp, sizeof(tmp), "%08x", ReadRegister(m_baseAddress + 0x4));
			SendReply(tmp);
		}

		//Read the GPIO output value
		else if(cmd == "OUTVAL")
		{
			char tmp[32];
			snprintf(tmp, sizeof(tmp), "%08x", ReadRegister(m_baseAddress + 0x0));
			SendReply(tmp);
		}

		//Read the GPIO tristate value
		else if(cmd == "TRIS")
		{
			char tmp[32];
			snprintf(tmp, sizeof(tmp), "%08x", ReadRegister(m_baseAddress + 0x8));
			SendReply(tmp);
		}

		//Invalid
		else
			return false;
	}

	//Nope, invalid command
	else
		return false;

	//If we get here all good
	return true;
}
