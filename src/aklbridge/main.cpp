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
#include "GPIOSCPIServer.h"
#include "ILA8b10bSCPIServer.h"
#include "ILASCPIServer.h"
#include "VIOSCPIServer.h"
#include <signal.h>
#include <string.h>

using namespace std;

void help();

void help()
{
	fprintf(stderr,
			"aklbridge [general options] [logger options] /dev/ttyX 115200\n"
			"\n"
			"  [general options]:\n"
			"    --help                        : this message...\n"
			"    --scpi-port port              : specifies the base SCPI control plane port (default 5025)\n"
		//	"    --waveform-port port          : specifies the binary waveform data port (default 5026)\n"
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

#ifdef _WIN32
BOOL WINAPI OnQuit(DWORD signal);
#else
void OnQuit(int signal);
#endif

bool g_triggerArmed;

recursive_mutex g_mutex;
UART* g_uart = nullptr;

vector<shared_ptr<Socket> > g_listenerSockets;
vector<unique_ptr<thread> > g_listenerThreads;

void GPIOListenerThread(uint32_t baseAddress, uint16_t port, shared_ptr<Socket> listener);
void ILA8b10bListenerThread(uint32_t baseAddress, uint16_t port, shared_ptr<Socket> listener);
void ILAListenerThread(uint32_t baseAddress, uint16_t port, shared_ptr<Socket> listener);
void VIOListenerThread(uint32_t baseAddress, uint16_t port, shared_ptr<Socket> listener);

int main(int argc, char* argv[])
{
	//Global settings
	Severity console_verbosity = Severity::NOTICE;

	//Parse command-line arguments
	uint16_t scpi_port = 5025;
	//uint16_t waveform_port = 5026;
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
	/*
		else if(s == "--waveform-port")
		{
			if(i+1 < argc)
				waveform_port = atoi(argv[++i]);
		}*/

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

	uint16_t nextPort = scpi_port;
	LogNotice("Initializing...\n");
	{
		LogIndenter li;

		//Send 16 nops then a reset command
		//TODO: this should be an argument we don't necessarily WANT to reset on connection
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

		//Get the size of the ROM
		uint32_t romsize = ReadRegister(rombase);
		LogNotice("ROM table has %u entries\n", romsize);

		//Read the ROM
		vector<uint32_t> rom;
		rom.resize(romsize*2);
		ReadRegisterBulk(rombase + 0x8, rom.size(), &rom[0]);

		//Walk the ROM
		for(uint32_t i=0; i<romsize; i++)
		{
			auto type = rom[i*2];
			memcpy(idcode, &type, sizeof(type));
			auto base = rom[i*2 + 1];
			string stype = idcode;

			//all-zero entry indicates the end of the ROM table
			if(type == 0)
				break;

			LogDebug("[%u] type = %08x (%4s) at 0x%08x\n", i, type, idcode, base);
			LogIndenter li2;

			if(stype == "GPIO")
			{
				//Create the server socket
				auto listener = make_shared<Socket>(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
				g_listenerSockets.push_back(listener);

				//Run the server
				g_listenerThreads.push_back(make_unique<thread>(GPIOListenerThread, base, nextPort, listener));
				nextPort ++;
			}
			else if(stype == "VIO_")
			{
				//Create the server socket
				auto listener = make_shared<Socket>(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
				g_listenerSockets.push_back(listener);

				//Run the server
				g_listenerThreads.push_back(make_unique<thread>(VIOListenerThread, base, nextPort, listener));
				nextPort ++;
			}
			else if(stype == "8B10")
			{
				//Create the server socket
				auto listener = make_shared<Socket>(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
				g_listenerSockets.push_back(listener);

				//Run the server
				g_listenerThreads.push_back(make_unique<thread>(ILA8b10bListenerThread, base, nextPort, listener));
				nextPort ++;
			}
			else if(stype == "ILA_")
			{
				//Create the server socket
				auto listener = make_shared<Socket>(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
				g_listenerSockets.push_back(listener);

				//Run the server
				g_listenerThreads.push_back(make_unique<thread>(ILAListenerThread, base, nextPort, listener));
				nextPort ++;
			}
			else
				LogDebug("Unrecognized debug IP type, ignoring\n");

			//Debug: delay to serialize log messages
			usleep(100 * 1000);
		}
	}

	//Set up signal handlers
#ifdef _WIN32
	SetConsoleCtrlHandler(OnQuit, TRUE);
#else
	signal(SIGINT, OnQuit);
	signal(SIGPIPE, SIG_IGN);
#endif

	LogDebug("Ready\n");

	//Wait for all threads to terminate and clean up gracefully
	for(auto& thread : g_listenerThreads)
		thread->join();

	OnQuit(SIGQUIT);
	return 0;
}

void GPIOListenerThread(uint32_t baseAddress, uint16_t port, shared_ptr<Socket> listener)
{
	LogDebug("GPIOListenerThread running on port %d\n", (int)port);

	listener->Bind(port);
	listener->Listen();

	while(true)
	{
		Socket client = listener->Accept();
		if(!client.IsValid())
			break;

		GPIOSCPIServer server(client.Detach(), baseAddress);
		server.MainLoop();
	}
}

void ILA8b10bListenerThread(uint32_t baseAddress, uint16_t port, shared_ptr<Socket> listener)
{
	LogDebug("ILA8b10bListenerThread running on port %d\n", (int)port);

	listener->Bind(port);
	listener->Listen();

	while(true)
	{
		Socket client = listener->Accept();
		if(!client.IsValid())
			break;

		ILA8b10bSCPIServer server(client.Detach(), baseAddress);
		server.MainLoop();
	}
}

void ILAListenerThread(uint32_t baseAddress, uint16_t port, shared_ptr<Socket> listener)
{
	LogDebug("ILAListenerThread running on port %d\n", (int)port);

	listener->Bind(port);
	listener->Listen();

	while(true)
	{
		Socket client = listener->Accept();
		if(!client.IsValid())
			break;

		ILASCPIServer server(client.Detach(), baseAddress);
		server.MainLoop();
	}
}

void VIOListenerThread(uint32_t baseAddress, uint16_t port, shared_ptr<Socket> listener)
{
	LogDebug("VIOListenerThread running on port %d\n", (int)port);

	listener->Bind(port);
	listener->Listen();

	while(true)
	{
		Socket client = listener->Accept();
		if(!client.IsValid())
			break;

		VIOSCPIServer server(client.Detach(), baseAddress);
		server.MainLoop();
	}
}

uint32_t ReadRegister(uint32_t addr)
{
	lock_guard<recursive_mutex> lock(g_mutex);

	uint8_t txbuf[5];
	txbuf[0] = OP_READ_32;
	memcpy(&txbuf[1], &addr, sizeof(addr));
	g_uart->Write(txbuf, sizeof(txbuf));

	uint32_t regval = 0;
	g_uart->Read((uint8_t*)&regval, sizeof(regval));
	return regval;
}

void ReadRegisterBulk(uint32_t addr, uint32_t size, uint32_t* outbuf)
{
	lock_guard<recursive_mutex> lock(g_mutex);

	if(size > 65535)
		LogFatal("sizes > 64k not implemented\n");

	//Send the read command
	uint8_t txbuf[7];
	txbuf[0] = OP_READ_32_BULK;
	memcpy(&txbuf[1], &addr, sizeof(addr));
	txbuf[5] = size & 0xff;
	txbuf[6] = (size >> 8) & 0xff;
	g_uart->Write(txbuf, sizeof(txbuf));

	//Read the data back
	if(!g_uart->Read((uint8_t*)outbuf, size * sizeof(uint32_t)))
		LogError("Read failed\n");
}

void WriteRegister(uint32_t addr, uint32_t value)
{
	lock_guard<recursive_mutex> lock(g_mutex);

	uint8_t txbuf[9];
	txbuf[0] = OP_WRITE_32;
	memcpy(&txbuf[1], &addr, sizeof(addr));
	memcpy(&txbuf[5], &value, sizeof(value));
	g_uart->Write(txbuf, sizeof(txbuf));
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

	for(auto& sock : g_listenerSockets)
		sock->Close();

	exit(0);
}

/**
	@brief Like std::to_string, but output in hex
 */
string to_string_hex(uint64_t n, bool zeropad, int len)
{
	char format[32];
	if(zeropad)
		snprintf(format, sizeof(format), "%%0%dlx", len);
	else if(len > 0)
		snprintf(format, sizeof(format), "%%%dlx", len);
	else
		snprintf(format, sizeof(format), "%%lx");

	char tmp[32];
	snprintf(tmp, sizeof(tmp), format, n);
	return tmp;
}
