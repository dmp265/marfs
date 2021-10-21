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

MarFS uses libaws4c for Amazon S3 object communication. The original
version is at https://aws.amazon.com/code/Amazon-S3/2601 and under the
LGPL license.  LANL added functionality to the original work. The
original work plus LANL contributions is found at
https://github.com/jti-lanl/aws4c.

GNU licenses can be found at http://www.gnu.org/licenses/.
*/

#include "marfs_auto_config.h"
#if defined(DEBUG_ALL)  ||  defined(DEBUG_DATASTREAM)
   #define DEBUG 1
#endif
#define LOG_PREFIX "datastream"

#include <logging.h>
#include "datastream.h"
#include "general_include/numdigits.h"

#include <time.h>


//   -------------   INTERNAL DEFINITIONS    -------------


#define INITIAL_FILE_ALLOC 64
#define FILE_ALLOC_MULT     2


//   -------------   INTERNAL FUNCTIONS    -------------


void freestream( DATASTREAM stream ) {
   // shorthand references
   const marfs_ms* ms = &(stream->ns->prepo->metascheme);
   // abort any data handle
   if ( stream->datahandle  &&  ne_abort( stream->datahandle ) ) {
      LOG( LOG_WARNING, "Failed to abort stream datahandle\n" );
   }
   // free any string elements
   if ( stream->ctag ) { free(stream->ctag); }
   if ( stream->streamid ) { free( stream->streamid ); }
   if ( stream->ftagstr ) { free( stream->ftagstr ); }
   if ( stream->finfostr ) { free( stream->finfostr ); }
   if ( stream->finfo.path ) { free( stream->finfo.path ); }
   // iterate over all file references and clean them up
   if ( stream->files ) {
      size_t curfile = 0;
      for ( ; curfile < stream->curfile + 1; curfile++ ) {
         LOG( LOG_INFO, "Closing file %d\n", curfile );
         if ( stream->files[curfile].metahandle  &&  ms->mdal->close( stream->files[curfile].metahandle ) ) {
            LOG( LOG_WARNING, "Failed to close meta handle for file %zu\n", curfile );
         }
      }
      // free the file list itself
      free( stream->files );
   }
   // finally, free the stream itself
   free( stream );
}

size_t allocfiles( STREAMFILE** files, size_t current, size_t max ) {
   // calculate the target size of the file list
   size_t allocsize = 0;
   if ( current < INITIAL_FILE_ALLOC ) {
      allocsize = INITIAL_FILE_ALLOC;
   }
   else {
      allocsize = current * FILE_ALLOC_MULT;
   }
   if ( max  &&  allocsize > max ) { allocsize = max; }
   // realloc the list ( this is much simpler than allocating large linked list blocks )
   STREAMFILE* newfiles = realloc( *files, allocsize * sizeof( struct streamfile_struct ) );
   if ( newfiles == NULL ) {
      LOG( LOG_ERR, "Failed to allocate stream filelist of size %zu\n", allocsize );
      return 0;
   }
   // NULL out all metahandles, so that we never try to close() them
   for ( ; current < allocsize; current++ ) {
      newfiles[current].metahandle = NULL;
   }
   *files = newfiles;
   return allocsize;
}

int putftag( DATASTREAM stream, STREAMFILE* file ) {
   // shorthand references
   const marfs_ms* ms = &(stream->ns->prepo->metascheme);
   // populate the ftag string format
   size_t prres = ftag_tostr( &(file->ftag), stream->ftagstr, stream->ftagstrsize );
   if ( prres >= stream->ftagstrsize ) {
      stream->ftagstrsize = 0;
      free( stream->ftagstr );
      stream->ftagstr = malloc( sizeof(char) * (prres + 1) );
      if ( stream->ftagstr == NULL ) {
         LOG( LOG_ERR, "Failed to allocate space for ftag string\n" );
         return -1;
      }
      stream->ftagstrsize = prres + 1;
      // reattempt, with a longer target string
      prres = ftag_tostr( &(file->ftag), stream->ftagstr, stream->ftagstrsize );
      if ( prres >= stream->ftagstrsize ) {
         LOG( LOG_ERR, "Ftag string has an inconsistent length\n" );
         errno = EFAULT;
         return -1;
      }
   }
   if ( prres <= 0 ) {
      LOG( LOG_ERR, "Failed to populate ftag string for stream\n" );
      return -1;
   }
   if ( ms->mdal->fsetxattr( file->metahandle, 1, FTAG_NAME, stream->ftagstr, prres, 0 ) ) {
      LOG( LOG_ERR, "Failed to attach marfs ftag value: \"%s\"\n", stream->ftagstr );
      return -1;
   }

   return 0;
}

int getftag( DATASTREAM stream, STREAMFILE* file ) {
   // shorthand references
   const marfs_ms* ms = &(stream->ns->prepo->metascheme);
   // attempt to retrieve the ftag attr value ( leaving room for NULL terminator )
   size_t getres = ms->mdal->fgetxattr( file->metahandle, 1, FTAG_NAME, stream->ftagstr, stream->ftagstrsize - 1 );
   if ( getres >= stream->ftagstrsize ) {
      stream->ftagstrsize = 0;
      free( stream->ftagstr );
      stream->ftagstr = malloc( sizeof(char) * (getres + 1) );
      if ( stream->ftagstr == NULL ) {
         LOG( LOG_ERR, "Failed to allocate space for ftag string buffer\n" );
         return -1;
      }
      stream->ftagstrsize = getres + 1;
      // reattempt, with a longer target string
      getres = ms->mdal->fgetxattr( file->metahandle, 1, FTAG_NAME, stream->ftagstr, stream->ftagstrsize - 1 );
      if ( getres >= stream->ftagstrsize ) {
         LOG( LOG_ERR, "Ftag value of file has an inconsistent length\n" );
         errno = EBUSY;
         return -1;
      }
   }
   if ( getres <= 0 ) {
      LOG( LOG_ERR, "Failed to retrieve ftag value for stream file\n" );
      return -1;
   }
   // ensure our string is NULL terminated
   *( stream->ftagstr + getres ) = '\0';
   // attempt to set struct values based on the ftag string
   if ( ftag_initstr( &(file->ftag), stream->ftagstr ) ) {
      LOG( LOG_ERR, "Failed to initialize ftag values for file\n" );
      return -1;
   }
   return 0;
}

int linkfile( DATASTREAM stream, const char* refpath, const char* tgtpath, MDAL_CTXT ctxt ) {
   // shorthand references
   const marfs_ms* ms = &(stream->ns->prepo->metascheme);
   // attempt to link the specified file to the specified user path
   if ( ms->mdal->linkref( ctxt, refpath, tgtpath ) ) {
      // if we got EEXIST, attempt to unlink the existing target and retry
      if ( errno != EEXIST ) {
         // any non-EEXIST error is fatal
         LOG( LOG_ERR, "Failed to link reference file to final location\n" );
         return -1;
      }
      else if ( ms->mdal->unlink( ctxt, tgtpath )  &&  errno != ENOENT ) {
         // ENOENT would indicate that another proc has unlinked the conflicting file for us
         //   Otherwise, we have to fail
         LOG( LOG_ERR, "Failed to unlink existing file: \"%s\"\n", tgtpath );
         return -1;
      }
      else if ( ms->mdal->linkref( ctxt, refpath, tgtpath ) ) {
         // This indicates either we're racing with another proc, or something more unusual
         //   Just fail out with whatever errno we get from flink()
         LOG( LOG_ERR, "Failed to link reference file to final location after retry\n" );
         return -1;
      }
   }
   return 0;
}

char* genrpath( DATASTREAM stream, STREAMFILE* file ) {
   // shorthand references
   const marfs_ms* ms = &(stream->ns->prepo->metascheme);
   // generate the meta reference name of this file
   size_t rnamelen = ftag_metatgt( &(file->ftag), NULL, 0 );
   if ( rnamelen < 1 ) {
      LOG( LOG_ERR, "Failed to generate file meta reference name\n" );
      return NULL;
   }
   char* refname = malloc( sizeof(char) * (rnamelen + 1) );
   if ( refname == NULL ) {
      LOG( LOG_ERR, "Failed to allocate a temporary meta reference string\n" );
      return NULL;
   }
   if ( ftag_metatgt( &(file->ftag), refname, rnamelen + 1 ) != rnamelen ) {
      LOG( LOG_ERR, "Inconsistent length of file meta reference string\n" );
      free( refname );
      return NULL;
   }
   // determine the target reference path of this file
   HASH_NODE* noderef = NULL;
   if ( hash_lookup( ms->reftable, refname, &(noderef) ) < 0 ) {
      LOG( LOG_ERR, "Failed to identify reference path for metaname \"%s\"\n", refname );
      free( refname );
      return NULL;
   }
   // populate the complete rpath
   size_t rpathlen = strlen(noderef->name) + strlen(refname);
   char* rpath = malloc( sizeof(char) * ( rpathlen + 1 ) );
   if ( rpath == NULL ) {
      LOG( LOG_ERR, "Failed to allocate rpath string\n" );
      free( refname );
      return NULL;
   }
   if ( snprintf( rpath, rpathlen + 1, "%s%s", noderef->name, refname ) != rpathlen ) {
      LOG( LOG_ERR, "Failed to populate rpath string\n" );
      free( refname );
      free( rpath );
      errno = EFAULT;
      return NULL;
   }
   free( refname ); // done with this tmp string
   return rpath;
}

int genrecoveryinfo( DATASTREAM stream, RECOVERY_FINFO* finfo, STREAMFILE* file, const char* path ) {
   // shorthand references
   const marfs_ms* ms = &(stream->ns->prepo->metascheme);
   // identify file attributes, for recovery info
   struct stat stval;
   if ( ms->mdal->fstat( file->metahandle, &(stval) ) ) {
      LOG( LOG_ERR, "Failed to stat meta file for recovery info values\n" );
      return -1;
   }

   // populate recovery info inode/mtime and calculate recovery finfo length
   finfo->inode = stval.st_ino;
   finfo->mode = stval.st_mode;
   finfo->owner = stval.st_uid;
   finfo->group = stval.st_gid;
   finfo->size = 0;
   finfo->mtime.tv_sec = stval.st_mtim.tv_sec;
   finfo->mtime.tv_nsec = stval.st_mtim.tv_nsec;
   finfo->eof = 0;
   if ( stream->type == READ_STREAM ) {
      finfo->size = stval.st_size;
      return 0;
   }
   finfo->path = strdup( path );
   if ( finfo->path == NULL ) {
      LOG( LOG_ERR, "Failed to duplicate file path into recovery info\n" );
      return -1;
   }

   // align our finalized file times with those we will be using in recovery info
   file->times[0] = stval.st_atim;
   file->times[1] = stval.st_mtim;

   // calculate the length of the recovery info
   size_t recoverybytes = recovery_finfotostr( finfo, NULL, 0 );
   if ( recoverybytes == 0 ) {
      LOG( LOG_ERR, "Failed to calculate recovery info size for \"%s\"\n", path );
      free( finfo->path );
      finfo->path = NULL;
      return -1;
   }

   // populate the recovery size, if absent
   // NOTE -- if the recovery size is inconsistent with the existing size, this will 
   //         be caught when attempting to write out recovery info
   if ( file->ftag.recoverybytes == 0 ) { file->ftag.recoverybytes = recoverybytes; }

   return 0;
}

int create_new_file( DATASTREAM stream, const char* path, MDAL_CTXT ctxt, mode_t mode ) {
   // NOTE -- it is the responsibility of the caller to set curfile/fileno/objno/offset
   //         values to the appropraite start positions prior to calling
   // populate shorthand references
   const marfs_ms* ms = &(stream->ns->prepo->metascheme);
   const marfs_ds* ds = &(stream->ns->prepo->datascheme);
   // construct a reference struct for our new file
   struct streamfile_struct newfile = 
   {
      .metahandle = NULL,
      .ftag.majorversion = FTAG_CURRENT_MAJORVERSION,
      .ftag.minorversion = FTAG_CURRENT_MINORVERSION,
      .ftag.ctag = stream->ctag,
      .ftag.streamid = stream->streamid,
      .ftag.objfiles = ds->objfiles,
      .ftag.objsize = ds->objsize,
      .ftag.fileno = stream->fileno,
      .ftag.objno = stream->objno,   // potentially modified below
      .ftag.offset = stream->offset, // potentially modified below
      .ftag.endofstream = 0,
      .ftag.protection = ds->protection,
      .ftag.bytes = 0,
      .ftag.availbytes = 0,
      .ftag.recoverybytes = 0, // modified below
      .ftag.state = FTAG_INIT,
      .times[0].tv_sec = 0,
      .times[0].tv_nsec = 0,
      .times[1].tv_sec = 0,
      .times[1].tv_nsec = 0,
      .dotimes = 1
   };

   // establish a reference path for the new file
   char* newrpath = genrpath( stream, &(newfile) );
   if ( newrpath == NULL ) {
      LOG( LOG_ERR, "Failed to identify reference path for stream\n" );
      if ( errno == EBADFD ) { errno = ENOMSG; } // don't allow our reserved EBADFD value
      return -1;
   }

   // create the reference file, ensuring we don't collide with an existing reference
   newfile.metahandle = ms->mdal->openref( ctxt, newrpath, O_CREAT | O_EXCL | O_WRONLY, mode );
   if ( newfile.metahandle == NULL ) {
      LOG( LOG_ERR, "Failed to create reference meta file: \"%s\"\n", newrpath );
      // a BUSY error is more indicative of the real problem
      if ( errno = EEXIST ) { errno = EBUSY; }
      // don't allow our reserved EBADFD value
      else if ( errno == EBADFD ) { errno = ENOMSG; }
      free( newrpath );
      return -1;
   }

   // identify file recovery info
   RECOVERY_FINFO newfinfo;
   if ( genrecoveryinfo( stream, &(newfinfo), &(newfile), path ) ) {
      LOG( LOG_ERR, "Failed to populate recovery info for file: \"%s\"\n", path );
      ms->mdal->unlinkref( ctxt, newrpath );
      free( newrpath );
      if ( errno == EBADFD ) { errno = ENOMSG; } // don't allow our reserved EBADFD value
      return -1;
   }

   // ensure the recovery info size is compatible with the current object size
   if ( newfile.ftag.objsize  &&  (stream->recoveryheaderlen + newfile.ftag.recoverybytes) >= newfile.ftag.objsize ) {
      LOG( LOG_ERR, "Recovery info size of new file is incompatible with current object size\n" );
      ms->mdal->unlinkref( ctxt, newrpath );
      free( newrpath );
      errno = ENAMETOOLONG; // this is most likely an issue with path length
      return -1;
   }

   // ensure that the current object still has space remaining for this file
   if ( newfile.ftag.objsize  &&  (newfile.ftag.objsize - stream->offset) < newfile.ftag.recoverybytes ) {
      // we're too far into the current obj to fit any more data
      LOG( LOG_INFO, "Shifting to new object, as current can't hold recovery info\n" );
      newfile.ftag.objno++;
      newfile.ftag.offset = stream->recoveryheaderlen;
   }
   else if ( newfile.ftag.objfiles  &&  stream->curfile >= newfile.ftag.objfiles ) {
      // there are too many files in the current obj to fit this one
      LOG( LOG_INFO, "Shifting to new object, as current can't hold another file\n" );
      newfile.ftag.objno++;
      newfile.ftag.offset = stream->recoveryheaderlen;
   }

   // attach updated ftag value to the new file
   if ( putftag( stream, &(newfile) ) ) {
      LOG( LOG_ERR, "Failed to initialize FTAG value on target file\n" );
      ms->mdal->unlinkref( ctxt, newrpath );
      free( newrpath );
      if ( errno == EBADFD ) { errno = ENOMSG; } // don't allow our reserved EBADFD value
      return -1;
   }

   // link the new file into the user namespace
   if ( linkfile( stream, newrpath, path, ctxt ) ) {
      LOG( LOG_ERR, "Failed to link reference file to target user path: \"%s\"\n", path );
      ms->mdal->unlinkref( ctxt, newrpath );
      free( newrpath );
      if ( errno == EBADFD ) { errno = ENOMSG; } // don't allow our reserved EBADFD value
      return -1;
   }

   // check if the current stream has space for this new file ref
   if ( stream->curfile >= stream->filealloc ) {
      stream->filealloc = allocfiles( &(stream->files), stream->filealloc, ds->objfiles );
      if ( stream->filealloc == 0 ) {
         LOG( LOG_ERR, "Failed to expand file list allocation\n" );
         stream->filealloc = stream->curfile - 1;
         ms->mdal->unlinkref( ctxt, newrpath );
         free( newrpath );
         if ( errno == EBADFD ) { errno = ENOMSG; } // don't allow our reserved EBADFD value
         return -1;
      }
   }
   free( newrpath ); // finally done with rpath

   // update the stream with new file information
   stream->files[ stream->curfile ] = newfile;
   stream->finfo = newfinfo;
   stream->fileno = newfile.ftag.fileno;
   stream->objno = newfile.ftag.objno;
   stream->offset = newfile.ftag.offset;

   return 0;
}

