/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include "CartRetailIR.h"
#include "../NDS.h"
#include "../Utils.h"

// CartRetailIR: NDS cartridge with IR transceiver (Pokémon games)
// the IR transceiver is connected to the SPI interface, with a passthrough command for SRAM access

namespace melonDS
{
using Platform::Log;
using Platform::LogLevel;

namespace NDSCart
{

CartRetailIR::CartRetailIR(const u8* rom, u32 len, u32 chipid, u32 irversion, bool badDSiDump, ROMListEntry romparams, std::unique_ptr<u8[]>&& sram, u32 sramlen, void* userdata) :
    CartRetailIR(CopyToUnique(rom, len), len, chipid, irversion, badDSiDump, romparams, std::move(sram), sramlen, userdata)
{
}

CartRetailIR::CartRetailIR(
    std::unique_ptr<u8[]>&& rom,
    u32 len,
    u32 chipid,
    u32 irversion,
    bool badDSiDump,
    ROMListEntry romparams,
    std::unique_ptr<u8[]>&& sram,
    u32 sramlen,
    void* userdata
) :
    CartRetail(std::move(rom), len, chipid, badDSiDump, romparams, std::move(sram), sramlen, userdata, CartType::RetailIR),
    IRVersion(irversion)
{
}

CartRetailIR::~CartRetailIR() = default;

void CartRetailIR::Reset()
{
    CartRetail::Reset();

    IRCmd = 0;
}

void CartRetailIR::DoSavestate(Savestate* file)
{
    CartRetail::DoSavestate(file);

    file->Var8(&IRCmd);
}

void CartRetailIR::SPISelect()
{
    CartRetail::SPISelect();
    IRPos = 0;
}

void CartRetailIR::SPIRelease()
{
    // If we were doing a Write to IR, send the buffered packet now
    if (IRCmd == 0x02 && IRPos > 1)
    {
        u8 sendLen = IRPos - 1;
        SendIR(sendLen);
    }

    CartRetail::SPIRelease();
}

u8 CartRetailIR::SPITransmitReceive(u8 val)
{
    if (IRPos == 0)
    {
        IRCmd = val;
        IRPos++;
        return 0;
    }

    u8 ret = 0;
    switch (IRCmd)
    {
    case 0x00: // pass-through
        ret = CartRetail::SPITransmitReceive(val);
        break;

    case 0x01: // Read from IR
        if (IRPos == 1)
        {
            ret = ReadIR(); // Initiates the Read. Returns the length of the packet.
        }
        else
        {
            ret = (u8)RxBuf[IRPos - 2]; // Return actual packet data to the game.
        }
        break;

    case 0x02: // Write to IR
        TxBuf[IRPos - 1] = (u8)val; // Load SPI data into Tx Buffer
        ret = 0x00;
        break;

    case 0x08: // ID
        ret = 0xAA;
        break;
    }

    IRPos++;
    return ret;
}


/*
   This is convoluted because 1: I haven't rewritten it to be nice and 2: We need to wait 3500us for no data. If we do NOT wait,
    walker emulators may work, but real hardware won't. Precise timings should be handled HERE to make Platform.h implementations
    as simple as possible.
  */

long long timeToAcceptPacket = 0;
u8 CartRetailIR::ReadIR()
{
    char tempBuf[0xB8];

    u8 pointer = 0;

    int len = Platform::IR_RecievePacket(tempBuf, sizeof(tempBuf), UserData);
    long long lastRxTime = Platform::GetUSCount();

    // This enters the receive loop. IF there are bytes to be received, keep trying to receive
    if (len > 0)
    {
        lastRxTime = Platform::GetUSCount();
        for (int i = 0; i < len; i++)
        {
            RxBuf[pointer + i] = tempBuf[i];
        }
        pointer = pointer + len;

        // keep trying to Rx until 3500us has passed
        while (true)
        {
            long long diff = Platform::GetUSCount() - lastRxTime;

            if (diff > 3500) break;

            len = Platform::IR_RecievePacket(tempBuf, sizeof(tempBuf), UserData);

            if (len <= 0) { continue; }
            else
            {
                lastRxTime = Platform::GetUSCount();
                for (int i = 0; i < len; i++)
                {
                    RxBuf[pointer + i] = tempBuf[i];
                }
                pointer = pointer + len;
            }
        }
    }

    u8 recvLen = pointer;
    if (recvLen == 0) return 0;

    Platform::IR_LogPacket((char*)&RxBuf, recvLen, false, UserData);
    return recvLen;
}

// Sends an entire packet to the frontend.
u8 CartRetailIR::SendIR(u8 len)
{
    Platform::IR_LogPacket((char*)&TxBuf, len, true, UserData);
    int sent;
    if ((u8)TxBuf[0] == 94) Platform::Sleep(10000); // Immediate disconnect. This packet needs to WAIT or else it will be piggybacked onto the latest packet (on the walker's end)
    sent = Platform::IR_SendPacket(TxBuf, len, UserData);
    if (sent < 0) perror("send error");
    return 0;
}


}

}
