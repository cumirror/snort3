//--------------------------------------------------------------------------
// Copyright (C) 2014-2015 Cisco and/or its affiliates. All rights reserved.
// Copyright (C) 2005-2013 Sourcefire, Inc.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License Version 2 as published
// by the Free Software Foundation.  You may not use, modify or distribute
// this program under any other version of the GNU General Public License.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//--------------------------------------------------------------------------

// @file    active.c
// @author  Russ Combs <rcombs@sourcefire.com>

#include "active.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "utils/dnet_header.h"
#include "stream/stream_api.h"
#include "snort_config.h"

#include "framework/codec.h"
#include "managers/action_manager.h"
#include "protocols/packet_manager.h"
#include "packet_io/sfdaq.h"
#include "protocols/tcp.h"
#include "protocols/protocol_ids.h"
#include "parser/parser.h"

#define MAX_ATTEMPTS 20

// these can't be pkt flags because we do the handling
// of these flags following all processing and the drop
// or response may have been produced by a pseudopacket.
THREAD_LOCAL tActiveDrop active_drop_pkt = ACTIVE_ALLOW;
THREAD_LOCAL tActiveSsnDrop active_drop_ssn = ACTIVE_SSN_ALLOW;
// TBD consider performance of replacing active_drop_pkt/ssn
// with a active_verdict.  change over if it is a wash or better.

THREAD_LOCAL int active_tunnel_bypass = 0;
THREAD_LOCAL int active_suspend = 0;
THREAD_LOCAL int active_have_rsp = 0;

typedef int (* send_t) (
    const DAQ_PktHdr_t* h, int rev, const uint8_t* buf, uint32_t len);

static THREAD_LOCAL eth_t* s_link = NULL;
static THREAD_LOCAL ip_t* s_ipnet = NULL;
static THREAD_LOCAL send_t s_send = DAQ_Inject;

static THREAD_LOCAL uint8_t s_attempts = 0;
static THREAD_LOCAL uint64_t s_injects = 0;

static int s_enabled = 0;

static int Active_Open(const char*);
static int Active_Close(void);

static int Active_SendEth(const DAQ_PktHdr_t*, int, const uint8_t*, uint32_t);
static int Active_SendIp(const DAQ_PktHdr_t*, int, const uint8_t*, uint32_t);

//--------------------------------------------------------------------

void Active_KillSession(Packet* p, EncodeFlags* pf)
{
    EncodeFlags flags = pf ? *pf : ENC_FLAG_FWD;

    switch ( p->type() )
    {
    case PktType::UNKNOWN:
        // Can only occur if we have never seen IP
        return;

    case PktType::TCP:
        Active_SendReset(p, 0);
        if ( flags & ENC_FLAG_FWD )
            Active_SendReset(p, ENC_FLAG_FWD);
        break;

    default:
        if ( Active_PacketForceDropped() )
            Active_SendUnreach(p, UnreachResponse::FWD);
        else
            Active_SendUnreach(p, UnreachResponse::PORT);
        break;
    }
}

//--------------------------------------------------------------------

int Active_Init(SnortConfig* sc)
{
    s_attempts = sc->respond_attempts;
    if ( s_attempts > MAX_ATTEMPTS )
        s_attempts = MAX_ATTEMPTS;
    if ( s_enabled && !s_attempts )
        s_attempts = 1;

    if ( s_enabled && (!DAQ_CanInject() || !sc->respond_device.empty()) )
    {
        if ( SnortConfig::read_mode() || Active_Open(sc->respond_device.c_str()) )
        {
            ParseWarning(WARN_DAQ, "active responses disabled since DAQ "
                "can't inject packets.");
#ifndef REG_TEST
            s_attempts = 0;
#endif
        }

        if (NULL != sc->eth_dst)
            PacketManager::encode_set_dst_mac(sc->eth_dst);
    }
    return 0;
}

int Active_Term(void)
{
    Active_Close();
    return 0;
}

int Active_IsEnabled(void) { return s_enabled && s_attempts; }

void Active_SetEnabled(int on_off)
{
    if ( !on_off || on_off > s_enabled )
        s_enabled = on_off;
}

static inline EncodeFlags GetFlags(void)
{
    EncodeFlags flags = ENC_FLAG_ID;
    if ( DAQ_RawInjection() || s_ipnet )
        flags |= ENC_FLAG_RAW;
    return flags;
}