int open_existing_file( DATASTREAM stream, const char* path, MDAL_CTXT ctxt ) {

   // open a handle for the target file
   MDAL curmdal = stream->ns->prepo->metascheme.mdal;
   if ( stream->type == READ_STREAM ) {
      stream->files[ stream->curfile ].metahandle = curmdal->open( ctxt, path, O_RDONLY );
   }
   else {
      stream->files[ stream->curfile ].metahandle = curmdal->open( ctxt, path, O_WRONLY );
   }
   if ( stream->files[ stream->curfile ].metahandle == NULL ) {
      LOG( LOG_ERR, "Failed to open metahandle for target file: \"%s\"\n", path );
      return -1;
   }

   // retrieve the file's FTAG info
   if ( getftag( stream, stream->files + stream->curfile ) ) {
      LOG( LOG_ERR, "Failed to retrieve FTAG value of target file: \"%s\"\n", path );
      curmdal->close( stream->files[ stream->curfile ].metahandle );
      stream->files[ stream->curfile ].metahandle = NULL;
      return -1;
   }
   stream->files[ stream->curfile ].dotimes = 0; // make sure our time flag is cleared

   // populate RECOVERY_FINFO
   if ( genrecoveryinfo( stream, &(stream->finfo), stream->files + stream->curfile, path ) ) {
      LOG( LOG_ERR, "Failed to identify recovery info for target file: \"%s\"\n", path );
      curmdal->close( stream->files[ stream->curfile ].metahandle );
      stream->files[ stream->curfile ].metahandle = NULL;
      return -1;
   }

   // the stream inherits string values from the FTAG
   stream->ctag = stream->files[ stream->curfile ].ftag.ctag;
   stream->streamid = stream->files[ stream->curfile ].ftag.streamid;
   return 0;
}

int open_current_obj( DATASTREAM stream ) {
   // shorthand references
   const marfs_ds* ds = &(stream->ns->prepo->datascheme);

   // find the length of the current object name
   FTAG tgttag = stream->files[stream->curfile].ftag;
   tgttag.objno = stream->objno; // we actually want the stream object number
   tgttag.offset = stream->offset;
   ssize_t objnamelen = ftag_datatgt( &(tgttag), NULL, 0 );
   if ( objnamelen <= 0 ) {
      LOG( LOG_ERR, "Failed to determine object path from current ftag\n" );
      return -1;
   }
   // allocate a new string, and populate it with the object name
   char* objname = malloc( sizeof(char) * (objnamelen + 1) );
   if ( objname == NULL ) {
      LOG( LOG_ERR, "Failed to allocate space for new object name\n" );
      return -1;
   }
   if ( objnamelen != ftag_datatgt( &(tgttag), objname, objnamelen + 1 ) ) {
      LOG( LOG_ERR, "Ftag producing inconsistent object name string\n" );
      free( objname );
      return -1;
   }

   // identify the pod/cap/scatter values for the current object
   ne_location location = { .pod = -1, .cap = -1, .scatter = -1 };
   int iteration = 0;
   for( ; iteration < 3; iteration++ ) {
      // determine which table we are currently pulling from
      HASH_TABLE curtable = ds->scattertable;
      int* tgtval = &(location.scatter);
      if ( iteration < 1 ) {
         curtable = ds->podtable;
         tgtval = &(location.pod);
      }
      else if ( iteration < 2 ) {
         curtable = ds->captable;
         tgtval = &(location.cap);
      }
      // hash our object name, to identify a target node
      HASH_NODE* node = NULL;
      if ( hash_lookup( curtable, objname, &node ) < 0 ) {
         LOG( LOG_ERR, "Failed to lookup %s location for new object \"%s\"\n",
                       (iteration < 1) ? "pod" : (iteration < 2) ? "cap" : "scatter",
                       objname );
         free( objname );
         return -1;
      } 
      // parse our nodename, to produce an integer value
      char* endptr = NULL;
      unsigned long long parseval = strtoull( node->name, &(endptr), 10 );
      if ( *endptr != '\0'  ||  parseval >= INT_MAX ) {
         LOG( LOG_ERR, "Failed to parse %s value of \"%s\" for new object \"%s\"\n",
                       (iteration < 1) ? "pod" : (iteration < 2) ? "cap" : "scatter",
                       node->name, objname );
         free( objname );
         return -1;
      }
      // assign the parsed value to the appropriate var
      *tgtval = (int)parseval;
   }

   // identify the erasure scheme
   ne_erasure tmperasure = tgttag.protection;
   tmperasure.O = (int)(hash_rangevalue( objname, tmperasure.N + tmperasure.E )); // produce tmperasure offset value
   LOG( LOG_INFO, "Object: \"%s\"\n", objname );
   LOG( LOG_INFO, "Position: pod%d, cap%d, scatter%d\n",
                  location.pod, location.cap, location.scatter );
   LOG( LOG_INFO, "Erasure: N=%d,E=%d,O=%d,psz=%zu\n",
                  tmperasure.N, tmperasure.E, tmperasure.O, tmperasure.partsz );

   // open a handle for the new object
   if ( stream->type == READ_STREAM ) {
      stream->datahandle = ne_open( ds->nectxt, objname, location, tmperasure, NE_RDALL );
   }
   else {
      stream->datahandle = ne_open( ds->nectxt, objname, location, tmperasure, NE_WRALL );
   }
   if ( stream->datahandle == NULL ) {
      LOG( LOG_ERR, "Failed to open object \"%s\"\n", objname );
      free( objname );
      return -1;
   }
   free( objname ); // done with object name

   if ( stream->type == READ_STREAM ) {
      // if we're reading, we may need to seek to a specific offset
      if ( stream->offset ) {
         if ( stream->offset != ne_seek( stream->datahandle, stream->offset ) ) {
            LOG( LOG_ERR, "Failed to seek to offset %zu of object %zu\n", stream->offset, stream->objno );
            return -1;
         }
      }
   }
   else {
      // our offset value should match the recovery header length
      if ( stream->offset != stream->recoveryheaderlen ) {
         LOG( LOG_ERR, "Stream offset does not match recovery header length of %zu\n",
                       stream->recoveryheaderlen );
         ne_abort( stream->datahandle );
         stream->datahandle = NULL;
         return -1;
      }

      // if we're writing out a new object, output a recovery header
      RECOVERY_HEADER header = 
      {
         .majorversion = RECOVERY_CURRENT_MAJORVERSION,
         .minorversion = RECOVERY_CURRENT_MINORVERSION,
         .ctag = stream->ctag,
         .streamid = stream->streamid
      };
      char* recovheader = malloc( sizeof(char) * (stream->recoveryheaderlen + 1) );
      if ( recovheader == NULL ) {
         LOG( LOG_ERR, "Failed to allocate space for recovery header string\n" );
         ne_abort( stream->datahandle );
         stream->datahandle = NULL;
         return -1;
      }
      if ( recovery_headertostr( &(header), recovheader, stream->recoveryheaderlen + 1 ) != stream->recoveryheaderlen ) {
         LOG( LOG_ERR, "Recovery header string has inconsistent length (expected %zu)\n",
                       stream->recoveryheaderlen );
         ne_abort( stream->datahandle );
         stream->datahandle = NULL;
         free( recovheader );
         errno = EFAULT;
         return -1;
      }
      if ( ne_write( stream->datahandle, recovheader, stream->recoveryheaderlen ) != stream->recoveryheaderlen ) {
         LOG( LOG_ERR, "Failed to write recovery header to new data object\n" );
         ne_abort( stream->datahandle );
         stream->datahandle = NULL;
         free( recovheader );
         return -1;
      }
      free( recovheader ); // done with recovery header string
   }

   return 0;
}

int close_current_obj( DATASTREAM stream, char** rtagstr ) {
   int closeres = 0;
   ne_state objstate;
   size_t rtagstrlen = 0;
   char abortflag = 0;
   if ( newstream->datahandle != NULL ) {
      closeres = ne_close( newstream->datahandle, NULL, &(objstate) );
      newstream->datahandle = NULL; // never reattempt this process
   }
   if ( closeres > 0 ) {
      // object synced, but with errors
      // generate a rebuild tag to mark for future repair
      rtagstrlen = rtag_tostr( &(objstate), NULL, 0 );
      if ( rtagstrlen == 0 ) {
         LOG( LOG_ERR, "Failed to identify rebuild tag length\n" );
         abortflag = 1;
      }
      *rtagstr = malloc( sizeof(char) * (rtagstrlen + 1) );
      if ( *rtagstr == NULL ) {
         LOG( LOG_ERR, "Failed to allocate space for rebuild tag string\n" );
         abortflag = 1;
      }
      if ( rtag_tostr( &(objstate), *rtagstr, rtagstrlen + 1 ) != rtagstrlen ) {
         LOG( LOG_ERR, "Rebuild tag has inconsistent length\n" );
         free( *rtagstr );
         *rtagstr = NULL;
         abortflag = 1;
      }
      LOG( LOG_INFO, "Attaching rebuild tag: \"%s\"\n", rtagstr );
   }
   if ( closeres < 0 ) {
      LOG( LOG_ERR, "ne_close() indicates failure for object %zu\n", curobj );
      abortflag = 1;
   }
   if ( abortflag ) { return -1; }

   return 0;
}

