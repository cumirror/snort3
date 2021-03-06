//--------------------------------------------------------------------------
// Copyright (C) 2014-2015 Cisco and/or its affiliates. All rights reserved.
// Copyright (C) 2012-2013 Sourcefire, Inc.
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
/*
**  Author(s):  Hui Cao <hcao@sourcefire.com>
**
**  NOTES
**  5.25.2012 - Initial Source Code. Hui Cao
*/
#ifndef FILE_CONFIG_H
#define FILE_CONFIG_H
#include "file_lib.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "file_identifier.h"

#define DEFAULT_FILE_TYPE_DEPTH 1460
#define DEFAULT_FILE_SIGNATURE_DEPTH 10485760 /*10 Mbytes*/
#define DEFAULT_FILE_SHOW_DATA_DEPTH 100
#define DEFAULT_FILE_BLOCK_TIMEOUT 86400 /*1 day*/
#define DEFAULT_FILE_LOOKUP_TIMEOUT 2    /*2 seconds*/

class FileConfig
{
public:
    void print_file_rule(FileMagicRule&);
    FileMagicRule* get_rule_from_id(uint32_t);
    void process_file_rule(FileMagicRule&);
    bool process_file_magic(FileMagicData&);
    uint32_t find_file_type_id(uint8_t* buf, int len, FileContext* context);
    int64_t file_type_depth = DEFAULT_FILE_TYPE_DEPTH;
    int64_t file_signature_depth = DEFAULT_FILE_SIGNATURE_DEPTH;
    int64_t file_block_timeout = DEFAULT_FILE_BLOCK_TIMEOUT;
    int64_t file_lookup_timeout = DEFAULT_FILE_LOOKUP_TIMEOUT;
    bool block_timeout_lookup = false;

#if defined(DEBUG_MSGS) || defined (REG_TEST)
    int64_t show_data_depth = DEFAULT_FILE_SHOW_DATA_DEPTH;
#endif

private:
    FileIdenfifier fileIdentifier;
};

#endif