//--------------------------------------------------------------------

static uint32_t Strafe(int, uint32_t, const Packet*);

void Active_SendReset(Packet* p, EncodeFlags ef)
{
    int i;
    EncodeFlags flags = (GetFlags() | ef) & ~ENC_FLAG_VAL;
    EncodeFlags value = ef & ENC_FLAG_VAL;

    for ( i = 0; i < s_attempts; i++ )
    {
        uint32_t len;
        const uint8_t* rej;

        value = Strafe(i, value, p);

        rej = PacketManager::encode_response(TcpResponse::RST, flags|value, p, len);
        if ( !rej )
            return;

        s_send(p->pkth, !(ef & ENC_FLAG_FWD), rej, len);
    }
}

void Active_SendUnreach(Packet* p, UnreachResponse type)
{
    uint32_t len;
    const uint8_t* rej;
    EncodeFlags flags = GetFlags();

    if ( !s_attempts )
        return;

    rej = PacketManager::encode_reject(type, flags, p, len);
    if ( !rej )
        return;

    s_send(p->pkth, 1, rej, len);
}

bool Active_SendData(
    Packet* p, EncodeFlags flags, const uint8_t* buf, uint32_t blen)
{
    uint16_t toSend;
    const uint8_t* seg;

    flags |= GetFlags();
    flags &= ~ENC_FLAG_VAL;

    if ( flags & ENC_FLAG_RST_SRVR )
    {
        uint32_t plen = 0;
        EncodeFlags tmp_flags = flags ^ ENC_FLAG_FWD;
        seg = PacketManager::encode_response(TcpResponse::RST, tmp_flags, p, plen);

        if ( seg )
            s_send(p->pkth, !(tmp_flags & ENC_FLAG_FWD), seg, plen);
    }
    flags |= ENC_FLAG_SEQ;

    uint32_t sent = 0;
    const uint16_t maxPayload = PacketManager::encode_get_max_payload(p);

    if (!maxPayload)
        return false;

    do
    {
        uint32_t plen = 0;
        toSend = blen > maxPayload ? maxPayload : blen;
        flags = (flags & ~ENC_FLAG_VAL) | sent;
        seg = PacketManager::encode_response(TcpResponse::PUSH, flags, p, plen, buf, toSend);

        if ( !seg )
            return false;

        s_send(p->pkth, !(flags & ENC_FLAG_FWD), seg, plen);

        buf += toSend;
        sent += toSend;
    }
    while (blen -= toSend);

    if (flags & ENC_FLAG_RST_CLNT)
    {
        uint32_t plen = 0;
        flags = (flags & ~ENC_FLAG_VAL) | sent;
        seg = PacketManager::encode_response(TcpResponse::RST, flags, p, plen);

        if ( seg )
            s_send(p->pkth, !(flags & ENC_FLAG_FWD), seg, plen);
    }

    return true;
}

void Active_InjectData(
    Packet* p, EncodeFlags flags, const uint8_t* buf, uint32_t blen)
{
    uint32_t plen;
    const uint8_t* seg;

    if ( !s_attempts )
        return;

    flags |= GetFlags();
    flags &= ~ENC_FLAG_VAL;

    seg = PacketManager::encode_response(TcpResponse::PUSH, flags, p, plen, buf, blen);
    if ( !seg )
        return;

    s_send(p->pkth, !(flags & ENC_FLAG_FWD), seg, plen);
}

//--------------------------------------------------------------------

int Active_IsRSTCandidate(const Packet* p)
{
    if ( !p->is_tcp() )
        return 0;

    if ( !p->ptrs.tcph )
        return 0;

    /*
    **  This ensures that we don't reset packets that we just
    **  spoofed ourselves, thus inflicting a self-induced DOS
    **  attack.
    */
    return ( !(p->ptrs.tcph->th_flags & TH_RST) );
}

int Active_IsUNRCandidate(const Packet* p)
{
    // FIXIT allow unr to tcp/udp/icmp4/icmp6 only or for all
    switch ( p->type() )
    {
    case PktType::TCP:
    case PktType::UDP:
        return 1;

    case PktType::ICMP:
        // FIXIT-L return false for icmp unreachables
        return 1;

    default:
        return 0;
    }

    return 0;
}

//--------------------------------------------------------------------
// TBD strafed sequence numbers could be divided by window
// scaling if present.

