/***********************************************************************************************************************
*                                                                                                                      *
* aklbridge                                                                                                            *
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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Program entry point
 */

#include "aklbridge.h"
//#include "AseqSCPIServer.h"
#include <signal.h>
#include <string.h>

using namespace std;

//vector<string> explode(const string& str, char separator);
//string Trim(const string& str);

void help();

void help()
{
	fprintf(stderr,
			"aklbridge [general options] [logger options] /dev/ttyX 115200\n"
			"\n"
			"  [general options]:\n"
			"    --help                        : this message...\n"
			"    --scpi-port port              : specifies the SCPI control plane port (default 5025)\n"
			"    --waveform-port port          : specifies the binary waveform data port (default 5026)\n"
			"\n"
			"  [logger options]:\n"
			"    levels: ERROR, WARNING, NOTICE, VERBOSE, DEBUG\n"
			"    --quiet|-q                    : reduce logging level by one step\n"
			"    --verbose                     : set logging level to VERBOSE\n"
			"    --debug                       : set logging level to DEBUG\n"
			"    --trace <classname>|          : name of class with tracing messages. (Only relevant when logging level is DEBUG.)\n"
			"            <classname::function>\n"
			"    --logfile|-l <filename>       : output log messages to file\n"
			"    --logfile-lines|-L <filename> : output log messages to file, with line buffering\n"
			"    --stdout-only                 : writes errors/warnings to stdout instead of stderr\n"
		   );
}

Socket g_scpiSocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
Socket g_dataSocket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);

#ifdef _WIN32
BOOL WINAPI OnQuit(DWORD signal);
#else
void OnQuit(int signal);
#endif

bool g_triggerArmed;

UART* g_uart = nullptr;

uint32_t ReadRegister(uint32_t addr);

