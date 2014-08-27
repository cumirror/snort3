/****************************************************************************
 *
** Copyright (C) 2014 Cisco and/or its affiliates. All rights reserved.
 * Copyright (C) 2003-2013 Sourcefire, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2 as
 * published by the Free Software Foundation.  You may not use, modify or
 * distribute this program under any other version of the GNU General
 * Public License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 ****************************************************************************/

// zhash is based on sfxhash - see sfxhash.cc for details


#include <assert.h>
#include <stdlib.h>

#include "zhash.h"
#include "snort_types.h"
#include "snort_debug.h"
#include "util.h"
#include "hash/sfhashfcn.h"

//-------------------------------------------------------------------------
// private stuff
//-------------------------------------------------------------------------

struct ZHashNode
{
  ZHashNode* gnext, * gprev; // global list
  ZHashNode* next,  * prev;  // row list

  int rindex;

  void* key;
  void* data;
};

void ZHash::delete_free_list()
{
    if ( !fhead )
        return;

    ZHashNode* cur = fhead;

    while ( cur )
    {
        fhead = cur->gnext;
        free(cur);
        cur = fhead;
    }
}

void ZHash::save_free_node(ZHashNode* node)
{
    if ( fhead )
    {
        node->gprev = nullptr;
        node->gnext = fhead;
        fhead->gprev = node;
        fhead = node;
    }
    else
    {
        node->gprev = nullptr;
        node->gnext = nullptr;
        fhead = node;
    }
}

ZHashNode* ZHash::get_free_node()
{
    ZHashNode* node = fhead;

    if ( fhead )
    {
        fhead = fhead->gnext;

        if ( fhead )
            fhead->gprev = nullptr;
    }

    return node;
}

void ZHash::glink_node(ZHashNode* node)
{
    if ( ghead )
    {
        node->gprev = nullptr;
        node->gnext = ghead;
        ghead->gprev = node;
        ghead = node;
    }
    else
    {
        node->gprev = nullptr;
        node->gnext = nullptr;
        ghead = node;
        gtail = node;
    }
}

void ZHash::gunlink_node(ZHashNode* node)
{
    if ( cursor == node )
        cursor = node->gprev;

    if ( ghead == node )
    {
        ghead = ghead->gnext;
        if ( ghead )
            ghead->gprev = nullptr;
    }

    if ( node->gprev )
        node->gprev->gnext = node->gnext;

    if ( node->gnext )
        node->gnext->gprev = node->gprev;

    if ( gtail == node )
        gtail = node->gprev;
}

void ZHash::link_node(ZHashNode* node)
{
    if ( table[node->rindex] )
    {
        node->prev = nullptr;
        node->next=table[node->rindex];
        table[node->rindex]->prev = node;
        table[node->rindex] = node;
    }
    else
    {
        node->prev=nullptr;
        node->next=nullptr;
        table[node->rindex] = node;
    }
}

void ZHash::unlink_node(ZHashNode* node)
{
    if ( node->prev )
    {
        node->prev->next = node->next;
        if ( node->next )
            node->next->prev = node->prev;
    }
    else if ( table[node->rindex] )
    {
        table[node->rindex] = table[node->rindex]->next;

        if ( table[node->rindex] )
            table[node->rindex]->prev = nullptr;
    }
}

void ZHash::move_to_front(ZHashNode* node)
{
    // move to front of row list
    if ( table[node->rindex] != node )
    {
        unlink_node(node);
        link_node(node);
    }

    // move to front of global list
    if ( node != ghead )
    {
        gunlink_node(node);
        glink_node(node);
    }
}

ZHashNode* ZHash::find_node_row(const void* key, int* rindex)
{
    unsigned hashkey = sfhashfcn->hash_fcn(
        sfhashfcn, (unsigned char*)key, keysize);

    // Modulus is slow; use a table size that is a power of 2.
    int index = hashkey & (nrows - 1);

    *rindex = index;

    for ( ZHashNode* node=table[index]; node; node=node->next )
    {
        if ( !sfhashfcn->keycmp_fcn(node->key,key,keysize) )
        {
            move_to_front(node);
            find_success++;
            return node;
        }
    }

    find_fail++;
    return nullptr;
}