DATASTREAM genstream( STREAM_TYPE type, const char* path, marfs_position* pos, mode_t mode, const char* ctag ) {
   // create some shorthand references
   marfs_ds* ds = &(pos->ns->prepo->datascheme);

   // allocate the new datastream and check for success
   DATASTREAM stream = malloc( sizeof( struct datastream_struct ) );
   if ( stream == NULL ) {
      LOG( LOG_ERR, "Failed to allocate space for a datastream\n" );
      return NULL;
   }
   // populate default stream values
   stream->type = type;
   stream->ctag = NULL;
   stream->streamid = NULL;
   stream->ns = pos->ns;
   stream->recoveryheaderlen = 0; // redefined below
   stream->fileno = 0;
   stream->objno = 0;
   stream->offset = 0; // redefined below
   stream->datahandle = NULL;
   stream->files = NULL; // redefined below
   stream->curfile = 0;
   stream->filealloc = 0; // redefined below
   stream->ftagstr = malloc( sizeof(char) * 512 );
   stream->ftagstrsize = 512;
   stream->finfostr = malloc( sizeof(char) * 512 );
   stream->finfostrlen = 512;
   // zero out all recovery finfo values; those will be populated later, if needed
   stream->finfo.inode = 0;
   stream->finfo.mode = 0;
   stream->finfo.owner = 0;
   stream->finfo.group = 0;
   stream->finfo.size = 0;
   stream->finfo.mtime.tv_sec = 0;
   stream->finfo.mtime.tv_nsec = 0;
   stream->finfo.eof = 0;
   stream->finfo.path = NULL;
   // verify that all string allocations succeeded
   if ( stream->ftagstr == NULL  ||  stream->finfostr == NULL ) {
      LOG( LOG_ERR, "Failed to allocate space for stream string elements\n" );
      freestream( stream );
      return NULL;
   }

   // allocate our first file reference(s)
   if ( type == READ_STREAM  ||  type == EDIT_STREAM ) {
      // Read streams should only ever expect a single file to be referenced at a time
      // Edit streams will *likely* only reference a single file, but may reference more later
      stream->filealloc = allocfiles( &(stream->files), stream->curfile, 1 );
   }
   else if ( type == CREATE_STREAM ) {
      // Create streams are only restriced by the object packing limits
      stream->filealloc = allocfiles( &(stream->files), stream->curfile, ds->objfiles );
   }
   if ( stream->filealloc == 0 ) {
      LOG( LOG_ERR, "Failed to allocate space for streamfiles OR received ERROR type\n" );
      freestream( stream );
      return NULL;
   }

   // populate info for the first stream file
   stream->files[0].metahandle = NULL;
   stream->files[0].ftag.majorversion = FTAG_CURRENT_MAJORVERSION;
   stream->files[0].ftag.minorversion = FTAG_CURRENT_MINORVERSION;
   stream->files[0].ftag.ctag = stream->ctag;
   stream->files[0].ftag.streamid = stream->streamid;
   stream->files[0].ftag.objfiles = ds->objfiles;
   stream->files[0].ftag.objsize = ds->objsize;
   stream->files[0].ftag.fileno = 0;
   stream->files[0].ftag.objno = 0;
   stream->files[0].ftag.endofstream = 0;
   stream->files[0].ftag.offset = 0;
   stream->files[0].ftag.protection = ds->protection;
   stream->files[0].ftag.bytes = 0;
   stream->files[0].ftag.availbytes = 0;
   stream->files[0].ftag.recoverybytes = 0;
   stream->files[0].ftag.state = FTAG_INIT; // no data written and no other access
   stream->files[0].times[0].tv_sec = 0;
   stream->files[0].times[0].tv_nsec = 0;
   stream->files[0].times[1].tv_sec = 0;
   stream->files[0].times[1].tv_nsec = 0;
   stream->files[0].dotimes = 0;

   // perform type-dependent initialization
   if ( type == CREATE_STREAM ) {
      // set the ctag value
      stream->ctag = strdup( ctag );
      if ( stream->ctag == NULL ) {
         LOG( LOG_ERR, "Failed to allocate space for stream Client Tag\n" );
         freestream( stream );
         return NULL;
      }

      // generate a new streamID
      // NOTE -- This is the ONLY location in the MarFS code where streamIDs are generated.
      //         All other funcs take the existing ID value.
      struct timespec curtime;
      if ( clock_gettime( CLOCK_REALTIME, &curtime ) ) {
         LOG( LOG_ERR, "Failed to determine the current time\n" );
         freestream( stream );
         return NULL;
      }
      char* nsrepo = NULL;
      char* nspath = NULL;
      if ( config_nsinfo( stream->ns->idstr, &(nsrepo), &(nspath) ) ) {
         LOG( LOG_ERR, "Failed to retrieve path/repo info for this stream's NS\n" );
         freestream( stream );
         return NULL;
      }
      char* parse = nspath;
      size_t nspathlen = 0;
      for( ; *parse != '\0'; parse++ ) {
         if ( *parse == '/' ) { *parse = '#'; }
         nspathlen++;
      }
      size_t streamidlen = SIZE_DIGITS;  // to account for tv_sec (see numdigits.h)
      streamidlen += SIZE_DIGITS; // to account for tv_nsec
      streamidlen += strlen( nsrepo ) + nspathlen; // to include NS/Repo info
      streamidlen += 4; // for '|'/'#'/'.' seperators and null terminator
      if ( (stream->streamid = malloc( sizeof(char) * streamidlen )) == NULL ) {
         LOG( LOG_ERR, "Failed to allocate space for streamID\n" );
         free( nsrepo );
         free( nspath );
         freestream( stream );
         return NULL;
      }
      ssize_t prres = snprintf( stream->streamid, streamidlen, "%s|%s|%zd.%ld",
                                nsrepo, nspath, curtime.tv_sec, curtime.tv_nsec );
      if ( prres <= 0  ||  prres >= streamidlen ) {
         LOG( LOG_ERR, "Failed to generate streamID value\n" );
         free( nsrepo );
         free( nspath );
         freestream( stream );
         return NULL;
      }
      free( nsrepo );
      free( nspath );

      // establish our recovery header length
      RECOVERY_HEADER header = {
         .majorversion = RECOVERY_CURRENT_MAJORVERSION,
         .minorversion = RECOVERY_CURRENT_MINORVERSION,
         .ctag = stream->ctag,
         .streamid = stream->streamid
      };
      stream->recoveryheaderlen = recovery_headertostr( &(header), NULL, 0 );
      if ( stream->recoveryheaderlen < 1 ) {
         LOG( LOG_ERR, "Failed to identify length of create stream recov header\n" );
         freestream( stream );
         return NULL;
      }
      stream->offset = stream->recoveryheaderlen;

      // create the output file
      if ( create_new_file( stream, path, pos->ctxt, mode ) ) {
         LOG( LOG_ERR, "Failed to create output file: \"%s\"\n", path );
         freestream( stream );
         return NULL;
      }

   }
   else {
      // open an existing file and populate stream info
      if ( open_existing_file( stream, path, pos->ctxt ) ) {
         LOG( LOG_ERR, "Failed to initialize stream for file: \"%s\"\n", path );
         freestream( stream );
         return NULL;
      }
      // perform type dependent state checks
      FTAG_STATE curstate = (stream->files + stream->curfile)->ftag.state;
      if ( type == EDIT_STREAM  &&
           !(curstate & FTAG_WRITEABLE)  &&
           (curstate & FTAG_DATASTATE) != FTAG_COMP ) {
         LOG( LOG_ERR, "Cannot edit a non-complete, non-extended file\n" );
         freestream( stream );
         errno = EPERM;
         return NULL;
      }
      if ( type == READ_STREAM  &&
           !(curstate & FTAG_READABLE) ) {
         LOG( LOG_ERR, "Target file is not yet readable\n" );
         freestream( stream );
         errno = EPERM;
         return NULL;
      }
   }

   return stream;
}

int gettargets( DATASTREAM stream, off_t offset, int whence, size_t* tgtobjno, size_t* tgtoffset, size_t* remaining, size_t* maxobjdata ) {
   // establish some bounds values that we'll need later
   FTAG curtag = stream->files[stream->curfile].ftag;
   size_t dataperobj = ( curtag.objsize - (curtag.recoverybytes + stream->recoveryheaderlen) );
   size_t minobj = curtag.objno;
   size_t minoffset = curtag.offset - stream->recoveryheaderlen; // data space already occupied in first obj

   // convert all values to a seek from the start of the file ( convert to a whence == SEEK_SET offset )
   if ( whence == SEEK_END ) {
      offset += curtag.availbytes; // just add the total file size
   }
   else if ( whence == SEEK_CUR ) {
      if ( stream->objno > minobj ) {
         offset += (dataperobj - minoffset); // add the data in the first obj
         offset += ( stream->objno - (minobj + 1) ) * dataperobj; // add the data of all complete objs
         if ( stream->offset ) {
            offset += ( stream->offset - stream->recoveryheaderlen ); // add the data in the current obj
         }
      }
      else if ( stream->offset ) {
         offset += ( (stream->offset - stream->recoveryheaderlen) - minoffset );
      }
   }
   else if ( whence != SEEK_SET ) {
      // catch an unknown 'whence' value
      LOG( LOG_ERR, "Invalid value of 'whence'\n" );
      errno = EINVAL;
      return -1;
   }
   // regardless of 'whence', we are now seeking from the min values ( beginning of file )
   if ( offset > curtag.availbytes ) {
      // make sure we aren't seeking beyond the end of the file
      LOG( LOG_ERR, "Offset value extends beyond end of file\n" );
      errno = EINVAL;
      return -1;
   }
   if ( offset < 0 ) {
      // make sure we aren't seeking before the start of the file
      LOG( LOG_ERR, "Offset value extends prior to beginning of file\n" );
      errno = EINVAL;
      return -1;
   }
   size_t tgtobj = minobj;
   size_t tgtoff = minoffset;
   size_t remain = curtag.availbytes - offset;
   if ( (offset + minoffset) > dataperobj ) {
      // this offset will cross object boundaries
      offset -= (dataperobj - minoffset);
      tgtobj += ( offset / dataperobj ) + 1;
      tgtoff = offset % dataperobj;
   }
   else {
      tgtoff += offset;
   }

   // finally, populate our final values, omitting recovery info
   if ( tgtobjno ) { *tgtobjno = tgtobj; }
   if ( tgtoffset ) { *tgtoffset = tgtoff; }
   if ( remaining ) { *remaining = remain; }
   if ( maxobjdata ) { *maxobjdata = dataperobj; }
   return 0;
}

int putfinfo( DATASTREAM stream ) {
   // allocate a recovery info string
   size_t recoverybytes = stream->files[ stream->curfile ].ftag.recoverybytes;
   if ( recoverybytes > stream->finfostrlen ) {
      LOG( LOG_INFO, "Allocating extended finfo string of %zu bytes\n", recoverybytes + 1 );
      char* oldstr = stream->finfostr;
      stream->finfostr = malloc( sizeof(char) * (recoverybytes + 1) );
      if ( stream->finfostr == NULL ) {
         LOG( LOG_ERR, "Failed to allocate new finfo string\n" );
         stream->finfostr = oldstr;
         return -1;
      }
      stream->finfostrlen = recoverybytes;
      free( oldstr );
   }
   // populate recovery info string
   size_t genbytes = recovery_finfotostr( &(stream->finfo), stream->finfostr, stream->finfostrlen + 1 );
   if ( genbytes > recoverybytes ) {
      LOG( LOG_ERR, "File recovery info has an inconsistent length\n" );
      return -1;
   }
   else if ( genbytes < recoverybytes ) {
      // finfo string is shorter than expected, so zero out the unused tail of the string
      bzero( stream->finfostr + genbytes, (recoverybytes + 1) - genbytes );
   }
   // Note -- previous writes should have ensured we have at least 'recoverybytes' of 
   //         available spaece to write out the recovery string
   if ( ne_write( stream->datahandle, stream->finfostr, recoverybytes ) != recoverybytes ) {
      LOG( LOG_ERR, "Failed to store file recovery info to data object\n" );
      return -1;
   }
   stream->offset += recoverybytes; // update our object offset
   return 0;
} 

int finfile( DATASTREAM stream ) {
   // get a reference to the active file
   STREAMFILE* curfile = stream->files + stream->curfile;

   // only perform this action if the file has not yet been finalized
   if ( (curfile->ftag.state & FTAG_DATASTATE) < FTAG_FIN ) {
      if ( curfile->ftag.bytes == 0  &&  stream->datahandle == NULL ) {
         // special case, non-extended create stream with no current data content
         // Open the output object to record recov info for zero-length file
         if ( open_current_obj( stream ) ) {
            LOG( LOG_ERR, "Failed to open output object for zero-length prev file\n" );
            return -1;
         }
      }
      if ( stream->datahandle ) {
         // end of prev file
         stream->finfo.eof = 1;
         //  output recovery info for the prev file
         if ( putfinfo( stream ) ) {
            LOG( LOG_ERR, "Failed to output prev file recovery info\n" );
            stream->finfo.eof = 0;
            return -1;
         }
      }
      else {
         // this is an extended file and thus can't pack,
         //   so proceed to the next object by default
         stream->objno++;
         stream->offset = stream->recoveryheaderlen;
      }
      // set data state to 'FINALIZED', ensuring we never reattempt with this same handle
      curfile->ftag.state = ( curfile->ftag.state & ~(FTAG_DATASTATE) ) | FTAG_FIN;
   }

   return 0;
}

/**
 * Completes the given file: truncating to appropriate length, setting the FTAG to a 
 *  complete + readable state, setting file times, and closing the meta handle
 * @param DATASTREAM stream : Current DATASTREAM
 * @param STREAMFILE* file : File to be finalized
 * @return int : Zero on success, or -1 on failure
 */
int completefile( DATASTREAM stream, STREAMFILE* file ) {
   // check for NULL handle
   if ( file->metahandle == NULL ) {
      LOG( LOG_ERR, "Tgt file is already closed\n" );
      return -1;
   }
   // check for an extended file from a create stream
   if ( (file->ftag.state & FTAG_WRITEABLE)  &&  stream->type == CREATE_STREAM ) {
      LOG( LOG_ERR, "Cannot complete extended file from original create stream\n" );
      return -1;
   }
   // check for a non-finalized file from an edit stream
   if ( (file->ftag.state & FTAG_DATASTATE) != FTAG_FIN  &&  stream->type == EDIT_STREAM ) {
      LOG( LOG_ERR, "Cannot complete non-finalized file from edit stream\n" );
      return -1;
   }
   // shorthand references
   const marfs_ms* ms = &(stream->ns->prepo->metascheme);
   // set ftag to readable and complete state
   file->ftag.state = ( FTAG_COMP | FTAG_READABLE ) | ( file->ftag.state & ~(FTAG_DATASTATE) );
   // truncate the file to an appropriate length
   if ( ms->mdal->ftruncate( file->metahandle, file->ftag.availbytes ) ) {
      LOG( LOG_ERR, "Failed to truncate file %zu to proper size\n", file->ftag.fileno );
      ms->mdal->close( file->metahandle );
      file->metahandle = NULL; // NULL out this handle, so that we never double close()
      return -1;
   }
   // set an updated ftag value
   if ( putftag( stream, file ) ) {
      LOG( LOG_ERR, "Failed to update FTAG on file %zu to complete state\n", file->ftag.fileno );
      ms->mdal->close( file->metahandle );
      file->metahandle = NULL; // NULL out this handle, so that we never double close()
      return -1;
   }
   // set atime/mtime values
   if ( ms->mdal->futimens( file->metahandle, file->times ) ) {
      LOG( LOG_ERR, "Failed to update time values on file %zu\n", file->ftag.fileno );
      ms->mdal->close( file->metahandle );
      file->metahandle = NULL; // NULL out this handle, so that we never double close()
      return -1;
   }
   // close the meta handle
   if ( ms->mdal->close( file->metahandle ) ) {
      LOG( LOG_ERR, "Failed to close meta handle on file %zu\n", file->ftag.fileno );
      file->metahandle = NULL; // NULL out this handle, so that we never double close()
      return -1;
   }
   file->metahandle = NULL; // NULL out this handle, so that we never double close()
   return 0;
}


//   -------------   EXTERNAL FUNCTIONS    -------------


