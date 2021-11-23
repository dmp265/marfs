#ifndef _CONFIG_H
#define _CONFIG_H
/*
Copyright (c) 2015, Los Alamos National Security, LLC
All rights reserved.

Copyright 2015.  Los Alamos National Security, LLC. This software was
produced under U.S. Government contract DE-AC52-06NA25396 for Los
Alamos National Laboratory (LANL), which is operated by Los Alamos
National Security, LLC for the U.S. Department of Energy. The
U.S. Government has rights to use, reproduce, and distribute this
software.  NEITHER THE GOVERNMENT NOR LOS ALAMOS NATIONAL SECURITY,
LLC MAKES ANY WARRANTY, EXPRESS OR IMPLIED, OR ASSUMES ANY LIABILITY
FOR THE USE OF THIS SOFTWARE.  If software is modified to produce
derivative works, such modified software should be clearly marked, so
as not to confuse it with the version available from LANL.
 
Additionally, redistribution and use in source and binary forms, with
or without modification, are permitted provided that the following
conditions are met: 1. Redistributions of source code must retain the
above copyright notice, this list of conditions and the following
disclaimer.

2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
3. Neither the name of Los Alamos National Security, LLC, Los Alamos
National Laboratory, LANL, the U.S. Government, nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY LOS ALAMOS NATIONAL SECURITY, LLC AND
CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL LOS
ALAMOS NATIONAL SECURITY, LLC OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-----
NOTE:
-----
MarFS is released under the BSD license.

MarFS was reviewed and released by LANL under Los Alamos Computer Code
identifier: LA-CC-15-039.

MarFS uses libaws4c for Amazon S3 object communication. The original version
is at https://aws.amazon.com/code/Amazon-S3/2601 and under the LGPL license.
LANL added functionality to the original work. The original work plus
LANL contributions is found at https://github.com/jti-lanl/aws4c.

GNU licenses can be found at http://www.gnu.org/licenses/.
*/

#include "hash/hash.h"
#include "mdal/mdal.h"
#include <ne.h>

#define CONFIG_CTAG_LENGTH 32

typedef struct marfs_repo_struct marfs_repo;
typedef struct marfs_namespace_struct marfs_ns;

typedef enum {
   NS_NOACCESS = 0,    // 0  = 0b0000 -- No access at all
   NS_READMETA,        // 1  = 0b0001 -- Read access to metadata   ( readdir / stat / etc. )
   NS_WRITEMETA,       // 2  = 0b0010 -- Write access to metadata  ( open / mkdir / etc. )
   NS_RWMETA,          // 3  = 0b0011 -- Read and Write access to metadata
   NS_READDATA,        // 4  = 0b0100 -- Read access to data
   NS_WRITEDATA = 8,   // 8  = 0b1000 -- Write access to data
   NS_RWDATA = 12,     // 12 = 0b1100 -- Read and Write access to data
   NS_FULLACCESS = 15  // 15 = 0b1111 -- Read and Write for data and metadata
} ns_perms;


typedef struct marfs_namespace_struct {
   char*       idstr;        // unique (per-repo) ID of this namespace
   size_t      fquota;       // file quota of the namespace ( zero if no limit )
   size_t      dquota;       // data quota of the namespace ( zero if no limit )
   ns_perms    iperms;       // interactive access perms for this namespace
   ns_perms    bperms;       // batch access perms for this namespace
   marfs_repo* prepo;        // reference to the repo containing this namespace
   marfs_ns*   pnamespace;   // reference to the parent of this namespace
   HASH_TABLE  subspaces;    // subspace hash table, referencing namespaces below this one
   HASH_NODE*  subnodes;     // subnode list reference ( shared with table ) for safe iter
   size_t      subnodecount; // count of subnode references
} marfs_ns;
// NOTE -- namespaces will be wrapped in HASH_NODES for use in HASH_TABLEs
//         the HASH_NODE struct will provide the name string of the namespace