int main(int argc, char* argv[])
{
	//Global settings
	Severity console_verbosity = Severity::NOTICE;

	//Parse command-line arguments
	uint16_t scpi_port = 5025;
	uint16_t waveform_port = 5026;
	string uartPath = "";
	int uartBaud = 115200;
	for(int i=1; i<argc; i++)
	{
		string s(argv[i]);

		//Let the logger eat its args first
		if(ParseLoggerArguments(i, argc, argv, console_verbosity))
			continue;

		if(s == "--help")
		{
			help();
			return 0;
		}

		else if(s == "--scpi-port")
		{
			if(i+1 < argc)
				scpi_port = atoi(argv[++i]);
		}

		else if(s == "--waveform-port")
		{
			if(i+1 < argc)
				waveform_port = atoi(argv[++i]);
		}

		else if(s[0] != '-')
		{
			if(uartPath.empty())
				uartPath = argv[i];
			else
				uartBaud = atoi(argv[i]);
		}

		else
		{
			fprintf(stderr, "Unrecognized command-line argument \"%s\", use --help\n", s.c_str());
			return 1;
		}
	}

	//Set up logging
	g_log_sinks.emplace(g_log_sinks.begin(), new ColoredSTDLogSink(console_verbosity));

	//Initialize the UART
	UART uart(uartPath, uartBaud);
	g_uart = &uart;

	LogNotice("Initializing...\n");
	{
		LogIndenter li;

		//Send 16 nops then a reset command
		LogNotice("Resetting debug bus...\n");
		uint8_t resetBuf[17];
		for(int i=0; i<16; i++)
			resetBuf[i] = OP_NOP;
		resetBuf[16] = OP_RESET;
		uart.Write(resetBuf, sizeof(resetBuf));

		//Flush any junk that might be in the rx buffer
		usleep(250 * 1000);
		uart.FlushRxBuffer();

		//Read the ID code
		uint8_t idcmd = OP_IDCODE;
		uart.Write(&idcmd, sizeof(idcmd));
		char idcode[5] = {0};
		uart.Read((uint8_t*)idcode,   4);
		LogNotice("Debug bridge ID:   %s\n", idcode);
		if(strcmp(idcode, "APB_") != 0)
		{
			LogError("Invalid IDCODE, bailing out\n");
			return 1;
		}

		//Read the ROM table base address
		uint8_t romcmd = OP_GET_BASE;
		uart.Write(&romcmd, sizeof(romcmd));
		uint32_t rombase;
		uart.Read((uint8_t*)&rombase, sizeof(rombase));
		LogNotice("Debug ROM address: 0x%08x\n", rombase);

		//Walk the ROM
		const uint32_t rom_max = 4;	//TODO increase this
		for(uint32_t i=0; i<rom_max; i++)
		{
			auto type = ReadRegister(rombase + i*8);
			auto typeswap = __builtin_bswap32(type);	//reverse endianness since string is big endian on the device
			memcpy(idcode, &typeswap, sizeof(typeswap));
			auto base = ReadRegister(rombase + i*8 + 4);

			//all-zero entry indicates the end of the ROM table
			if(type == 0)
				break;

			LogDebug("[%u] type = %08x (%4s) at 0x%08x\n", i, type, idcode, base);
		}
	}

	/*
	//Set up signal handlers
#ifdef _WIN32
	SetConsoleCtrlHandler(OnQuit, TRUE);
#else
	signal(SIGINT, OnQuit);
	signal(SIGPIPE, SIG_IGN);
#endif

	//Configure the data plane socket
	g_dataSocket.Bind(waveform_port);
	g_dataSocket.Listen();

	//Launch the control plane socket server
	g_scpiSocket.Bind(scpi_port);
	g_scpiSocket.Listen();

	LogDebug("Ready\n");

	while(true)
	{
		Socket scpiClient = g_scpiSocket.Accept();
		if(!scpiClient.IsValid())
			break;

		//Create a server object for this connection
		AseqSCPIServer server(scpiClient.Detach());

		//Launch the data-plane thread
		thread dataThread(WaveformServerThread);

		//Process connections on the socket
		server.MainLoop();

		g_waveformThreadQuit = true;
		dataThread.join();
		g_waveformThreadQuit = false;
	}
	*/

	OnQuit(SIGQUIT);
	return 0;
}

uint32_t ReadRegister(uint32_t addr)
{
	uint8_t txbuf[5];
	txbuf[0] = OP_READ_32;
	memcpy(&txbuf[1], &addr, sizeof(addr));
	g_uart->Write(txbuf, sizeof(txbuf));

	uint32_t regval = 0;
	g_uart->Read((uint8_t*)&regval, sizeof(regval));
	return regval;
}

#ifdef _WIN32
BOOL WINAPI OnQuit(DWORD signal)
{
	(void)signal;
#else
void OnQuit(int /*signal*/)
{
#endif
	LogNotice("Shutting down...\n");

	exit(0);
}

/**
	@brief Splits a string up into an array separated by delimiters
 */
 /*
vector<string> explode(const string& str, char separator)
{
	vector<string> ret;
	string tmp;
	for(auto c : str)
	{
		if(c == separator)
		{
			if(!tmp.empty())
				ret.push_back(tmp);
			tmp = "";
		}
		else
			tmp += c;
	}
	if(!tmp.empty())
		ret.push_back(tmp);
	return ret;
}
*/

/**
	@brief Removes whitespace from the start and end of a string
 */
 /*
string Trim(const string& str)
{
	string ret;
	string tmp;

	//Skip leading spaces
	size_t i=0;
	for(; i<str.length() && isspace(str[i]); i++)
	{}

	//Read non-space stuff
	for(; i<str.length(); i++)
	{
		//Non-space
		char c = str[i];
		if(!isspace(c))
		{
			ret = ret + tmp + c;
			tmp = "";
		}

		//Space. Save it, only append if we have non-space after
		else
			tmp += c;
	}

	return ret;
}
*/