// Failure w/ EBADFD -> no handle for new file, failure of previous files
// Failure w/ any other errno -> no handle for new file, previous stream is still valid
// Success -> handle references new file
int datastream_create( DATASTREAM* stream, const char* path, marfs_position* pos, mode_t mode, const char* ctag ) {
   // check for a NULL path arg
   if ( path == NULL ) {
      LOG( LOG_ERR, "Received a NULL path argument\n" );
      errno = EINVAL;
      return -1;
   }
   // check for a NULL position
   if ( pos == NULL ) {
      LOG( LOG_ERR, "Received a NULL position argument\n" );
      errno = EINVAL;
      return -1;
   }
   // check for NULL stream reference
   if ( stream == NULL ) {
      LOG( LOG_ERR, "Received a NULL stream reference argument\n" );
      errno = EINVAL;
      return -1;
   }
   char closestream = 0;
   DATASTREAM newstream = *stream;
   // check if we've been passed an existing stream
   if ( newstream ) {
      if ( newstream->type != CREATE_STREAM ) {
         LOG( LOG_ERR, "Received non-CREATE stream\n" );
         errno = EINVAL;
         return -1;
      }
      if ( newstream->ns != pos->ns ) {
         LOG( LOG_INFO, "Received datastream has different NS target: \"%s\"\n",
                        newstream->ns->idstr );
         // can't continue with a stream from a previous NS
         closestream = 1;
         newstream = NULL; // so that stream generation logic kicks in later
      }
      else {
         // we're going to continue using the provided stream structure
         size_t curobj = newstream->objno;
         // finalize the current file
         if ( finfile( newstream ) ) {
            LOG( LOG_ERR, "Failed to finalize previous stream file\n" );
            freestream( newstream );
            *stream = NULL; // unsafe to reuse this stream
            errno = EBADFD;
            return -1;
         }
         // progress to the next file
         newstream->curfile++;
         newstream->fileno++;
         // create the new file
         if ( create_new_file( newstream, path, pos->ctxt, mode ) ) {
            LOG( LOG_ERR, "Failed to create new file: \"%s\"\n", path );
            // roll back our stream changes
            newstream->curfile--;
            newstream->fileno--;
            if ( errno = EBADFD ) { errno = ENOMSG; } // avoid using our reserved errno value
            return -1;
         }
         // check for an object transition
         STREAMFILE* newfile = newstream->files + newstream->curfile;
         if ( newfile->ftag.objno != curobj ) {
            size_t newfilepos = newstream->curfile;
            LOG( LOG_INFO, "Stream has transitioned from objno %zu to %zu\n",
                           curobj, newfile->ftag.objno );
            // close our data handle
            char* rtagstr = NULL;
            if ( close_current_obj( stream, &(rtagstr) ) ) {
               freestream( newstream );
               *stream = NULL; // unsafe to reuse this stream
               errno = EBADFD;
               return -1;
            }
            // we need to mark all previous files as complete
            MDAL mdal = newstream->ns->prepo->metascheme.mdal;
            while ( newstream->curfile ) {
               newstream->curfile--;
               STREAMFILE* compfile = newstream->files + newstream->curfile;
               // attach our rebuild tag, if necessary
               if ( rtagstr  &&
                    mdal->fsetxattr( compfile->metahandle, 1, RTAG_NAME, rtagstr, rtagstrlen, 0 ) ) {
                  LOG( LOG_ERR, "Failed to attach rebuild tag to file %zu\n", newstream->curfile );
                  abortflag = 1;
               }
               else if ( completefile( newstream, newstream->files + newstream->curfile ) ) {
                  LOG( LOG_ERR, "Failed to complete file %zu\n", newstream->curfile );
                  abortflag = 1;
               }
            }
            // free our rtag string, if necessary
            if ( rtagstr ) { free( rtagstr ); }
            // shift the new file reference to the front of the list
            newstream->files[0] = newstream->files[newfilepos];
            // check for any errors 
            if ( abortflag ) {
               LOG( LOG_INFO, "Terminating datastream due to previous errors\n" );
               freestream( newstream );
               *stream = NULL; // unsafe to reuse this stream
               errno = EBADFD;
               return -1;
            }
         }
         else {
            // at least need to push out the 'FINALIZED' state of the previous file
            if ( putftag( newstream, newstream->files + ( newstream->curfile - 1 ) ) ) {
               LOG( LOG_ERR, "Failed to push the FINALIZED FTAG for the previous file\n" );
               freestream( newstream );
               *stream = NULL; // unsafe to reuse this stream
               errno = EBADFD;
               return -1;
            }
         }
      }
   }
   if ( newstream == NULL ) { // recheck, so as to catch if the prev stream was abandoned
      // we need to generate a fresh stream structure
      newstream = genstream( CREATE_STREAM, path, pos, mode, ctag );
   }
   // check if we need to close the previous stream
   if ( closestream ) {
      if ( datastream_close( stream ) ) {
         LOG( LOG_ERR, "Failed to close previous datastream\n" );
         *stream = NULL; // don't attempt to close the original stream again
         if ( newstream ) { freestream( newstream ); } // get rid of our new stream as well
         errno = EBADFD;
         return -1;
      }
   }
   // finally, check to ensure we at least have a valid stream to return
   if ( newstream == NULL ) {
      // still NULL means failure of genstream()
      LOG( LOG_ERR, "Failed to generate new stream\n" );
      return -1;
   }
   // update the external stream reference
   *stream = newstream;

   return 0;
}

int datastream_open( DATASTREAM* stream, STREAM_TYPE type, const char* path, marfs_position* pos, const char* ctag ) {
   // check for invalid args
   if ( type != EDIT_STREAM  &&
        type != READ_STREAM ) {
      LOG( LOG_ERR, "Received STREAM_TYPE is unsupported\n" );
      errno = EINVAL;
      return -1;
   }
   // check for a NULL path arg
   if ( path == NULL ) {
      LOG( LOG_ERR, "Received a NULL path argument\n" );
      errno = EINVAL;
      return -1;
   }
   // check for a NULL position
   if ( pos == NULL ) {
      LOG( LOG_ERR, "Received a NULL position argument\n" );
      errno = EINVAL;
      return -1;
   }
   // check for NULL stream reference
   if ( stream == NULL ) {
      LOG( LOG_ERR, "Received a NULL stream reference argument\n" );
      errno = EINVAL;
      return -1;
   }
   char closestream = 0;
   DATASTREAM newstream = *stream;
   // check if we've been passed an existing stream
   if ( newstream ) {
      if ( newstream->type != type ) {
         LOG( LOG_ERR, "Received stream does not match requested STREAM_TYPE\n" );
         errno = EINVAL;
         return -1;
      }
      STREAMFILE* curfile = newstream->files + newstream->curfile;
      if ( newstream->ns != pos->ns ) {
         // stream MUST match in NS
         LOG( LOG_INFO, "Received datastream has different NS target: \"%s\"\n",
                        newstream->ns->idstr );
         closestream = 1;
         newstream = NULL; // so stream generation logic kicks in
      }
      else if ( newstream->type == EDIT_STREAM ) {
         // no point hanging onto any info in between edits
         LOG( LOG_INFO, "Received datastream is irrelevant for new edit stream\n" );
         closestream = 1;
         newstream = NULL; // so stream generation logic kicks in
      }
      else {
         // we're going to continue using the provided READ stream structure
         newstream->curfile++; // progress to the next file
         // attempt to open the new file target
         if ( open_existing_file( newstream, path, pos->ctxt ) ) {
            LOG( LOG_ERR, "Failed to open target file: \"%s\"\n", path );
            newstream->curfile--; // reset back to our old position
            if ( errno = EBADFD ) { errno = ENOMSG; }
            return -1;
         }
         STREAMFILE* newfile = newstream->files + newstream->curfile;
         // check if our old stream targets the same object
         if ( strcmp( curfile->ftag.streamid, newfile->ftag.streamid )  ||
              strcmp( curfile->ftag.ctag, newfile->ftag.ctag )  ||
              curfile->ftag.objno != newfile->ftag.objno ) {
            // data objects differ, so close the old reference
            char* rtagstr = NULL;
            if ( close_current_obj( newstream, &(rtagstr) ) ) {
               LOG( LOG_ERR, "Failed to close old stream data handle\n" );
               freestream( newstream );
               *stream = NULL;
               errno = EBADFD;
               return -1;
            }
            if ( rtagstr != NULL ) {
               // need to attach rebuild tag
               MDAL mdal = stream->ns->prepo->metascheme.mdal;
               if ( mdal->fsetxattr( curfile->metahandle, 1, RTAG_NAME, rtagstr, rtagstrlen, 0 ) ) {
                  LOG( LOG_ERR, "Failed to attach rebuild tag to file %zu\n", curfile->ftag.fileno );
                  freestream( newstream );
                  free( rtagstr );
                  *stream = NULL;
                  errno = EBADFD;
                  return -1;
               }
               free( rtagstr );
            }
         }
         // cleanup our old file reference
         free( curfile->ftag.ctag );
         free( curfile->ftag.streamid );
         // move the new file reference to the first position
         *curfile = *newfile;
         // clean out the old reference location (probably unnecessary)
         newfile->metahandle = NULL;
         newfile->ftag.ctag = NULL;
         newfile->ftag.streamid = NULL;
         newstream->curfile--; // abandon the old reference location
      }
   }
   if ( newstream == NULL ) { // NOTE -- recheck, so as to catch if the prev stream was closed
      // we need to generate a fresh stream structure
      newstream = genstream( type, path, pos, 0, ctag );
   }
   // check if we need to close the previous stream
   if ( closestream ) {
      if ( datastream_release( stream ) ) {
         LOG( LOG_ERR, "Failed to release previous datastream\n" );
         *stream = NULL; // don't attempt to close the original stream again
         if ( newstream ) { freestream( newstream ); } // get rid of our new stream as well
         errno = EBADFD;
         return -1;
      }
   }
   // finally, check to ensure we at least have a valid stream to return
   if ( newstream == NULL ) {
      // still NULL means failure of genstream()
      LOG( LOG_ERR, "Failed to generate new stream\n" );
      return -1;
   }
   // update the external stream reference
   *stream = newstream;

   return 0;
}

int datastream_release( DATASTREAM* stream ) {
   // check for invalid args
   if ( stream == NULL  ||  *stream == NULL ) {
      LOG( LOG_ERR, "Received a NULL stream reference\n" );
      errno = EINVAL;
      return -1;
   }
   DATASTREAM tgtstream = *stream;
   if ( tgtstream->type != EDIT_STREAM  &&
        tgtstream->type != CREATE_STREAM  &&
        tgtstream->type != READ_STREAM ) {
      LOG( LOG_ERR, "Received STREAM_TYPE is unsupported\n" );
      errno = EINVAL;
      return -1;
   }
   // shorthand references
   const marfs_ms* ms = &(tgtstream->ns->prepo->metascheme);
   STREAMFILE* curfile = tgtstream->files + tgtstream->curfile;
   // create/edit streams require extra attention
   if ( tgtstream->type == CREATE_STREAM ) {
      // make sure we're releasing a file that actually got extended
      if ( !(curfile->ftag.state & FTAG_WRITEABLE)  ||  tgtstream->curfile ) {
         LOG( LOG_ERR, "Cannot release non-extended file reference\n" );
         freestream( tgtstream );
         *stream = NULL;
         errno = EINVAL;
         return -1;
      }
      // finalize the current file
      if ( finfile( tgtstream ) ) {
         LOG( LOG_ERR, "Failed to finalize previous stream file\n" );
         freestream( tgtstream );
         *stream = NULL; // unsafe to reuse this stream
         return -1;
      }
   }
   else { // for edit streams...
      // if we've output data, output file recovery info
      if ( tgtstream->datahandle != NULL  &&  putfinfo( tgtstream ) ) {
         LOG( LOG_ERR, "Failed to output file recovery info to current obj\n" );
         freestream( tgtstream );
         *stream = NULL; // unsafe to reuse this stream
         return -1;
      }
   }
   // close our data handle
   char* rtagstr = NULL;
   char abortflag = 0;
   MDAL mdal = tgtstream->ns->prepo->metascheme.mdal;
   if ( close_current_obj( tgtstream, &(rtagstr) ) ) {
      LOG( LOG_ERR, "Close failure for object %zu\n", tgtstream->objno );
      abortflag = 1;
   }
   else if ( rtagstr  &&
        mdal->fsetxattr( curfile->metahandle, 1, RTAG_NAME, rtagstr, rtagstrlen, 0 ) ) {
      LOG( LOG_ERR, "Failed to attach rebuild tag to file %zu\n", curfile->ftag.fileno );
      abortflag = 1;
   }
   // for create streams, update the ftag to a finalizd state
   else if ( tgtstream->type == CREATE_STREAM  &&  putftag( tgtstream, curfile ) ) {
      LOG( LOG_ERR, "Failed to update FTAG of file %zu\n", curfile->ftag.fileno );
      abortflag = 1;
   }
   // if this is a create stream OR if utimens was called, set atime/mtime values
   else if ( (tgtstream->type == CREATE_STREAM  ||  curfile->dotimes)  &&
             ms->mdal->futimens( curfile->metahandle, curfile->times ) ) {
      LOG( LOG_ERR, "Failed to update time values on file %zu\n", curfile->ftag.fileno );
      abortflag = 1;
   }
   // free our rtag string, if necessary
   if ( rtagstr ) { free( rtagstr ); }
   // check for any errors 
   if ( abortflag ) {
      LOG( LOG_INFO, "Terminating datastream due to previous errors\n" );
      freestream( tgtstream );
      *stream = NULL; // unsafe to reuse this stream
      return -1;
   }

   // successfully completed all ops, just need to cleanup refs
   *stream = NULL;
   freestream( tgtstream );
   return 0;
}

int datastream_close( DATASTREAM* stream ) {
   // check for invalid args
   if ( stream == NULL  ||  *stream == NULL ) {
      LOG( LOG_ERR, "Received a NULL stream reference\n" );
      errno = EINVAL;
      return -1;
   }
   DATASTREAM tgtstream = *stream;
   if ( tgtstream->type != EDIT_STREAM  &&
        tgtstream->type != CREATE_STREAM  &&
        tgtstream->type != READ_STREAM ) {
      LOG( LOG_ERR, "Received STREAM_TYPE is unsupported\n" );
      errno = EINVAL;
      return -1;
   }
   // shorthand references
   STREAMFILE* curfile = tgtstream->files + tgtstream->curfile;
   // create/edit streams require extra attention
   if ( tgtstream->type == CREATE_STREAM ) {
      // make sure we're closing a file that did not get extended
      if ( curfile->ftag.state & FTAG_WRITEABLE ) {
         LOG( LOG_ERR, "Cannot close extended file reference\n" );
         freestream( tgtstream );
         *stream = NULL;
         errno = EINVAL;
         return -1;
      }
      // finalize the current file
      if ( finfile( tgtstream ) ) {
         LOG( LOG_ERR, "Failed to finalize previous stream file\n" );
         freestream( tgtstream );
         *stream = NULL; // unsafe to reuse this stream
         return -1;
      }
   }
   else if ( tgtstream->type == EDIT_STREAM ) {
      // make sure we're closing a writeable and finalized file
      if ( !(curfile->ftag.state & FTAG_WRITEABLE)  ||
           (curfile->ftag.state & FTAG_DATASTATE) != FTAG_FIN ) {
         LOG( LOG_ERR, "Cannot close non-extended, non-finalized file reference\n" );
         freestream( tgtstream );
         *stream = NULL;
         errno = EINVAL;
         return -1;
      }
      // if we've output data, output file recovery info
      if ( tgtstream->datahandle != NULL  &&  putfinfo( tgtstream ) ) {
         LOG( LOG_ERR, "Failed to output file recovery info to current obj\n" );
         freestream( tgtstream );
         *stream = NULL; // unsafe to reuse this stream
         return -1;
      }
   }
   // close our data handle
   char* rtagstr = NULL;
   char abortflag = 0;
   if ( close_current_obj( tgtstream, &(rtagstr) ) ) {
      LOG( LOG_ERR, "Failure during close of object %zu\n", tgtstream->objno );
      freestream( tgtstream );
      *stream = NULL; // unsafe to reuse this stream
      return -1;
   }
   // cleanup all open files
   MDAL mdal = tgtstream->ns->prepo->metascheme.mdal;
   while ( 1 ) {
      STREAMFILE* compfile = tgtstream->files + tgtstream->curfile;
      // attach our rebuild tag, if necessary
      if ( rtagstr  &&
           mdal->fsetxattr( compfile->metahandle, 1, RTAG_NAME, rtagstr, rtagstrlen, 0 ) ) {
         LOG( LOG_ERR, "Failed to attach rebuild tag to file %zu\n", compfile->ftag.fileno );
         abortflag = 1;
      }
      // for non-read streams, complete the current file
      else if ( tgtstream->type != READ_STREAM  &&  completefile( tgtstream, compfile ) ) {
         LOG( LOG_ERR, "Failed to complete file %zu\n", compfile->ftag.fileno );
         abortflag = 1;
      }
      if ( tgtstream->curfile == 0 ) { break; }
      tgtstream->curfile--;
   }
   // free our rtag string, if necessary
   if ( rtagstr ) { free( rtagstr ); }
   // check for any errors 
   if ( abortflag ) {
      LOG( LOG_INFO, "Terminating datastream due to previous errors\n" );
      freestream( tgtstream );
      *stream = NULL; // unsafe to reuse this stream
      return -1;
   }

   // successfully completed all ops, just need to cleanup refs
   *stream = NULL;
   freestream( tgtstream );
   return 0;
}