int ZHash::nearest_powerof2(int rows)
{
    rows -= 1;

    for ( unsigned i=1; i<sizeof(rows) * 8; i <<= 1 )
        rows = rows | (rows >> i);

    rows += 1;
    return rows;
}

//-------------------------------------------------------------------------
// public stuff
//-------------------------------------------------------------------------

ZHash::ZHash(int rows, int keysz)
{
    if ( rows > 0 )
    {
        // make sure we have a prime number 
        // rows = sf_nearest_prime(rows);

        /* If rows is not a power of two, need to find the
         * next highest power of two */
        rows = nearest_powerof2(rows);
    }
    else   /* use the magnitude of rows as is */
    {
        rows = -rows;
    }

    /* this has a default hashing function */
    sfhashfcn = sfhashfcn_new(rows);

    if ( !sfhashfcn )
        return;  // FIXIT-H can't just return

    /* Allocate the array of node ptrs */
    table = new ZHashNode*[rows];

    for ( int i = 0; i < rows; ++i )
        table[i] = nullptr;

    keysize = keysz;
    nrows = rows;

    fhead = cursor = nullptr;
    ghead = gtail = nullptr;
    count = find_success = find_fail = 0;
}

ZHash::~ZHash()
{
    if ( sfhashfcn )
        sfhashfcn_free(sfhashfcn);

    if ( table )
    {
        for ( unsigned i=0; i < nrows; ++i )
        {
            for ( ZHashNode* node=table[i]; node; )
            {
                ZHashNode* onode = node;
                node = node->next;
                free(onode);
            }
        }
        delete[] table;
    }
    delete_free_list();
}

void* ZHash::push(void* p)
{
    ZHashNode* node =
        (ZHashNode*)SnortAlloc(sizeof(ZHashNode) + keysize);

    node->key = (char*)node + sizeof(ZHashNode);
    node->data = p;

    save_free_node(node);
    return node->key;
}

void* ZHash::pop()
{
    ZHashNode* node = get_free_node();

    if ( !node )
        return nullptr;

    void* pv = node->data;
    free(node);

    return pv;
}

void* ZHash::get(const void* key)
{
    int index;
    ZHashNode* node = find_node_row(key, &index);

    if ( node )
        return node->data;

    node = get_free_node();

    if ( !node )
        return nullptr;

    memcpy(node->key,key,keysize);

    node->rindex = index;
    link_node (node);
    glink_node(node);

    count++;

    return node->data;
}

void* ZHash::find(const void* key)
{
    int rindex;
    ZHashNode* node = find_node_row(key, &rindex);

    if ( node )
        return node->data;

    return nullptr;
}


void* ZHash::first()
{
    cursor = gtail;
    return cursor ? cursor->data : nullptr;
}

void* ZHash::next()
{
    if ( !cursor )
        return nullptr;

    cursor = cursor->gprev;
    return cursor ? cursor->data : nullptr;
}

void* ZHash::current()
{
    return cursor ? cursor->data : nullptr;
}

bool ZHash::touch()
{
    ZHashNode* node = cursor;

    if ( !node )
        return false;

    cursor = cursor->gprev;

    if ( node != ghead )
    {
        gunlink_node(node);
        glink_node(node);
        return true;
    }
    return false;
}

bool ZHash::remove(ZHashNode* node)
{
    if ( !node )
        return false;

    unlink_node(node);
    gunlink_node(node);

    count--;
    save_free_node(node);

    return true;
}

bool ZHash::remove()
{
    ZHashNode* node = cursor;
    cursor = nullptr;
    return remove(node);
}

bool ZHash::remove(const void* key)
{
    int row;
    ZHashNode* node = find_node_row(key, &row);
    return remove(node);
}

int ZHash::set_keyops(
    unsigned (*hash_fcn)(SFHASHFCN* p, unsigned char*d, int n),
    int (*keycmp_fcn)(const void* s1, const void* s2, size_t n))
{
    if ( hash_fcn && keycmp_fcn )
        return sfhashfcn_set_keyops(sfhashfcn, hash_fcn, keycmp_fcn);

    return -1;
}

