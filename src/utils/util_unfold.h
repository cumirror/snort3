//--------------------------------------------------------------------------
// Copyright (C) 2014-2015 Cisco and/or its affiliates. All rights reserved.
// Copyright (C) 1998-2013 Sourcefire, Inc.
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
// Writen by Bhagyashree Bantwal <bbantwal@sourcefire.com>

#ifndef UTIL_UNFOLD_H
#define UTIL_UNFOLD_H

#include "snort_types.h"

SO_PUBLIC int sf_unfold_header(const uint8_t*, uint32_t, uint8_t*, uint32_t, uint32_t*, int, int*);
SO_PUBLIC int sf_strip_CRLF(const uint8_t*, uint32_t, uint8_t*, uint32_t, uint32_t*);
SO_PUBLIC int sf_strip_LWS(const uint8_t*, uint32_t, uint8_t*, uint32_t, uint32_t*);

#endif