ssize_t datastream_read( DATASTREAM stream, void* buf, size_t count ) {
   // check for invalid args
   if ( stream == NULL ) {
      LOG( LOG_ERR, "Received a NULL stream reference\n" );
      errno = EINVAL;
      return -1;
   }
   if ( stream->type != READ_STREAM ) {
      LOG( LOG_ERR, "Provided stream does not support reading\n" );
      errno = EINVAL;
      return -1;
   }
   // identify current position info 
   MDAL mdal = stream->ns->prepo->metascheme.mdal;
   STREAMFILE* curfile = stream->files + stream->curfile;
   size_t curoff;
   size_t remaining;
   size_t maxobjdata;
   if ( gettargets( stream, 0, SEEK_CUR, NULL, &(curoff), &(remaining), &(maxobjdata) ) ) {
      LOG( LOG_ERR, "Failed to identify position vals of file %zu\n", curfile->ftag.fileno );
      return -1;
   }

   // reduce read request to account for file limits
   size_t zerotailbytes = 0;
   if ( count > stream->finfo.size ) {
      LOG( LOG_INFO, "Read request exceeds file bounds, resizing to %zu bytes\n", stream->finfo.size );
      count = stream->finfo.size;
   }
   if ( count > remaining ) {
      zerotailbytes = count - remaining;
      count = remaining;
      LOG( LOG_INFO, "Read request exceeds data content, appending %zu tailing zero bytes\n",
                     zerotailbytes );
   }

   // retrieve data until we no longer can
   size_t readbytes = 0;
   while ( count ) {
      // calculate how much data we can read from the current data object
      size_t toread = maxobjdata - ( stream->offset - stream->recoveryheaderlen );
      if ( toread == 0 ) {
         // close the previous data handle
         char* rtagstr = NULL;
         size_t rtagstrlen = 0;
         if ( close_current_obj( stream, &(rtagstr), &(rtagstrlen) ) ) {
            LOG( LOG_WARNING, "Failed to close previous data object\n" );
         }
         if ( rtagstr != NULL ) {
            if ( mdal->fsetxattr( curfile->metahandle, 1, RTAG_NAME, rtagstr, rtagstrlen, 0 ) ) {
               LOG( LOG_WARNING, "Failed to attach rebuild tag to file %zu\n", curfile->ftag.fileno );
            }
            free( rtagstr );
         }
         // progress to the next data object
         stream->objno++;
         stream->offset = stream->recoveryheaderlen;
         toread = maxobjdata;
         LOG( LOG_INFO, "Progressing read into object %zu\n", stream->objno );
      }
      if ( toread > count ) { toread = count; }
      // open the current data object, if necessary
      if ( stream->datahandle == NULL ) {
         if ( open_current_obj( stream ) ) {
            LOG( LOG_ERR, "Failed to open data object %zu\n", stream->objno );
            return (readbytes) ? readbytes : -1;
         }
      }
      // perform the actual read op
      ssize_t readres = ne_read( stream->datahandle, buf, toread );
      if ( readres <= 0 ) {
         LOG( LOG_ERR, "Read failure in object %zu at offset %zu\n",
                       stream->objno, stream->offset );
         return (readbytes) ? readbytes : -1;
      }
      // adjust all offsets and byte counts
      buf += readres;
      count -= readres;
      readbytes += readres;
      stream->offset += readres;
   }

   // append zero bytes to account for file truncated beyond data length
   if ( zerotailbytes ) {
      bzero( buf, zerotailbytes );
      readbytes += zerotailbytes;
   }
   // TODO : need to track this offset!  Otherwise we'll repeat for reads near EOF

   return readbytes;
}

ssize_t datastream_write( DATASTREAM stream, const void* buf, size_t count ) {
   // check for invalid args
   if ( stream == NULL ) {
      LOG( LOG_ERR, "Received a NULL stream reference\n" );
      errno = EINVAL;
      return -1;
   }
   if ( stream->type != CREATE_STREAM  &&  stream->type != EDIT_STREAM ) {
      LOG( LOG_ERR, "Provided stream does not support writing\n" );
      errno = EINVAL;
      return -1;
   }
}

int datastream_setrecoverypath( DATASTREAM stream, const char* recovpath ) {
   // check for invalid args
   if ( stream == NULL ) {
      LOG( LOG_ERR, "Received a NULL stream reference\n" );
      errno = EINVAL;
      return -1;
   }
   if ( recovpath == NULL ) {
      LOG( LOG_ERR, "Received a NULL recovpath string\n" );
      errno = EINVAL;
      return -1;
   }
   // check if this stream is of an appropriate type
   if ( stream->type != CREATE_STREAM  &&  stream->type != EDIT_STREAM ) {
      LOG( LOG_ERR, "Received stream type is not supported\n" );
      errno = EINVAL;
      return -1;
   }
   // perform stream->type specific check
   STREAMFILE* curfile = stream->files + stream->curfile;
   if ( stream->type == CREATE_STREAM ) {
      // cannot adjust recovery path after we've started laying out data ( write or extend )
      if ( curfile->ftag.bytes ) {
         LOG( LOG_ERR, "Received CREATE stream already has associated data\n" );
         errno = EINVAL;
         return -1;
      }
   }
   // adjust the finfo path
   char* oldpath = stream->finfo.path;
   stream->finfo.path = strdup( recovpath );
   if ( stream->finfo.path == NULL ) {
      LOG( LOG_ERR, "Failed to duplicate new recovery path: \"%s\"\n", recovpath );
      stream->finfo.path = oldpath;
      return -1;
   }
   // identify the new finfo strlen
   size_t newstrlen = recovery_finfotostr( &(stream->finfo), NULL, 0 );
   if ( newstrlen < 1 ) {
      LOG( LOG_ERR, "Failed to produce recovery string with new recovery path\n" );
      free( stream->finfo.path );
      stream->finfo.path = oldpath;
      return -1;
   }
   // perform stream->type specific actions
   if ( stream->type == EDIT_STREAM ) {
      // ensure the new path won't exceed the file's existing recovery bytes setting
      if ( newstrlen > curfile->ftag.recoverybytes ) {
         LOG( LOG_ERR, "New recovery path results in excessive recovery string lenght of %zu bytes\n", newstrlen );
         free( stream->finfo.path );
         stream->finfo.path = oldpath;
         errno = ENAMETOOLONG; // probably the best way to describe this
         return -1;
      }
   }
   else { // implies type == CREATE
      // for create streams, we actually need to update our FTAG
      size_t oldrecovbytes = curfile->ftag.recoverybytes;
      curfile->ftag.recoverybytes = newstrlen;
      if ( putftag( stream, curfile ) ) {
         LOG( LOG_ERR, "Failed to update FTAG value to reflect new recovery length\n" );
         curfile->ftag.recoverybytes = oldrecovbytes;
         free( stream->finfo.path );
         stream->finfo.path = oldpath;
         return -1;
      }
   }

   // path change has succeeded; cleanup old string
   free( oldpath );
   return 0;
}

#if 0
DATASTREAM datastream_creat( const char* path, mode_t mode, const marfs_ns* ns, const marfs_config* config ) {
   // create some shorthand references
   marfs_ms* ms = &(ns->prepo->metascheme);
   marfs_ds* ds = &(ns->prepo->datascheme);

   // allocate the new datastream and check for success
   DATASTREAM stream = genstream( CREATE_STREAM, ns, config );
   if ( stream == NULL ) {
      LOG( LOG_ERR, "Failed to allocate a new datastream\n" );
      return NULL;
   }

   // identify a reference path
   char* rpath = genrpath( stream, &(stream->files[0]) );
   if ( rpath == NULL ) {
      LOG( LOG_ERR, "Failed to identify reference path for stream\n" );
      freestream( stream );
      return NULL;
   }

   // create the reference file, ensuring we don't collide with an existing reference
   stream->files[0].metahandle = ms->mdal->openref( stream->mdalctxt, rpath, O_CREAT | O_EXCL | O_WRONLY, mode );
   if ( stream->files[0].metahandle == NULL ) {
      LOG( LOG_ERR, "Failed to create reference meta file: \"%s\"\n", rpath );
      if ( errno = EEXIST ) { errno = EBUSY; } // a BUSY error is more indicative of the real problem
      free( rpath );
      freestream( stream );
      return NULL;
   }
   // still need to hang onto 'rpath', in case something goes wrong later

   // identify file recovery info
   if ( genrecoveryinfo( &(stream->finfo), &(stream->files[0]), path ) ) {
      LOG( LOG_ERR, "Failed to populate recovery info for file: \"%s\"\n", path );
      errno = EBADFD; // a bit vague, but hopefully at least not misleading
      ms->mdal->unlinkref( stream->mdalctxt, rpath );
      free( rpath );
      freestream( stream );
      return NULL;
   }
   stream->files[0].ftag.recoverybytes = recovery_finfotostr( &(stream->finfo), NULL, 0 );
   if ( stream->files[0].ftag.recoverybytes == 0 ) {
      LOG( LOG_ERR, "Failed to calculate recovery info size for created file\n" );
      errno = EBADFD; // a bit vague, but hopefully at least not misleading
      ms->mdal->unlinkref( stream->mdalctxt, rpath );
      free( rpath );
      freestream( stream );
      return NULL;
   }
   RECOVERY_HEADER header = 
   {
      .majorversion = RECOVERY_CURRENT_MAJORVERSION,
      .minorversion = RECOVERY_CURRENT_MINORVERSION,
      .ctag = stream->files[0].ftag.ctag,
      .streamid = stream->files[0].ftag.streamid
   };
   stream->recoveryheaderlen = recovery_headertostr( &(header), NULL, 0 );

   // verify that our object can fit recovery info, plus at least *some* data
   if ( (stream->recoveryheaderlen + stream->files[0].ftag.recoverybytes) >= stream->files[0].ftag.objsize ) {
      LOG( LOG_ERR, "Object size of %zu is insufficient for recovery header of %zu plus file recovery info of %zu\n", stream->files[0].ftag.objsize, stream->recoveryheaderlen, stream->files[0].ftag.recoverybytes );
      errno = EBADFD; // a bit vague, but hopefully at least not misleading
      ms->mdal->unlinkref( stream->mdalctxt, rpath );
      free( rpath );
      freestream( stream );
      return NULL;
   } 

   // attach our initial MARFS ftag value
   if ( putftag( stream, &(stream->files[0]) ) ) {
      LOG( LOG_ERR, "Failed to update FTAG value on target file\n" );
      ms->mdal->unlinkref( stream->mdalctxt, rpath );
      free( rpath );
      freestream( stream );
      return NULL;
   }

   // link the file into the user namespace
   if ( linkfile( stream, rpath, path ) ) {
      LOG( LOG_ERR, "Failed to link reference file to target user path: \"%s\"\n", path );
      ms->mdal->unlinkref( stream->mdalctxt, rpath );
      free( rpath );
      freestream( stream );
      return NULL;
   }

   // all done
   free( rpath );
   return stream;
}

DATASTREAM datastream_edit( const char* path, const marfs_ns* ns, const marfs_config* config ) {
   // create some shorthand references
   marfs_ms* ms = &(ns->prepo->metascheme);
   marfs_ds* ds = &(ns->prepo->datascheme);

   // allocate the new datastream and check for success
   DATASTREAM stream = genstream( EDIT_STREAM, ns, config );
   if ( stream == NULL ) {
      LOG( LOG_ERR, "Failed to allocate a new datastream\n" );
      return NULL;
   }

   // open a metadata handle for the target file
   stream->files[0].metahandle = ms->mdal->open( stream->mdalctxt, path, O_RDWR );
   if( stream->files[0].metahandle == NULL ) {
      LOG( LOG_ERR, "Failed to open metadata handle for target path: \"%s\"\n", path );
      freestream( stream );
      return NULL;
   }

   // retrieve ftag values for the existing file
   if ( getftag( stream, &(stream->files[0]) ) ) {
      LOG( LOG_ERR, "Failed to retrieve ftag value for target file: \"%s\"\n", path );
      freestream( stream );
      return NULL;
   }

   // set stream offsets to align with the start of the file
   stream->objno = stream->files[0].ftag.objno;
   stream->offset = stream->files[0].ftag.offset;

   // verify that this file is writable via a new handle, or has a completed data set
   if ( (stream->files[0].ftag.state & FTAG_WRITABLE) == 0  &&
        (stream->files[0].ftag.state & FTAG_DATASTATE) != FTAG_COMP ) {
      LOG( LOG_ERR, "Attempting to edit file before original handle has been released\n" );
      freestream( stream );
      return NULL;
   }

   // identify file attributes, for recovery info
   if ( genrecoveryinfo( &(stream->finfo), &(stream->files[0]), path ) ) {
      LOG( LOG_ERR, "Failed to populate recovery info for file: \"%s\"\n", path );
      freestream( stream );
      return NULL;
   }
   RECOVERY_HEADER header = 
   {
      .majorversion = RECOVERY_CURRENT_MAJORVERSION,
      .minorversion = RECOVERY_CURRENT_MINORVERSION,
      .ctag = stream->files[0].ftag.ctag,
      .streamid = stream->files[0].ftag.streamid
   };
   stream->recoveryheaderlen = recovery_headertostr( &(header), NULL, 0 );

   return stream;
}