static uint32_t Strafe(int i, uint32_t flags, const Packet* p)
{
    flags &= ENC_FLAG_VAL;

    switch ( i )
    {
    case 0:
        flags |= ENC_FLAG_SEQ;
        break;

    case 1:
        flags = p->dsize;
        flags &= ENC_FLAG_VAL;
        flags |= ENC_FLAG_SEQ;
        break;

    case 2:
    case 3:
        flags += (p->dsize << 1);
        flags &= ENC_FLAG_VAL;
        flags |= ENC_FLAG_SEQ;
        break;

    case 4:
        flags += (p->dsize << 2);
        flags &= ENC_FLAG_VAL;
        flags |= ENC_FLAG_SEQ;
        break;

    default:
        flags += (ntohs(p->ptrs.tcph->th_win) >> 1);
        flags &= ENC_FLAG_VAL;
        flags |= ENC_FLAG_SEQ;
        break;
    }
    return flags;
}

//--------------------------------------------------------------------
// support for decoder and rule actions

static inline void _Active_ForceIgnoreSession(Packet* p)
{
    stream.drop_packet(p);
}

static inline void _Active_DoIgnoreSession(Packet* p)
{
    if ( SnortConfig::inline_mode() || SnortConfig::treat_drop_as_ignore() )
    {
        _Active_ForceIgnoreSession(p);
    }
}

int Active_IgnoreSession(Packet* p)
{
    Active_DropPacket(p);
    _Active_DoIgnoreSession(p);
    return 0;
}

int Active_ForceDropAction(Packet* p)
{
    if ( p->has_ip() )
        return 0;

    // explicitly drop packet
    Active_ForceDropPacket();

    switch ( p->type() )
    {
    case PktType::TCP:
    case PktType::UDP:
        Active_DropSession(p);
        _Active_ForceIgnoreSession(p);
    default:
        break;
    }
    return 0;
}

static inline int _Active_DoReset(Packet* p)
{
    if ( !Active_IsEnabled() )
        return 0;

    if ( !p->ptrs.ip_api.is_valid() )
        return 0;

    switch ( p->type() )
    {
    case PktType::TCP:
        if ( Active_IsRSTCandidate(p) )
            ActionManager::queue_reject();
        break;

    case PktType::UDP:
    case PktType::ICMP:
    case PktType::IP:
        if ( Active_IsUNRCandidate(p) )
            ActionManager::queue_reject();
        break;

    default:
        break;
    }

    return 0;
}

int Active_DropAction(Packet* p)
{
    Active_IgnoreSession(p);

    if ( !s_attempts || s_enabled < 2 || active_drop_ssn == ACTIVE_SSN_DROP_WITHOUT_RESET )
        return 0;

    return _Active_DoReset(p);
}

int Active_ForceDropResetAction(Packet* p)
{
    Active_ForceDropAction(p);

    return _Active_DoReset(p);
}

//--------------------------------------------------------------------
// support for non-DAQ injection

static int Active_Open(const char* dev)
{
    if ( dev && strcasecmp(dev, "ip") )
    {
        s_link = eth_open(dev);

        if ( !s_link )
            FatalError("%s: can't open %s\n",
                "Active response", dev);
        s_send = Active_SendEth;
    }
    else
    {
        s_ipnet = ip_open();

        if ( !s_ipnet )
            FatalError("%s: can't open ip\n",
                "Active response");
        s_send = Active_SendIp;
    }
    return ( s_link || s_ipnet ) ? 0 : -1;
}

static int Active_Close(void)
{
    if ( s_link )
        eth_close(s_link);

    if ( s_ipnet )
        ip_close(s_ipnet);

    s_link = NULL;
    s_ipnet = NULL;

    return 0;
}

static int Active_SendEth(
    const DAQ_PktHdr_t*, int, const uint8_t* buf, uint32_t len)
{
    ssize_t sent = eth_send(s_link, buf, len);
    s_injects++;
    return ( (uint32_t)sent != len );
}

static int Active_SendIp(
    const DAQ_PktHdr_t*, int, const uint8_t* buf, uint32_t len)
{
    ssize_t sent = ip_send(s_ipnet, buf, len);
    s_injects++;
    return ( (uint32_t)sent != len );
}

uint64_t Active_GetInjects(void) { return s_injects; }

