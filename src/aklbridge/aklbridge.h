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

#ifndef ps6000d_h
#define ps6000d_h

#include "../../lib/log/log.h"
#include "../../lib/xptools/Socket.h"
#include "../../lib/xptools/UART.h"
#include "../../lib/xptools/TimeUtil.h"

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#endif

#include <thread>
#include <map>
#include <mutex>

extern Socket g_scpiSocket;
extern Socket g_dataSocket;

void WaveformServerThread();

extern std::string g_model;
extern std::string g_serial;

extern UART* g_uart;
extern std::mutex g_mutex;

extern volatile bool g_waveformThreadQuit;

extern bool g_triggerArmed;
extern bool g_triggerOneShot;

enum opcode_t
{
	OP_RESET	= 0x80,
	OP_WRITE_32	= 0x81,
	OP_READ_32	= 0x82,

	OP_GET_BASE	= 0xfd,
	OP_IDCODE	= 0xfe,
	OP_NOP		= 0xff
};

uint32_t ReadRegister(uint32_t addr);
void WriteRegister(uint32_t addr, uint32_t value);
std::string to_string_hex(uint64_t n, bool zeropad = false, int len = 0);

#endif