DATASTREAM datastream_wcont( DATASTREAM stream, const char* path, mode_t mode ) {
   // verify we haven't just been given a NULL stream
   if ( stream == NULL ) {
      LOG( LOG_ERR, "Received a NULL stream value\n" );
      errno = EINVAL;
      return NULL;
   }

   // ensure our stream handle is in an appropriate mode
   if ( stream->type == READ_STREAM ) {
      LOG( LOG_ERR, "Cannot continue a READ datastream\n" );
      errno = EINVAL;
      return NULL;
   }

   // create some shorthand references
   marfs_ms* ms = &(ns->prepo->metascheme);
   marfs_ds* ds = &(ns->prepo->datascheme);
   STREAMFILE curfile = stream->files + (stream->filecount - 1);

   // if this is an EDIT stream, ensure that this handle can finalize the current file
   if ( stream->type == EDIT_STREAM  &&  curfile ) {
      FTAG_STATE curfilestate = curfile->ftag.state;
      if ( (curfilestate & FTAG_WRITABLE) == 0 ) {
         LOG( LOG_ERR, "Current file cannot be modified by the given handle\n" );
         errno = EINVAL;
         return NULL;
      }
      if ( (curfilestate & FTAG_DATASTATE) != FTAG_FIN ) {
         LOG( LOG_ERR, "Current file does not have a finalized data size\n" );
         errno = EINVAL;
         return NULL;
      }
   }

   // Note -- This function has two distinct phases: creation of a new file, and completion of the previous.
   //         As completion of the previous file may involve irreversible modification of the stream, it is 
   //         safest to begin with creation of the new file.  That way, if file creation proves impossible, 
   //         for whatever reason, we still have the opportunity to roll-back the entire op.

   // NEW FILE CREATION

   // construct a reference struct for our new file
   struct streamfile_struct newfile = 
   {
      .metahandle = NULL,
      .ftag = curfile->ftag,  // copy our current, working ftag
      .times[0].tv_sec = 0,
      .times[0].tv_nsec = 0,
      .times[1].tv_sec = 0,
      .times[1].tv_nsec = 0
   };
   // update our ftag values to reflect the new file, rather than the old
   newfile.ftag.fileno++;
   if ( stream->type == CREATE_STREAM ) {
      // use the current stream object, for now
      newfile.ftag.objno = stream->objno;
      // use the current stream offset, accounting for the recovery info we still need to store
      newfile.ftag.offset = stream->offset + newfile.ftag.recoverybytes;
   }
   else {
   }
   newfile.ftag.bytes = 0;
   newfile.ftag.availbytes = 0;
   newfile.ftag.directbytes = 0;
   newfile.ftag.state = FTAG_INIT;

   // establish a reference path for the new file
   char* rpath = genrpath( stream, &(newfile) );
   if ( rpath == NULL ) {
      LOG( LOG_ERR, "Failed to identify reference path for stream\n" );
      if ( errno == EBADFD ) { errno = ENOMSG; } // don't allow our reserved EBADFD value
      return NULL;
   }

   // create the reference file, ensuring we don't collide with an existing reference
   newfile.metahandle = ms->mdal->openref( stream->mdalctxt, rpath, O_CREAT | O_EXCL | O_WRONLY, mode );
   if ( newfile.metahandle == NULL ) {
      LOG( LOG_ERR, "Failed to create reference meta file: \"%s\"\n", rpath );
      if ( errno = EEXIST ) { errno = EBUSY; } // a BUSY error is more indicative of the real problem
      else if ( errno == EBADFD ) { errno = ENOMSG; } // don't allow our reserved EBADFD value
      free( rpath );
      return NULL;
   }
   // still need to hang onto 'rpath', in case something goes wrong later

   // identify file recovery info
   RECOVERY_FINFO newfinfo;
   if ( genrecoveryinfo( &(newfinfo), &(newfile), path ) ) {
      LOG( LOG_ERR, "Failed to populate recovery info for file: \"%s\"\n", path );
      ms->mdal->unlinkref( stream->mdalctxt, rpath );
      free( rpath );
      if ( errno == EBADFD ) { errno = ENOMSG; } // don't allow our reserved EBADFD value
      return NULL;
   }
   newfile.ftag.recoverybytes = recovery_finfotostr( &(newfinfo), NULL, 0 );
   if ( newfile.ftag.recoverybytes == 0 ) {
      LOG( LOG_ERR, "Failed to calculate recovery info size for created file\n" );
      ms->mdal->unlinkref( stream->mdalctxt, rpath );
      free( rpath );
      if ( errno == EBADFD ) { errno = ENOMSG; } // don't allow our reserved EBADFD value
      return NULL;
   }

   // ensure the recovery info size is compatible with the current object size
   if ( (stream->recoveryheaderlen + newfile.ftag.recoverybytes) >= newfile.ftag.objsize ) {
      LOG( LOG_ERR, "Recovery info size of new file is incompatible with current object size\n" );
      ms->mdal->unlinkref( stream->mdalctxt, rpath );
      free( rpath );
      errno = ENAMETOOLONG; // as the prev file succeeded, it must be a longer name issue
      return NULL;
   }

   // identify if we must move on to a new data object
   // Note -- we *must* skip to the next object if this is an EDIT stream
   if ( stream->type != CREATE_STREAM  ||
        stream->filecount >= newfile.ftag.objfiles  ||
        ( newfile.ftag.offset + newfile.ftag.recoverybytes) >= newfile.ftag.objsize ) {
      // we'll need to start outputting this file to a new data object
      newfile.ftag.objno++;
      newfile.ftag.offset = stream->recoveryheaderlen;  // data will begin after the recov object header
   }

   // attach updated ftag value to the new file
   if ( putftag( stream, &(newfile) ) ) {
      LOG( LOG_ERR, "Failed to initialize FTAG value on target file\n" );
      ms->mdal->unlinkref( stream->mdalctxt, rpath );
      free( rpath );
      if ( errno == EBADFD ) { errno = ENOMSG; } // don't allow our reserved EBADFD value
      return NULL;
   }

   // link the new file into the user namespace
   if ( linkfile( stream, rpath, path ) ) {
      LOG( LOG_ERR, "Failed to link reference file to target user path: \"%s\"\n", path );
      ms->mdal->unlinkref( stream->mdalctxt, rpath );
      free( rpath );
      if ( errno == EBADFD ) { errno = ENOMSG; } // don't allow our reserved EBADFD value
      return NULL;
   }
   free( rpath ); // finally done with rpath

   // PREVIOUS FILE COMPLETION

   // Note -- We are now beyond the point of no return.
   //         Any errors now require us to 'fail forward', progressing the stream regardless.
   char errorflag = 0;  // just report errors, don't return

   // if this is a create stream, and either...
   //    we ARE already writing to an active data object
   //  or
   //    we ARE NOT relying on the metadata FS for data storage
   // ...then we must store recovery info for the previous file
   if ( stream->type == CREATE_STREAM  &&
         ( stream->datahandle != NULL  ||  ms->directchunks == 0 ) ) {

      stream->finfo.eof = 1; // this is the end of the previous file

      // if we haven't ever opened the current data object, we now need to
      if ( stream->datahandle == NULL ) {
         if ( opencurrentobj( stream ) ) {
            LOG( LOG_ERR, "Failed to create initial data object\n" );
            errorflag = 1;
         }
      }
      if ( errorflag == 0 ) {
         // output file recovery info
         errorflag = dumpfinfo( stream );
      }
   }

   // mark the current file as FINALIZED
   curfile->ftag.state = FTAG_FIN | ( curfile->ftag.state & ~(FTAG_DATASTATE) );

   // check if we need to finalize our current files
   if ( newfile.ftag.objno != stream->objno  ||  errorflag != 0 ) {
      char globalerror = errorflag; // failure to write recov info is unrecoverable
      // close the data object, if we have one
      if ( stream->datahandle != NULL  &&  ne_close( stream->datahandle ) ) {
         LOG( LOG_ERR, "Failed to sync current data object\n" );
         errorflag = 1;
         globalerror = 1; // don't indicate success for ANY files
      }
      stream->datahandle == NULL;
      // update all currently referenced files
      for( ; stream->filecount > 0; stream->filecount-- ) {
         STREAMFILE tmpfile = stream->files + ( stream->filecount - 1);
         if ( globalerror == 0 ) {
            // set all final values on this file
            if ( finfile( stream, tmpfile ) ) {
               LOG( LOG_ERR, "Failed to finalize file %zu\n", stream->filecount - 1 );
               errorflag = 1;
            }
         }
         // close the metadata handle for the file
         if ( ms->mdal->close( tmpfile->metahandle ) ) {
            // worth warning about, but probably not a serious issue
            LOG( LOG_ERROR, "Failed to close metahandle for file %zu\n", stream->filecount );
            errorflag = 1;
         }
      }
   }
   else {
      // at least update the ftag value of our current file
      if ( putftag( stream, curfile ) ) {
         LOG( LOG_ERR, "Failed to update FTAG state of previous file\n" );
         errorflag = 1;
      }
   }

   // update our stream values to match new ones
   stream->objno = newfile.ftag.objno;
   stream->offset = newfile.ftag.offset;
   if ( stream->finfo.path ) { free( stream->finfo.path ); } // free old recov path
   stream->finfo = newfinfo;
   if ( stream->filealloc < stream->filecount + 1 ) {
      // allocate more file references, if necessary
      stream->filealloc = allocfiles( &(stream->files), stream->filecount, ds->objfiles );
      if ( stream->filealloc == 0 ) {
         LOG( LOG_ERR, "Failed to allocate space for additional file reference\n" );
         // can't store new file info, so it must be freed
         ms->mdal->close( newfile.metahandle );
         if ( newfile.ftag.ctag ) { free( newfile.ftag.ctag ); }
         if ( newfile.ftag.streamid ) { free( newfile.ftag.streamid); }
         stream->filecount = 0; // ensure we never attempt to reference other files again
         errorflag = 1;
      }
   }
   if ( stream->filealloc ) {
      // so long as we didn't lose this allocate, store our new file reference
      stream->files[stream->filecount] = newfile;
      stream->filecount++;
   }

   // catch any errors with the stream itself
   if ( errorflag ) {
      stream->type = ERROR_STREAM; // render the stream unusable
      errno = EBADFD; // produce a special errno value
      return NULL;
   }

   // now that it references a new file, this can be considered a CREAT stream
   stream->type = CREAT_STREAM;

   // otherwise, return the same stream reference
   return stream;
}


DATASTREAM datastream_init( const char* path, const marfs_ns* ns, const marfs_config* config ) {
   // create some shorthand references
   marfs_ms* ms = &(ns->prepo->metascheme);
   marfs_ds* ds = &(ns->prepo->datascheme);

   // allocate the new datastream and check for success
   DATASTREAM stream = genstream( READ_STREAM, ns, config );
   if ( stream == NULL ) {
      LOG( LOG_ERR, "Failed to allocate a new datastream\n" );
      return NULL;
   }

   // open a metadata handle for the target file
   stream->files[0].metahandle = ms->mdal->open( stream->mdalctxt, path, O_RDONLY );
   if( stream->files[0].metahandle == NULL ) {
      LOG( LOG_ERR, "Failed to open metadata handle for target path: \"%s\"\n", path );
      freestream( stream );
      return NULL;
   }

   // retrieve ftag values for the existing file
   if ( getftag( stream, &(stream->files[0]) ) ) {
      LOG( LOG_ERR, "Failed to retrieve ftag value for target file: \"%s\"\n", path );
      freestream( stream );
      return NULL;
   }

   // set stream offsets to align with the start of the file
   stream->objno = stream->files[0].ftag.objno;
   stream->offset = stream->files[0].ftag.offset;

   // verify that this file is readable via a new handle
   if ( ( stream->files[0].ftag.state & FTAG_READABLE ) == 0 ) {
      LOG( LOG_ERR, "Cannot read from a file before it has been completed\n" );
      freestream( stream );
      return NULL;
   }

   // identify per-object recovery header info
   RECOVERY_HEADER header = 
   {
      .majorversion = RECOVERY_CURRENT_MAJORVERSION,
      .minorversion = RECOVERY_CURRENT_MINORVERSION,
      .ctag = stream->files[0].ftag.ctag,
      .streamid = stream->files[0].ftag.streamid
   };
   stream->recoveryheaderlen = recovery_headertostr( &(header), NULL, 0 );

   return stream;
}


DATASTREAM datastream_rcont( DATASTREAM stream, const char* path ) {
   // check for NULL stream or improper stream type
   if ( stream == NULL ) {
      LOG( LOG_ERR, "Received a NULL stream reference\n" );
      errno = EINVAL;
      return NULL;
   }
   if ( stream->type != READ_STREAM ) {
      LOG( LOG_ERR, "Recieved a non-READ stream reference\n" );
      errno = EINVAL;
      return NULL;
   }

   // create some shorthand references
   marfs_ms* ms = &(stream->ns->prepo->metascheme);
   marfs_ds* ds = &(stream->ns->prepo->datascheme);

   // open a metadata handle for the target file
   struct streamfile_struct oldfile = stream->files[0]; // track the old file reference
   stream->files[0].metahandle = ms->mdal->open( stream->mdalctxt, path, O_RDONLY );
   if( stream->files[0].metahandle == NULL ) {
      LOG( LOG_ERR, "Failed to open metadata handle for target path: \"%s\"\n", path );
      stream->files[0] = oldfile; // restore old references
      return NULL;
   }

   // retrieve ftag values for the existing file
   if ( getftag( stream, &(stream->files[0]) ) ) {
      LOG( LOG_ERR, "Failed to retrieve ftag value for target file: \"%s\"\n", path );
      stream->files[0] = oldfile; // restore old references
      return NULL;
   }

   // verify that this file is writable via a new handle
   if ( ( stream->files[0].ftag.state & FTAG_READABLE ) == 0 ) {
      LOG( LOG_ERR, "Attempting to edit file before original handle has been released\n" );
      stream->files[0] = oldfile; // restore old references
      return NULL;
   }

   // if we have an data handle, we need to check if it is still valid
   if ( stream->datahandle ) {
      char resethandle = 0;
      // check stream identifiers
      if ( strcmp( stream->files[0].ftag.streamid, oldfile.ftag.streamid )  ||
           strcmp( stream->files[0].ftag.ctag, oldfile.ftag.ctag ) ) {
         resethandle = 1;
      }
      // check object number
      if ( stream->objno != oldfile.ftag.objno ) {
         resethandle = 1;
      }
      // destroy the object reference, if it is outdated
      if ( resethandle ) {
         if ( ne_close( stream->datahandle ) ) {
            LOG( LOG_WARNING, "Failed to close previous data handle\n" );
         }
         stream->datahandle = NULL;
      }
   }

   // free old file reference info
   free( oldfile.ftag.streamid );
   if ( oldfile.ftag.ctag ) { free( oldfile.ftag.ctag ); }

   // set stream offsets to align with the start of the file
   stream->objno = stream->files[0].ftag.objno;
   stream->offset = stream->files[0].ftag.offset;


   // identify per-object recovery header info
   RECOVERY_HEADER header = 
   {
      .majorversion = RECOVERY_CURRENT_MAJORVERSION,
      .minorversion = RECOVERY_CURRENT_MINORVERSION,
      .ctag = stream->files[0].ftag.ctag,
      .streamid = stream->files[0].ftag.streamid
   };
   stream->recoveryheaderlen = recovery_headertostr( &(header), NULL, 0 );

   return stream;
}


