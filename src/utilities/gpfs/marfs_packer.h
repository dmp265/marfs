#ifndef MARFS_PACKER_H
#define MARFS_PACKER_H
/*
 * This file is part of MarFS, which is released under the BSD license.
 *
 *
 * Copyright (c) 2015, Los Alamos National Security (LANS), LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * -----
 *  NOTE:
 *  -----
 *  MarFS uses libaws4c for Amazon S3 object communication. The original version
 *  is at https://aws.amazon.com/code/Amazon-S3/2601 and under the LGPL license.
 *  LANS, LLC added functionality to the original work. The original work plus
 *  LANS, LLC contributions is found at https://github.com/jti-lanl/aws4c.
 *
 *  GNU licenses can be found at <http://www.gnu.org/licenses/>.
 *
 *
 *  From Los Alamos National Security, LLC:
 *  LA-CC-15-039
 *
 *  Copyright (c) 2015, Los Alamos National Security, LLC All rights reserved.
 *  Copyright 2015. Los Alamos National Security, LLC. This software was produced
 *  under U.S. Government contract DE-AC52-06NA25396 for Los Alamos National
 *  Laboratory (LANL), which is operated by Los Alamos National Security, LLC for
 *  the U.S. Department of Energy. The U.S. Government has rights to use,
 *  reproduce, and distribute this software.  NEITHER THE GOVERNMENT NOR LOS
 *  ALAMOS NATIONAL SECURITY, LLC MAKES ANY WARRANTY, EXPRESS OR IMPLIED, OR
 *  ASSUMES ANY LIABILITY FOR THE USE OF THIS SOFTWARE.  If software is
 *  modified to produce derivative works, such modified software should be
 *  clearly marked, so as not to confuse it with the version available from
 *  LANL.
 *
 *  THIS SOFTWARE IS PROVIDED BY LOS ALAMOS NATIONAL SECURITY, LLC AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 *  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL LOS ALAMOS NATIONAL SECURITY, LLC OR
 *  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 *  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 *  OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 *  IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 *  OF SUCH DAMAGE.
 *  */

#include <aws4c.h>
//#include <object_stream.h>
#include <marfs_base.h>

#define MAX_STACK_SIZE 1024


// This defines how many paths the treewalk will work on at a time
#define MAX_SCAN_FILE_COUNT 1024 

#define MAX_PATH_LENGTH 1024
//#define MAX_SCAN_FILE_COUNT 5 

typedef struct MarFS_XattrPost2 {
   uint16_t           config_vers_maj; // redundant w/ config_vers in Pre?
   uint16_t           config_vers_min; // redundant w/ config_vers in Pre?
   MarFS_ObjType      obj_type;      // type of storage
   int                obj_offset;    // offset of file in the obj (Packed)
   int                chunks;        // (context-dependent.  See NOTE)
   int                chunk_info_bytes; // total size of chunk-info in MDFS file (Multi)
   CorrectInfo        correct_info;  // correctness info  (e.g. the computed checksum)
   EncryptInfo        encrypt_info;  // any info reqd to decrypt the data
   PostFlagsType      flags;
   char               md_path[MARFS_MAX_MD_PATH]; // full path to MDFS file
} MarFS_XattrPost2;

struct walk_path{
	size_t inode;
	char path[1024];
	char parent[1024];
};

struct marfs_inode {
	time_t atime;
	time_t ctime;
	time_t mtime;
        size_t inode;
        int size;
        int offset;
	char path[1024];
        MarFS_XattrPre pre;
        MarFS_XattrPost post;
};
typedef struct inode_lnklist {
	struct marfs_inode val;
	int count;
	struct inode_lnklist *next;
} inode_lnklist;

typedef struct obj_lnklist {
   struct obj_lnklist *next;
   int count;
   struct inode_lnklist *val;
} obj_lnklist;

typedef struct pack_vars {
   size_t max_object_size;
   size_t small_object_size;
   ssize_t min_pack_file_size;
   ssize_t max_pack_file_size;
   ssize_t min_pack_file_count;
   ssize_t max_pack_file_count;
   FILE *outfd;
} pack_vars;



int get_objects(struct marfs_inode *unpacked, int unpacked_size, 
		obj_lnklist**  packed, int *packed_size, pack_vars *pack_params);
int pack_up(obj_lnklist *objects, MarFS_Repo* repo, MarFS_Namespace* ns);
int set_md(obj_lnklist *objects, pack_vars *pack_params);
int set_xattrs(size_t inode, int xattr);
int trash_inode(size_t inode); 
int push( struct walk_path stack[MAX_STACK_SIZE],int *top, struct walk_path *data);
int pop( struct walk_path stack[MAX_STACK_SIZE], int *top, struct walk_path *data);
void get_marfs_path(char * patht, char marfs[]);
void print_usage();
int walk_and_scan_control (char* top_level_path, const char* ns,
                            MarFS_Repo* repo, MarFS_Namespace* namespace,
                            uint8_t no_pack_flag, pack_vars *pack_params);
int get_inodes(const char *fnameP, struct marfs_inode *inode,
               int *marfs_inodeLen, size_t *sum_size, const char* namespace,
               struct walk_path *paths, pack_vars *pack_params);
int find_inode(size_t inode_number, struct walk_path *paths, pack_vars *pack_params);
int pack_and_write(char* top_level_path, MarFS_Repo* repo, 
                   MarFS_Namespace* namespace, const char *ns, 
                   struct walk_path *paths, uint8_t no_pack,
		   pack_vars *pack_params);
void free_objects(obj_lnklist *objects);
void free_sub_objects(inode_lnklist *sub_objects);
#endif