typedef struct marfs_datascheme_struct {
   ne_erasure protection;       // erasure defintion for writing out objects
   ne_ctxt    nectxt;           // LibNE context reference for data access
   size_t     objfiles;         // maximum count of files per data object (zero if no limit)
   size_t     objsize;          // maximum data object size (zero if no limit)
   HASH_TABLE podtable;         // hash table for object POD postion
   HASH_TABLE captable;         // hash table for object CAP position
   HASH_TABLE scattertable;     // hash table for object SCATTER position
} marfs_ds;


typedef struct marfs_metadatascheme_struct {
   MDAL       mdal;         // MDAL reference for metadata access
   char       directread;   // flag indicating support for data read from metadata files
   HASH_TABLE reftable;     // hash table for determining reference path
   HASH_NODE* refnodes;     // reference node list ( shared with table ) for safe iter
   size_t     refnodecount; // count of reference nodes
   int        nscount;      // count of the namespaces directly referenced by this repo
   HASH_NODE* nslist;       // array of namespaces directly referenced by this repo
} marfs_ms;


typedef struct marfs_repo_struct {
   char*     name;        // name of this repo
   marfs_ds  datascheme;  // struct defining the data structure of this repo
   marfs_ms  metascheme;  // struct defining the metadata structure of this repo
} marfs_repo;


typedef struct marfs_config_struct {
   char*       version;
   char*       mountpoint;
   char*       ctag;
   marfs_ns*   rootns;
   int         repocount;
   marfs_repo* repolist;
} marfs_config;

typedef struct marfs_position_struct {
   marfs_ns* ns;
   unsigned int depth;
   MDAL_CTXT ctxt;
} marfs_position;

/**
 * Initialize memory structures based on the given config file
 * @param const char* cpath : Path of the config file to be parsed
 * @return marfs_config* : Reference to the newly populated config structures
 */
marfs_config* config_init( const char* cpath );

/**
 * Destroy the given config structures
 * @param marfs_config* config : Reference to the config to be destroyed
 * @return int : Zero on success, or -1 on failure
 */
int config_term( marfs_config* config );

/**
 * Verifies the LibNE Ctxt of every repo, creates every namespace, creates all
 *  reference dirs in the given config, and verifies the LibNE CTXT
 * @param marfs_config* config : Reference to the config to be validated
 * @param char fix : If non-zero, attempt to correct any problems encountered
 * @return int : A count of uncorrected errors encountered, or -1 if a failure occurred
 */
int config_verify( marfs_config* config, char fix );

/**
 * Traverse the given path, idetifying a final NS target and resulting subpath
 * @param marfs_config* config : Config reference
 * @param marfs_position* pos : Reference populated with the initial position value
 *                              This will be updated to reflect the resulting position
 * @param char** subpath : Relative path from the tgtns
 *                         This will be updated to reflect the resulting subpath from
 *                         the new tgtns
 *                         NOTE -- this function may completely replace the
 *                         string reference
 * @param char linkchk : If zero, this function will not check for symlinks in the path.
 *                          All path componenets are assumed to be directories.
 *                       If non-zero, this function will perform a readlink() op on all
 *                          path components, substituting in the targets of all symlinks.
 * @return int : The depth of the path from the resulting NS target, 
 *               or -1 if a failure occurred
 */
int config_traverse( marfs_config* config, marfs_position* pos, char** subpath, char linkchk );

/**
 * Idetify the repo and NS path of the given NS ID string reference
 * @param const char* nsidstr : Reference to the NS ID string for which to retrieve info
 * @param char** repo : Reference to be populated with the name of the NS repo
 *                      NOTE -- it is the caller's responsibility to free this string
 * @param char** path : Reference to be populated with the path of the NS
 *                      NOTE -- it is the caller's responsibility to free this string
 * @return int : Zero on success;
 *               One, if the NS path is invalid ( likely means NS has no parent );
 *               -1 on failure.
 */
int config_nsinfo( const char* nsidstr, char** repo, char** path );

#endif // _CONFIG_H

