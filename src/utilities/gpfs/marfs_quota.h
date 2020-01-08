#ifndef MARFS_QUOTA_H
#define MARFS_QUOTA_H
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


#include <stdint.h>
#include <stddef.h>             // size_t
#include <sys/types.h>          // ino_t
#include <sys/stat.h>
#include <math.h>               // floorf
#include <gpfs_fcntl.h>
// TEMP for now to allow buidling of config and usable fuse mount
#define NEW_CONFIG

#include "core/marfs_base.h"

//#define MAX_XATTR_VAL_LEN 64
//#define MAX_XATTR_NAME_LEN 32
//#define MAX_XATTR_VAL_LEN 1024 
//#define MAX_XATTR_NAME_LEN 1024 

#define MAX_FILESET_NAME_LEN 256
#define SMALL_FILE_MAX 4096
#define MEDIUM_FILE_MAX 1048576


#define MAX_PATH_LENGTH 4096

// Xattr info
#define MAX_MARFS_XATTR 3
#define MARFS_QUOTA_XATTR_CNT 4
#define MARFS_GC_XATTR_CNT 2

#ifdef DEBUG
   #define DEBUG_PRINT(fmt, ... ) fprintf(stderr, fmt, ##__VA_ARGS__);
#else
   #define DEBUG_PRINT(fmt, ... )
#endif

struct store_type {
  size_t uni_count;
  size_t multi_count;
  size_t packed_count;
};

typedef struct Marfs_Xattr {
// Getting the following array sizez from gpfs_fcntl.h
  char xattr_name[GPFS_FCNTL_XATTR_MAX_NAMELEN];
  char xattr_value[GPFS_FCNTL_XATTR_MAX_VALUELEN];

} Marfs_Xattr;

typedef struct Fileset_Stats {
      int fileset_id;
      char fileset_name[MAX_FILESET_NAME_LEN];
      //unsigned long long int sum_size;
      //FIX THIS NUMBER
      char fsinfo_path[1024];
      size_t sum_size;
      int sum_blocks;
      size_t sum_filespace_used;
      size_t sum_file_count;
      size_t sum_trash;
      size_t sum_trash_file_count;
      size_t adjusted_size;
      size_t sum_restart_size;
      size_t sum_restart_file_count;
      size_t small_count;
      size_t medium_count;
      size_t large_count;
      struct store_type obj_type;
} Fileset_Stats;


int read_inodes(const char *fnameP, FILE *outfd, int fileset_id, Fileset_Stats *fileset_stat_ptr, size_t rec_count, size_t offset_start);
int clean_exit(FILE *fd, gpfs_iscan_t *iscanP, gpfs_fssnap_handle_t *fsP, int terminate);
int get_xattr_value(Marfs_Xattr *xattr_ptr, const char *desired_xattr, int cnt, FILE *outfd);
size_t get_xattrs(gpfs_iscan_t *iscanP,
                 const char *xattrP,
                 unsigned int xattrLen,
                 const char **marfs_xattr,
                 int max_xattr_count,
                 Marfs_Xattr *xattr_ptr);
void print_usage();
void init_records(Fileset_Stats *fileset_stat_buf, unsigned int record_count);
int lookup_fileset(Fileset_Stats *fileset_stat_ptr, size_t rec_count, size_t offset_start, char *inode_fileset);
static void fill_size_histo(const gpfs_iattr_t *iattrP, Fileset_Stats *fileset_buffer, int index);
void write_fsinfo(FILE* outfd, Fileset_Stats* fileset_stat_ptr, size_t rec_count, size_t index_start, const char *root_dir, size_t non_marfs_cnt);
void update_type(MarFS_XattrPost * xattr_post, Fileset_Stats *fileset_stat_ptr, int index);
int lookup_fileset_path(Fileset_Stats *fileset_stat_ptr, size_t rec_count, int *trash_index, char *md_path_ptr);
Fileset_Stats * read_config(unsigned int *count);
int trunc_fsinfo(FILE* outfd, Fileset_Stats* fileset_stat_ptr, size_t rec_count, size_t index_start, const char *root_dir_fsinfo, size_t non_marfs_cnt);
#endif