int datastream_setrecoverypath( DATASTREAM stream, const char* recovpath ) {
   // check for NULL stream or improper stream type
   if ( stream == NULL ) {
      LOG( LOG_ERR, "Received a NULL stream reference\n" );
      errno = EINVAL;
      return -1;
   }
   if ( stream->type != CREAT_STREAM  &&  stream->type != EDIT_STREAM ) {
      LOG( LOG_ERR, "Received a non-CREAT/EDIT stream reference\n" );
      errno = EINVAL;
      return -1;
   }
   // make sure this stream actually has a current file reference
   if ( !(stream->filecount) ) {
      LOG( LOG_ERR, "Current stream is not referencing an active file\n" );
      errno = EINVAL;
      return -1;
   }

   // we don't allow an edit of the recovery path after data output has begun
   if( stream->datahandle ) {
      LOG( LOG_ERR, "Recovery path cannot be altered after data output has begun\n" );
      errno = EINVAL;
      return -1;
   }

   // update the recovery path value
   char* oldpath = stream->finfo.path;
   stream->finfo.path = strdup( recovpath );
   if ( stream->finfo.path == NULL ) {
      LOG( LOG_ERR, "Failed to update recovery path value\n" );
      stream->finfo.path = oldpath;
      return -1;
   }

   // identify the new recovery info size
   size_t recovsize = recovery_finfotostr( &(stream->finfo), NULL, 0 );
   if ( recovsize == 0 ) {
      LOG( LOG_ERR, "Failed to calculate size of new recovery info\n" );
      free( stream->finfo.path );
      stream->finfo.path = oldpath;
      return -1;
   }

   // ensure the recovery info size is compatible with the current object size
   if ( (stream->recoveryheaderlen + recovsize) >= stream->files[stream->filecount - 1].ftag.objsize ) {
      LOG( LOG_ERR, "Resulting recovery info size is incompatible with current object size\n" );
      free( stream->finfo.path );
      stream->finfo.path = oldpath;
      return -1;
   }

   // ensure that the new recovery info size does not contradict the existing value
   if ( stream->files[stream->filecount - 1].ftag.recoverybytes != recovsize ) {
      // for EDIT streams, this is a hard error
      if ( stream->type == EDIT_STREAM ) {
         LOG( LOG_ERR, "Resulting recovery info size contradicts FTAG value\n" );
         free( stream->finfo.path );
         stream->finfo.path = oldpath;
         errno = EINVAL;
         return -1;
      }
      // for CREAT streams, we will actually adjust the FTAG value
      else {
         size_t origrecovbytes = stream->files[stream->filecount - 1].ftag.recoverybytes;
         stream->files[stream->filecount - 1].ftag.recoverybytes = recovsize;
         if ( putftag( stream, &(stream->files[stream->filecount - 1]) ) ) {
            LOG( LOG_ERR, "Failed to update FTAG to reflect new recovery info size\n" );
            stream->files[stream->filecount - 1].ftag.recoverybytes = origrecovbytes;
            free( stream->finfo.path );
            stream->finfo.path = oldpath;
            return -1;
         }
      }
   }

   // all done
   free( oldpath );
   return 0;
}


int datastream_extend( DATASTREAM stream, off_t length ) {
   // check for NULL stream or improper stream type
   if ( stream == NULL ) {
      LOG( LOG_ERR, "Received a NULL stream reference\n" );
      errno = EINVAL;
      return NULL;
   }
   if ( stream->type != CREAT_STREAM ) {
      // only CREAT streams can modify the size of a file
      LOG( LOG_ERR, "Recieved a non-CREAT stream reference\n" );
      errno = EINVAL;
      return NULL;
   }
   // make sure this stream actually has a current file reference
   if ( !(stream->filecount) ) {
      LOG( LOG_ERR, "Current stream is not referencing an active file\n" );
      errno = EINVAL;
      return -1;
   }

   // create some shorthand references
   marfs_ms* ms = &(stream->ns->prepo->metascheme);
   marfs_ds* ds = &(stream->ns->prepo->datascheme);
   STREAMFILE curfile = stream->files + (stream->filecount - 1);
   // preserve old offset values
   FTAG curtag = curfile->ftag;
   size_t oldoff = stream->offset;
   size_t oldobj = stream->objno;

   // check if our current offset / byte count supports extension
   if ( curfile->bytes == 0 ) {
      // this file currently has no content, and can safely be extended
      if ( curfile->offset != stream->recoveryheaderlen ) {
         // we need to move this file to a new object
         curfile->objno++;
         curfile->offset = stream->recoveryheaderlen;
         stream->objno = curfile->objno;
         stream->offset = 0;
      }
   }
   // Note -- if stream->offset is zero, we are already sitting right on an object boundary
   else if ( stream->offset != 0 ) {
      // if we have already written to this file, and don't align with an obj boundary, we can't extend
      LOG( LOG_ERR, "Cannot extend a file whose existing data does not align with object boundaries\n" );
      errno = EINVAL;
      return NULL;
   }

   // attempt to close the current object reference, if we have one
   char errorflag = 0;
   if ( stream->filecount > 1 ) {
      char globalerror = 0;
      // close the data object, if we have one
      if ( stream->datahandle && ne_close( stream->datahandle ) ) {
         LOG( LOG_ERR, "Failed to sync current data object\n" );
         errorflag = 1;
         globalerror = 1; // don't indicate success for ANY files
      }
      stream->datahandle == NULL;
      // update all currently referenced files
      int fnum = stream->filecount - 1;
      for( ; fnum > 0; fnum-- ) {
         STREAMFILE tmpfile = stream->files + ( fnum - 1);
         if ( globalerror == 0 ) {
            // set all final values on this file
            if ( finfile( stream, tmpfile ) ) {
               LOG( LOG_ERR, "Failed to finalize file %zu\n", stream->filecount - 1 );
               errorflag = 1;
            }
         }
         // close the metadata handle for the file
         if ( ms->mdal->close( tmpfile->metahandle ) ) {
            // worth warning about, but probably not a serious issue
            LOG( LOG_WARNING, "Failed to close metahandle for file %zu\n", stream->filecount );
         }
      }

      // relocate the current file to the first position and eliminate all others 
      stream->files[0] = stream->files[stream->filecount - 1];
      curfile = stream->files;
      stream->filecount = 1; // drop all file references besides the current
   }

   // actually increase the size of the current file
   curfile->ftag.bytes += length;
   curfile->ftag.availbytes += length;

   // update the current FTAG value
   curfile->ftag.state = FTAG_SIZED | ( curfile->ftag.state & ~(FTAG_DATASTATE) );
   if ( putftag( stream, curfile ) ) {
      LOG( LOG_ERR, "Failed to update FTAG value of current file to reflect extension\n" );
      errorflag = 1;
   }

   // check for any previous error conditions
   if ( errorflag ) {
      // this stream is in an ugly state, and should be considered unrecoverable
      errno = EBADFD;
      stream->type = ERROR_STREAM;
      return -1;
   }

   return 0;
}

int datastream_release( DATASTREAM stream ) {
   // check for NULL stream
   if ( stream == NULL ) {
      LOG( LOG_ERR, "Received a NULL stream reference\n" );
      errno = EINVAL;
      return NULL;
   }

   // create some shorthand references
   marfs_ms* ms = &(stream->ns->prepo->metascheme);
   marfs_ds* ds = &(stream->ns->prepo->datascheme);
   STREAMFILE curfile = NULL;
   if ( stream->filecount ) { curfile = stream->files + (stream->filecount - 1); }
   char errorflag = 0;

   // create streams require special care
   if ( stream->type == CREAT_STREAM  &&  curfile ) {
      // update current FTAG to finalize size and enable parallel access
      curfile->ftag.state = FTAG_FIN | FTAG_WRITABLE | ( curfile->ftag.state & ~(FTAG_DATASTATE) );
      // attach the FTAG value
      if ( putftag( stream, curfile ) ) {
         LOG( LOG_ERR, "Failed to finalize and enable write access for current FTAG value\n" );
         errorflag = 1;
      }
   }

   // close our object handle, if present
   char globalerror = 0;
   if ( stream->datahandle  &&  ne_close( stream->datahandle ) ) {
      LOG( LOG_ERR, "Failed to properly close data object handle\n" );
      errorflag = 1;
      globalerror = 1;
   }

   // if we're still dealing with packed files, this becomes more complex
   if ( stream->filecount > 1 ) {
      // sanity check, this should always be a CREAT stream
      if ( stream->type != CREAT_STREAM ) {
         LOG( LOG_ERR, "Detected excessive file count for non-CREAT stream\n" );
         errno = EBADFD;
         errorflag = 1;
      }
      else if ( !(globalerror)  &&  curfile->ftag.bytes == 0 ) {
         // only if the data object synced AND the last thing we wrote was previous file recovery info, 
         //  can we safely finalize the previous files
         int iter = 0;
         for ( ; iter < (stream->filecount - 1); iter++ ) {
            if ( finfile( stream, stream->files + iter ) ) {
               LOG( LOG_ERR, "Failed to finalize file %zu\n", iter );
               errorflag = 1;
            }
         }
      }
      else {
         errorflag = 1;
      }
   }

   // just destroy the stream, no need to adjust any further state
   freestream( stream );

   if ( errorflag ) { return -1; }
   return 0;
}

int datastream_close( DATASTREAM stream ) {
   // check for NULL stream
   if ( stream == NULL ) {
      LOG( LOG_ERR, "Received a NULL stream reference\n" );
      errno = EINVAL;
      return NULL;
   }
   if ( stream->type == ERROR_STREAM ) {
      LOG( LOG_ERR, "Received an in-error stream reference\n" );
      errno = EINVAL;
      return NULL;
   }
   // make sure this stream actually has a current file reference
   if ( !(stream->filecount) ) {
      LOG( LOG_ERR, "Current stream is not referencing an active file\n" );
      errno = EINVAL;
      return -1;
   }

   // create some shorthand references
   marfs_ms* ms = &(stream->ns->prepo->metascheme);
   marfs_ds* ds = &(stream->ns->prepo->datascheme);
   STREAMFILE curfile = stream->files + (stream->filecount - 1);
   char errorflag = 0;

   // if this is a create stream, and either...
   //    we ARE already writing to an active data object
   //  or
   //    we ARE NOT relying on the metadata FS for data storage
   // ...then we must store recovery info for the previous file
   if ( stream->type == CREATE_STREAM  &&
         ( stream->datahandle != NULL  ||  ms->directchunks == 0 ) ) {

      stream->finfo.eof = 1; // this is the end of the previous file

      // if we haven't ever opened the current data object, we now need to
      if ( stream->datahandle == NULL ) {
         if ( opencurrentobj( stream ) ) {
            LOG( LOG_ERR, "Failed to create initial data object\n" );
            errorflag = 1;
         }
      }
      if ( errorflag == 0 ) {
         // output file recovery info
         errorflag = dumpfinfo( stream );
      }
   }

   // mark the current file as the end of this stream
   curfile->ftag.endofstream = 1;

   char globalerror = errorflag; // failure to write recov info is unrecoverable
   // close the data object, if we have one
   if ( stream->datahandle != NULL  &&  ne_close( stream->datahandle ) ) {
      LOG( LOG_ERR, "Failed to sync/close current data object\n" );
      errorflag = 1;
      globalerror = 1; // don't indicate success for ANY files
   }
   stream->datahandle == NULL;
   // update all currently referenced files
   for( ; stream->filecount > 0; stream->filecount-- ) {
      STREAMFILE tmpfile = stream->files + ( stream->filecount - 1);
      if ( globalerror == 0  &&  stream->type != READ_STREAM ) {
         // set all final values on this file
         if ( finfile( stream, tmpfile ) ) {
            LOG( LOG_ERR, "Failed to finalize file %zu\n", stream->filecount - 1 );
            errorflag = 1;
         }
      }
      // close the metadata handle for the file
      if ( ms->mdal->close( tmpfile->metahandle ) ) {
         // worth warning about, but probably not a serious issue
         LOG( LOG_ERROR, "Failed to close metahandle for file %zu\n", stream->filecount );
         errorflag = 1;
      }
   }

   // done with this stream
   freestream( stream );

   // check for any error conditions
   if ( errorflag ) { return -1; }
   return 0;
}

int datastream_chunkbounds( DATASTREAM stream, int chunknum, off_t* offset, size_t* size ) {
   // check for NULL or ERROR stream
   if ( stream == NULL ) {
      LOG( LOG_ERR, "Received a NULL stream reference\n" );
      errno = EINVAL;
      return -1;
   }
   if ( stream->type == ERROR_STREAM ) {
      LOG( LOG_ERR, "Received an in-error stream reference\n" );
      errno = EINVAL;
      return -1;
   }
   // make sure this stream actually has a current file reference
   if ( !(stream->filecount) ) {
      LOG( LOG_ERR, "Current stream is not referencing an active file\n" );
      errno = EINVAL;
      return -1;
   }

   // do a simple bounds check
   if ( chunknum < 0 ) {
      LOG( LOG_ERR, "Recieved negative chunknum argument\n" );
      errno = EINVAL;
      return -1;
   }

   // get object layout info
   size_t startobj;
   size_t startoff;
   size_t totsz;
   size_t dataperobj = gettargets( stream, 0, SEEK_SET, &(startobj), &(startoff), &(totsz) );
   if ( dataperobj == 0 ) {
      LOG( LOG_ERR, "Failed to identify data bounds for the current stream\n" );
      return -1;
   }

   // we only really care about three cases
   // check if the target is chunk zero
   if ( chunknum == 0 ) {
      *offset = 0;
      *size = (dataperobj - startoff);
   }
   // check if the target is any other in-bounds chunk
   else if ( chunknum <= ( (totsz + startoff) / dataperobj ) ) {
      *offset = ( chunknum * dataperobj ) - startoff;
      *size = dataperobj;
   }
   // the target is out of bounds
   else {
      LOG( LOG_ERR, "Target chunknumber of %d is out of bounds\n", chunknum );
      errno = EINVAL;
      return -1;
   }

   // CREAT streams allow the caller to determin maximum possible chunk sizes, assuming the file will expand
   if ( stream->type == CREAT_STREAM ) {
      // So... we're done
      return 0;
   }

   // now, we need to ensure our chunksize does not expand beyond appropriate file bounds
   FTAG_STATE datastate = ( stream->files[stream->filecount - 1].ftag.state & FTAG_DATASTATE );
   if ( (*size + *offset) > totsz ) {
      // incomplete files require special care
      if ( datastate == FTAG_SIZED  ||  datastate == FTAG_INIT ) {
         // we can't permit access to this chunk ( final chunk ) by non-CREAT streams
         *size = 0;
      }
      else {
         // limit chunk size to the upper bound of the file itself
         *size = totsz - *offset;
      }
   }

   return 0;
}

off_t datastream_seek( DATASTREAM stream, off_t offset, int whence ) {
   // check for NULL or ERROR stream
   if ( stream == NULL ) {
      LOG( LOG_ERR, "Received a NULL stream reference\n" );
      errno = EINVAL;
      return -1;
   }
   if ( stream->type == ERROR_STREAM ) {
      LOG( LOG_ERR, "Received an in-error stream reference\n" );
      errno = EINVAL;
      return -1;
   }
   // CREAT streams cannot be seeked
   if ( stream->type == CREAT_STREAM ) {
      LOG( LOG_ERR, "Received a create stream reference\n" );
      errno = EINVAL;
      return -1;
   }
   // make sure this stream actually has a current file reference
   if ( !(stream->filecount) ) {
      LOG( LOG_ERR, "Current stream is not referencing an active file\n" );
      errno = EINVAL;
      return -1;
   }

   // create some shorthand references
   marfs_ms* ms = &(stream->ns->prepo->metascheme);
   marfs_ds* ds = &(stream->ns->prepo->datascheme);
   STREAMFILE curfile = stream->files + (stream->filecount - 1);
   char errorflag = 0;

   // establish targets for the seek
   size_t tgtobj;
   size_t tgtoff;
   size_t remaining;
   size_t dataperobj = gettargets( stream, offset, whence, &(tgtobj), &(tgtoff), &(remaining) );
   if ( dataperobj == 0 ) {
      LOG( LOG_ERR, "Cannot identify offsets of the specified target\n" );
      return -1;
   }

   // write streams can only seek to object boundaries
   if ( stream->type != READ_STREAM  &&  tgtoff != 0 ) {
      LOG( LOG_ERR, "Seek target does not align with chunk boundaries\n" );
      errno = EINVAL;
      return -1;
   }
   else {
      // read streams need to have their offset adjusted to skip the recovery header info
      tgtoff += stream->recoveryheaderlen;
   }

   // non-CREAT streams are not permitted to access the final chunk of an unbounded file
   if ( (curfile->ftag.state & FTAG_DATASTATE) < FTAG_FIN  &&  remaining < dataperobj ) {
      LOG( LOG_ERR, "Access to an unbounded data chunk is not permitted for non-CREAT streams\n" );
      errno = EINVAL;
      return -1;
   }

   // check if we need to switch data objects
   if ( tgtobj != stream->objno  &&  stream->datahandle ) {
      // non-READ streams need to output trailing recovery info
      if ( stream->type != READ_STREAM ) {
         errorflag = dumpfinfo( stream );
      }
      // close the current object reference
      if ( ne_close( stream->datahandle ) ) {
         LOG( LOG_ERR, "Failed to close/sync current data handle\n" );
         errorflag = 1;
      }
      stream->datahandle = NULL;
   }

   // set stream offset values to the new targets
   stream->objno = tgtobj;
   stream->offset = tgtoffset;

   // check for any previous errors
   if ( errorflag ) {
      errno = EBADFD;
      stream->type = ERROR_STREAM;
      return -1;
   }

   return offset;
}

int datastream_truncate( DATASTREAM stream, off_t length ) {
   // check for NULL or ERROR stream
   if ( stream == NULL ) {
      LOG( LOG_ERR, "Received a NULL stream reference\n" );
      errno = EINVAL;
      return -1;
   }
   if ( stream->type == ERROR_STREAM ) {
      LOG( LOG_ERR, "Received an in-error stream reference\n" );
      errno = EINVAL;
      return -1;
   }
   // only EDIT streams can be truncated
   if ( stream->type != EDIT_STREAM ) {
      LOG( LOG_ERR, "Received a non-edit stream reference\n" );
      errno = EINVAL;
      return -1;
   }
   // make sure this stream actually has a current file reference
   if ( !(stream->filecount) ) {
      LOG( LOG_ERR, "Current stream is not referencing an active file\n" );
      errno = EINVAL;
      return -1;
   }

   // negative offset values are unacceptable
   if ( length < 0 ) {
      LOG( LOG_ERR, "Received a negative length value\n" );
      errno = EINVAL;
      return -1;
   }

   // create some shorthand references
   marfs_ms* ms = &(stream->ns->prepo->metascheme);
   marfs_ds* ds = &(stream->ns->prepo->datascheme);
   STREAMFILE curfile = stream->files + (stream->filecount - 1);
   size_t oldavail = curfile->ftag.availbytes;

   // EDIT streams can only truncate if a file's content is complete
   if ( (curfile->ftag.state & FTAG_DATASTATE) != FTAG_COMP ) {
      LOG( LOG_ERR, "Edit streams cannot truncate an incomplete file\n" );
      errno = EPERM;
      return -1;
   }

   // adjust the available bytes FTAG value
   if ( length < curfile->ftag.availbytes ) {
      curfile->ftag.availbytes = length;
   }

   // update the FTAG values
   if ( putftag( stream, curfile ) ) {
      LOG( LOG_ERR, "Failed to update FTAG value to reflect truncate op\n" );
      curfile->ftag.availbytes = oldavail; // reset FTAG value
      return -1;
   }
   // actually truncate the metadata file
   if ( ms->mdal->ftruncate( curfile->metahandle, length ) ) {
      LOG( LOG_ERR, "Failed to truncate metadata file to proper length\n" );
      return -1;
   }

   return 0;
}

int datastream_utimens( DATASTREAM stream, const struct timespec times[2] ) {
   // check for NULL or ERROR stream
   if ( stream == NULL ) {
      LOG( LOG_ERR, "Received a NULL stream reference\n" );
      errno = EINVAL;
      return -1;
   }
   if ( stream->type == ERROR_STREAM ) {
      LOG( LOG_ERR, "Received an in-error stream reference\n" );
      errno = EINVAL;
      return -1;
   }
   // READ streams cannot edit time values
   if ( stream->type == READ_STREAM ) {
      LOG( LOG_ERR, "Received a read stream reference\n" );
      errno = EINVAL;
      return -1;
   }
   // make sure this stream actually has a current file reference
   if ( !(stream->filecount) ) {
      LOG( LOG_ERR, "Current stream is not referencing an active file\n" );
      errno = EINVAL;
      return -1;
   }

   // create some shorthand references
   marfs_ms* ms = &(stream->ns->prepo->metascheme);
   marfs_ds* ds = &(stream->ns->prepo->datascheme);
   STREAMFILE curfile = stream->files + (stream->filecount - 1);

   if ( stream->type == EDIT_STREAM ) {
      // EDIT streams can only update times if a file's content is complete
      if ( (curfile->ftag.state & FTAG_DATASTATE) != FTAG_COMP ) {
         LOG( LOG_ERR, "Edit streams cannot update times of an incomplete file\n" );
         errno = EPERM;
         return -1;
      }
      // actually update the file times
      if ( ms->mdal->futimens( curfile->metahandle, times ) ) {
         LOG( LOG_ERR, "Failed to update time values on current metadata handle\n" );
         return -1;
      }
      return 0; // all that needs to be done for EDIT streams
   }

   // for CREAT streams, just stash the values until the file is complete
   curfile->times = times;
   return 0;
}

ssize_t datastream_write( DATASTREAM stream, const void* buff, size_t size ){
   // check for NULL or ERROR stream
   if ( stream == NULL ) {
      LOG( LOG_ERR, "Received a NULL stream reference\n" );
      errno = EINVAL;
      return -1;
   }
   if ( stream->type == ERROR_STREAM ) {
      LOG( LOG_ERR, "Received an in-error stream reference\n" );
      errno = EINVAL;
      return -1;
   }
   // READ streams cannot be written to
   if ( stream->type == READ_STREAM ) {
      LOG( LOG_ERR, "Received a read stream reference\n" );
      errno = EINVAL;
      return -1;
   }
   // make sure this stream actually has a current file reference
   if ( !(stream->filecount) ) {
      LOG( LOG_ERR, "Current stream is not referencing an active file\n" );
      errno = EINVAL;
      return -1;
   }

   // create some shorthand references
   marfs_ms* ms = &(stream->ns->prepo->metascheme);
   marfs_ds* ds = &(stream->ns->prepo->datascheme);
   STREAMFILE curfile = stream->files + (stream->filecount - 1);

   // identify data boundaries
   size_t remaining;
   size_t dataperobj = gettargets( stream, 0, SEEK_CUR, NULL, NULL, &(remaining) );
   if ( stream->type == EDIT_STREAM ) {
      // verify that the current file is even writable via an EDIT stream
      if ( !(curfile->ftag.state & FTAG_WRITABLE) ) {
         LOG( LOG_ERR, "The current FTAG has not been opened for parallel write access\n" );
         errno = EPERM;
         return -1;
      }
      // EDIT streams cannot write to any unbounded data chunk
      if ( (curfile->ftag.stat & FTAG_DATASTATE) < FTAG_FIN ) {
         remaining -= remaining % dataperobj;
      }
      // reduce the write size, if necessary
      if ( remaining < size ) {
         LOG( LOG_INFO, "Reducing write from %zu bytes to %zu, to fit within file bounds\n", size, remaining );
         size = remaining;
      }
   }

   // loop until all data has been written
   ssize_t written = 0;
   while ( size ) {

      // may need to create a new data object
      if ( stream->datahandle == NULL ) {
         // sanity check, we should be at a zero offset'
         if ( stream->offset ) {
            LOG( LOG_ERR, "No obj ref and non-zero offset value\n" );
            errno = EBADFD;
            stream->type = ERROR_STREAM;
            return -1;
         }
         // create a new data object
         if ( opencurrentobj( stream ) ) {
            stream->type = ERROR_STREAM;
            return -1;
         }
      }

      // identify how much data we can currently write out
      size_t canwrite = dataperobj - (stream->offset - stream->recoveryheaderlen);

      // done with the current object
      if ( canwrite == 0 ) {
         // write out file recovery info
         if ( dumpfinfo( stream ) ) {
            LOG( LOG_ERR, "Failed to output intermediate file recovery info\n" );
            stream->type = ERROR_STREAM;
            return -1;
         }
         // finalize the current data object
         if ( ne_close( stream->datahandle ) ) {
            LOG( LOG_ERR, "Failed to close / sync current data object\n" );
            stream->datahandle = NULL;
            stream->type = ERROR_STREAM;
            return -1;
         }
         stream->datahandle = NULL;
         // progress to the next object
         stream->objno++;
         stream->offset = 0;
      }
      else {
         // output any data we can
         ssize_t writeres = ne_write( stream->datahandle, buff + written, (canwrite > size) ? size : canwrite );
         if ( writeres <= 0 ) {
            LOG( LOG_ERR, "Failed to write to current data object\n" );
            stream->type = ERROR_STREAM;
            return -1;
         }
         // adjust written and remaining counts
         written += writeres;
         size -= writeres;
      }
   }

   return written;
}

ssize_t datastream_read( DATASTREAM stream, void* buffer, size_t size ) {
   // check for NULL
   if ( stream == NULL ) {
      LOG( LOG_ERR, "Received a NULL stream reference\n" );
      errno = EINVAL;
      return -1;
   }
   // only READ streams with READABLE FTAG values can be... read...
   if ( stream->type != READ_STREAM ) {
      LOG( LOG_ERR, "Received a read stream reference\n" );
      errno = EINVAL;
      return -1;
   }

   // create some shorthand references
   marfs_ms* ms = &(stream->ns->prepo->metascheme);
   marfs_ds* ds = &(stream->ns->prepo->datascheme);
   STREAMFILE curfile = stream->files + (stream->filecount - 1);

   // verify that the FTAG is in a readable state
   if ( !(curfile->ftag.state & FTAG_READABLE) ) {
      LOG( LOG_ERR, "Current FTAG state does not support reading\n" );
      errno = EPERM;
      return -1;
   }

   // identify data boundaries
   size_t remaining;
   size_t dataperobj = gettargets( stream, 0, SEEK_CUR, NULL, NULL, &(remaining) );

   // reduce the read size, if necessary
   if ( remaining < size ) {
      LOG( LOG_INFO, "Reducing read from %zu bytes to %zu, to fit within file bounds\n", size, remaining );
      size = remaining;
   }

   // loop until all data has been read
   ssize_t read = 0;
   while ( size ) {

      // may need to open a new data object
      if ( stream->datahandle == NULL  &&  opencurrentobj( stream ) ) {
         stream->type = ERROR_STREAM;
         return -1;
      }

      // identify how much data we can currently write out
      size_t canread = dataperobj - (stream->offset - stream->recoveryheaderlen);

      // done with the current object
      if ( canread == 0 ) {
         // close the current data object
         if ( ne_close( stream->datahandle ) ) {
            LOG( LOG_ERR, "Failed to close / sync current data object\n" );
            stream->datahandle = NULL;
            stream->type = ERROR_STREAM;
            return -1;
         }
         stream->datahandle = NULL;
         // progress to the next object
         stream->objno++;
         stream->offset = stream->recoveryheaderlen;
      }
      else {
         // output any data we can
         ssize_t readres = ne_read( stream->datahandle, buff + read, (canread > size) ? size : canread );
         if ( readres <= 0 ) {
            LOG( LOG_ERR, "Failed to read from current data object\n" );
            stream->type = ERROR_STREAM;
            return -1;
         }
         // adjust written and remaining counts
         read += readres;
         size -= readres;
      }
   }

   return read;
}

#endif

