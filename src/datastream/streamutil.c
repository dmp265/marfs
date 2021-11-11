#ifndef __MARFS_COPYRIGHT_H__
#define __MARFS_COPYRIGHT_H__

/*
Copyright (c) 2015, Los Alamos National Security, LLC
All rights reserved.

Copyright 2015.  Los Alamos National Security, LLC. This software was produced
under U.S. Government contract DE-AC52-06NA25396 for Los Alamos National
Laboratory (LANL), which is operated by Los Alamos National Security, LLC for
the U.S. Department of Energy. The U.S. Government has rights to use, reproduce,
and distribute this software.  NEITHER THE GOVERNMENT NOR LOS ALAMOS NATIONAL
SECURITY, LLC MAKES ANY WARRANTY, EXPRESS OR IMPLIED, OR ASSUMES ANY LIABILITY
FOR THE USE OF THIS SOFTWARE.  If software is modified to produce derivative
works, such modified software should be clearly marked, so as not to confuse it
with the version available from LANL.

Additionally, redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.
3. Neither the name of Los Alamos National Security, LLC, Los Alamos National
Laboratory, LANL, the U.S. Government, nor the names of its contributors may be
used to endorse or promote products derived from this software without specific
prior written permission.

THIS SOFTWARE IS PROVIDED BY LOS ALAMOS NATIONAL SECURITY, LLC AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL LOS ALAMOS NATIONAL SECURITY, LLC OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-----
NOTE:
-----
Although these files reside in a seperate repository, they fall under the MarFS copyright and license.

MarFS is released under the BSD license.

MarFS was reviewed and released by LANL under Los Alamos Computer Code identifier:
LA-CC-15-039.

These erasure utilites make use of the Intel Intelligent Storage
Acceleration Library (Intel ISA-L), which can be found at
https://github.com/01org/isa-l and is under its own license.

MarFS uses libaws4c for Amazon S3 object communication. The original version
is at https://aws.amazon.com/code/Amazon-S3/2601 and under the LGPL license.
LANL added functionality to the original work. The original work plus
LANL contributions is found at https://github.com/jti-lanl/aws4c.

GNU licenses can be found at http://www.gnu.org/licenses/.
*/

#endif

#include "marfs_auto_config.h"
#include "datastream.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>


#define PROGNAME "marfs_streamutil"
#define OUTPREFX PROGNAME ": "


typedef struct argopts_struct {
   char streamnum;
   int streamnumval;
   char path;
   char* pathval;
   char mode;
   mode_t modeval;
   char ctag;
   char* ctagval;
   char type;
   STREAM_TYPE typeval;
   char bytes;
   size_t bytesval;
   char outputfile;
   char* outputfileval;
   char offset;
   off_t offsetval;
   char seekfrom;
   int seekfromval;
   char chunknum;
   size_t chunknumval;
   char length;
   off_t lengthval;
} argopts; 


void free_argopts( argopts* opts ) {
   if ( opts->pathval ) { free( opts->pathval ); }
   if ( opts->ctagval ) { free( opts->ctagval ); }
   if ( opts->outputfileval ) { free( opts->outputfileval ); }
}


// Show all the usage options in one place, for easy reference
// An arrow appears next to the one you tried to use.
//
void usage(const char *op)
{

   printf("Usage: <op> [<args> ...]\n");
   printf("  Where <op> and <args> are like one of the following:\n");
   printf("\n");

#define USAGE(CMD, ARGS, DESC)        \
   printf("  %2s %-11s %s\n%s", \
            (!strncmp(op, CMD, 11) ? "->" : ""), \
            (CMD), (ARGS), (!strncmp(op, "help", 5) ? "      " DESC "\n" : "") )

   USAGE( "create",
          "[-s stream-num] -p path -m mode -c ctag",
          "Create a new file, associated with the given datastream" );

   USAGE( "open",
          "[-s stream-num] -t type -p path",
          "Open a file, associated with the given datastream" );

   USAGE( "release",
          "[-s stream-num]",
          "Release the given datastream" );

   USAGE( "close",
          "[-s stream-num]",
          "Close the given datastream" );

   USAGE( "read",
          "[-s stream-num] [-b bytes] [-o outputfile [-@ outputoffset [-f seekfrom] ] ]",
          "Read from the given datastream" );

   USAGE( "write",
          "[-s stream-num] [-b bytes] [-i inputfile [-@ inputoffset [-f seekfrom] ] ]",
          "Write to the given datastream" );

   USAGE( "setrpath",
          "[-s stream-num] -p path",
          "Set the recovery path of the given datastream" );

   USAGE( "seek",
          "[-s stream-num] -@ offset -f seekfrom",
          "Seek the given datastream" );

   USAGE( "chunkbounds",
          "[-s stream-num] [-c chunknum]",
          "Identify chunk boundries for the given datastream" );

   USAGE( "extend",
          "[-s stream-num] -l length",
          "Extend the given datastream" );

   USAGE( "truncate",
          "[-s stream-num] -l length",
          "Truncate the given datastream" );

   USAGE( "utime",
          "[-s stream-num] -i inputfile",
          "Truncate the given datastream" );

   USAGE( "streamlist",
          "[-s stream-num]",
          "Truncate the given datastream" );

   USAGE( "( exit | quit )",
          "",
          "Terminate ( active streams will be released )" );

   USAGE( "help", "", "Print this usage info" );

   printf("\n");

#undef USAGE
}

int create_command( marfs_config* config, DATASTREAM* stream, argopts* opts ) {
   return 0;
}

int open_command( marfs_config* config, DATASTREAM* stream, argopts* opts ) {
   return 0;
}

int release_command( marfs_config* config, DATASTREAM* stream, argopts* opts ) {
   return 0;
}

int close_command( marfs_config* config, DATASTREAM* stream, argopts* opts ) {
   return 0;
}

int read_command( marfs_config* config, DATASTREAM* stream, argopts* opts ) {
   return 0;
}

int write_command( marfs_config* config, DATASTREAM* stream, argopts* opts ) {
   return 0;
}

int setrpath_command( marfs_config* config, DATASTREAM* stream, argopts* opts ) {
   return 0;
}

int seek_command( marfs_config* config, DATASTREAM* stream, argopts* opts ) {
   return 0;
}

int chunkbounds_command( marfs_config* config, DATASTREAM* stream, argopts* opts ) {
   return 0;
}

int extend_command( marfs_config* config, DATASTREAM* stream, argopts* opts ) {
   return 0;
}

int truncate_command( marfs_config* config, DATASTREAM* stream, argopts* opts ) {
   return 0;
}

int command_loop( marfs_config* config ) {
   // allocate list structures
   int streamalloc = 10;
   DATASTREAM* streamlist = calloc( streamalloc, sizeof( DATASTREAM ) );
   if ( streamlist == NULL ) {
      printf( OUTPREFX "ERROR: Failed to allocate DATASTREAM list\n" );
      return -1;
   }
   char** streamdesc = malloc( sizeof( char* ) * streamalloc );
   if ( streamdesc == NULL ) {
      printf( OUTPREFX "ERROR: Failed to allocate stream description list\n" );
      free( streamlist );
      return -1;
   }

   // infinite loop, processing user commands
   printf( OUTPREFX "Ready for user commands\n" );
   int tgtstream = 0;
   int retval = 0;
   while ( 1 ) {
      printf( "> " );
      fflush( stdout );
      // read in a new line from stdin ( 4096 char limit )
      char inputline[4097] = {0}; // init to NULL bytes
      if ( scanf( "%4096[^\n]", inputline ) < 0 ) {
         printf( OUTPREFX "ERROR: Failed to read user input\n" );
         retval = -1;
         break;
      }
      fgetc( stdin ); // to clear newline char
      if ( inputline[4095] != '\0' ) {
         printf( OUTPREFX "ERROR: Input command exceeds parsing limit of 4096 chars\n" );
         retval = -1;
         break;
      }

      // parse the input command
      char* parse = inputline;
      char repchar = 0;
      while ( *parse != '\0' ) {
         parse++;
         if ( *parse == ' ' ) { *parse = '\0'; repchar = 1; }
      }
      // check for program exit right away
      if ( strcmp( inputline, "exit" ) == 0  ||  strcmp( inputline, "quit" ) == 0 ) {
         printf( OUTPREFX "Terminating...\n" );
         break;
      }
      if ( repchar ) {
         parse++; // proceed to the char following ' '
      }
      // parse all command arguments
      argopts inputopts = {
         .streamnum = 0,
         .streamnumval = 0,
         .path = 0,
         .pathval = NULL,
         .mode = 0,
         .modeval = 0,
         .ctag = 0,
         .ctagval = NULL,
         .type = 0,
         .typeval = 0,
         .bytes = 0,
         .bytesval = 0,
         .outputfile = 0,
         .outputfileval = NULL,
         .offset = 0,
         .offsetval = 0,
         .seekfrom = 0,
         .seekfromval = 0,
         .chunknum = 0,
         .chunknumval = 0,
         .length = 0,
         .lengthval = 0
      };
      char argerror = 0;
      char* argparse = strtok( parse, " " );
      while ( argparse ) {
         char argchar = '\0';
         if ( *argparse != '-' ) {
            printf( OUTPREFX "ERROR: Unrecognized argument: \"%s\"\n", argparse );
            argerror = 1;
            break;
         }
         argchar = *(argparse + 1);
         // proceed to the next arguemnt substring
         argparse = strtok( NULL, " " );
         if ( argparse == NULL ) {
            printf( OUTPREFX "ERROR: '-%c' argument lacks a value\n", argchar );
            argerror = 1;
            break;
         }
         // check arg type
         char* endptr = NULL;
         unsigned long long parseval;
         switch( argchar ) {
            case 's':
               if ( inputopts.streamnum ) {
                  printf( OUTPREFX "ERROR: Duplicate '-s' argument detected\n" );
                  argerror = 1;
                  break;
               }
               parseval = strtoull( argparse, &(endptr), 10 );
               if ( parseval > INT_MAX ) {
                  printf( OUTPREFX "ERROR: Streamnum value exceeds type bounds: \"%s\"\n",
                          argparse );
                  argerror = 1;
                  break;
               }
               inputopts.streamnum = 1;
               inputopts.streamnumval = (int)parseval;
               break;
            case 'p':
               if ( inputopts.path ) {
                  printf( OUTPREFX "ERROR: Duplicate '-p' argument detected\n" );
                  argerror = 1;
                  break;
               }
               inputopts.path = 1;
               inputopts.pathval = strdup( argparse );
               if ( inputopts.pathval == NULL ) {
                  printf( OUTPREFX "ERROR: Failed to allocate space for 'path' value: \"%s\"\n", argparse );
                  argerror = 1;
                  break;
               }
               break;
            case 'm':
               if ( inputopts.mode ) {
                  printf( OUTPREFX "ERROR: Duplicate '-m' argument detected\n" );
                  argerror = 1;
                  break;
               }
               parseval = strtoull( argparse, &(endptr), 8 );
               if ( parseval > 07777 ) {
                  printf( OUTPREFX "ERROR: Mode value exceeds type bounds: \"%s\"\n",
                          argparse );
                  argerror = 1;
                  break;
               }
               inputopts.mode = 1;
               inputopts.modeval = (mode_t)parseval;
               break;
            case 'c':
               if ( inputopts.ctag ) {
                  printf( OUTPREFX "ERROR: Duplicate '-c' argument detected\n" );
                  argerror = 1;
                  break;
               }
               inputopts.ctag = 1;
               inputopts.ctagval = strdup( argparse );
               if ( inputopts.ctagval == NULL ) {
                  printf( OUTPREFX "ERROR: Failed to allocate space for 'ctag' value: \"%s\"\n", argparse );
                  argerror = 1;
                  break;
               }
               break;
            case 't':
               if ( inputopts.type ) {
                  printf( OUTPREFX "ERROR: Duplicate '-t' argument detected\n" );
                  argerror = 1;
                  break;
               }
               inputopts.type = 1;
               if ( strcasecmp( argparse, "create" ) == 0 ) {
                  inputopts.typeval = CREATE_STREAM;
               }
               else if ( strcasecmp( argparse, "edit" ) == 0 ) {
                  inputopts.typeval = EDIT_STREAM;
               }
               else if ( strcasecmp( argparse, "read" ) == 0 ) {
                  inputopts.typeval = READ_STREAM;
               }
               else {
                  printf( OUTPREFX "ERROR: '-t' argument is unrecognized: \"%s\"\n",
                          argparse );
                  printf( OUTPREFX "ERROR: Acceptable values are 'read'/'edit'/'create'\n" );
                  argerror = 1;
                  break;
               }
               break;
            case 'b':
               if ( inputopts.bytes ) {
                  printf( OUTPREFX "ERROR: Duplicate '-b' argument detected\n" );
                  argerror = 1;
                  break;
               }
               parseval = strtoull( argparse, &(endptr), 10 );
               if ( parseval > SIZE_MAX ) {
                  printf( OUTPREFX "ERROR: Bytes value exceeds type bounds: \"%s\"\n",
                          argparse );
                  argerror = 1;
                  break;
               }
               inputopts.bytes = 1;
               inputopts.bytesval = (size_t)parseval;
               break;
            case 'o':
               if ( inputopts.outputfile ) {
                  printf( OUTPREFX "ERROR: Duplicate '-o' argument detected\n" );
                  argerror = 1;
                  break;
               }
               inputopts.outputfile = 1;
               inputopts.outputfileval = strdup( argparse );
               if ( inputopts.outputfileval == NULL ) {
                  printf( OUTPREFX "ERROR: Failed to allocate space for 'outputfile' value: \"%s\"\n", argparse );
                  argerror = 1;
                  break;
               }
               break;
            case '@':
               if ( inputopts.offset ) {
                  printf( OUTPREFX "ERROR: Duplicate '-@' argument detected\n" );
                  argerror = 1;
                  break;
               }
               parseval = strtoull( argparse, &(endptr), 10 );
               if ( parseval > INT_MAX ) {
                  printf( OUTPREFX "ERROR: Offset value exceeds type bounds: \"%s\"\n",
                          argparse );
                  argerror = 1;
                  break;
               }
               inputopts.offset = 1;
               inputopts.offsetval = (off_t)parseval;
               break;
            case 'f':
               if ( inputopts.seekfrom ) {
                  printf( OUTPREFX "ERROR: Duplicate '-f' argument detected\n" );
                  argerror = 1;
                  break;
               }
               inputopts.seekfrom = 1;
               if ( strcasecmp( argparse, "set" ) == 0 ) {
                  inputopts.seekfromval = SEEK_SET;
               }
               else if ( strcasecmp( argparse, "cur" ) == 0 ) {
                  inputopts.seekfromval = SEEK_CUR;
               }
               else if ( strcasecmp( argparse, "end" ) == 0 ) {
                  inputopts.seekfromval = SEEK_END;
               }
               else {
                  printf( OUTPREFX "ERROR: '-f' argument is unrecognized: \"%s\"\n",
                          argparse );
                  printf( OUTPREFX "ERROR: Acceptable values are 'set'/'cur'/'end'\n" );
                  argerror = 1;
                  break;
               }
               break;
            case 'n':
               if ( inputopts.chunknum ) {
                  printf( OUTPREFX "ERROR: Duplicate '-n' argument detected\n" );
                  argerror = 1;
                  break;
               }
               parseval = strtoull( argparse, &(endptr), 10 );
               if ( parseval > INT_MAX ) {
                  printf( OUTPREFX "ERROR: Chunknum value exceeds type bounds: \"%s\"\n",
                          argparse );
                  argerror = 1;
                  break;
               }
               inputopts.chunknum = 1;
               inputopts.chunknumval = (int)parseval;
               break;
            case 'l':
               if ( inputopts.length ) {
                  printf( OUTPREFX "ERROR: Duplicate '-l' argument detected\n" );
                  argerror = 1;
                  break;
               }
               parseval = strtoull( argparse, &(endptr), 10 );
               if ( parseval > SIZE_MAX ) {
                  printf( OUTPREFX "ERROR: Length value exceeds type bounds: \"%s\"\n",
                          argparse );
                  argerror = 1;
                  break;
               }
               inputopts.length = 1;
               inputopts.lengthval = (size_t)parseval;
               break;
            default:
               printf( OUTPREFX "ERROR: Unrecognized argument: \"-%c\"\n", argchar );
               argerror = 1;
               break;
         }
         // abort if we've hit an error
         if ( argerror ) { break; }
         // proceed to the next arguemnt substring
         argparse = strtok( NULL, " " );
      }

      // check for any errors parsing arguments
      if ( argerror ) {
         printf( OUTPREFX "ERROR: Skipping command execution due to previous errors\n" );
         usage( inputline );
         free_argopts( &(inputopts) );
         retval = -1; // remember any errors
         continue;
      }

      // resolve a target stream, if specified
      if ( inputopts.streamnum ) {
         tgtstream = inputopts.streamnum;
      }
      if ( tgtstream + 1 > streamalloc ) {
         // need to expand our stream allocation
         printf( OUTPREFX "Expanding stream list to accomodate %d entries...\n",
                 (tgtstream + 1) );
         streamlist = realloc( streamlist, (tgtstream + 1) * sizeof(DATASTREAM) );
         if ( streamlist == NULL ) {
            printf( OUTPREFX "ERROR: Failed to allocate expanded stream list\n" );
            free_argopts( &(inputopts) );
            return -1;
         }
         // zero out any added entries
         bzero( streamlist + streamalloc, sizeof(DATASTREAM) * ( (tgtstream + 1) - streamalloc ) );
         streamalloc = (tgtstream + 1);
      }

      // TODO command execution
      if ( strcmp( inputline, "create" ) == 0 ) {
      }
      else if ( strcmp( inputline, "open" ) == 0 ) {
      }
      else if ( strcmp( inputline, "release" ) == 0 ) {
      }
      else if ( strcmp( inputline, "close" ) == 0 ) {
      }
      else if ( strcmp( inputline, "read" ) == 0 ) {
      }
      else if ( strcmp( inputline, "write" ) == 0 ) {
      }
      else if ( strcmp( inputline, "setrpath" ) == 0 ) {
      }
      else if ( strcmp( inputline, "seek" ) == 0 ) {
      }
      else if ( strcmp( inputline, "chunkbounds" ) == 0 ) {
      }
      else if ( strcmp( inputline, "extend" ) == 0 ) {
      }
      else if ( strcmp( inputline, "truncate" ) == 0 ) {
      }
      else if ( strcmp( inputline, "utime" ) == 0 ) {
      }
      else if ( strcmp( inputline, "streamlist" ) == 0 ) {
      }
      else {
      }


      // free inputopts
      free_argopts( &(inputopts) );
   }

   // TODO release active streams

   // cleanup
   free( streamlist );
   free( streamdesc );
   return retval;
}


int main(int argc, const char **argv)
{
   errno = 0; // init to zero (apparently not guaranteed)
   char *config_path = NULL;
   char config_v = 0;

   char pr_usage = 0;
   int c;
   // parse all position-independent arguments
   while ((c = getopt(argc, (char *const *)argv, "c:vh")) != -1)
   {
      switch (c)
      {
         case 'c':
            config_path = optarg;
            break;
         case 'v':
            config_v = 1;
            break;
         case 'h':
         case '?':
            pr_usage = 1;
            break;
         default:
            printf("Failed to parse command line options\n");
            return -1;
      }
   }

   // check if we need to print usage info
   if ( pr_usage ) {
      printf( OUTPREFX "Usage info --\n" );
      printf( OUTPREFX "%s -c configpath [-v] [-h]\n", PROGNAME );
      printf( OUTPREFX "   -c : Path of the MarFS config file\n" );
      printf( OUTPREFX "   -v : Validate the MarFS config\n" );
      printf( OUTPREFX "   -h : Print this usage info\n" );
      return -1;
   }

   // verify that a config was defined
   if ( config_path == NULL ) {
      printf( OUTPREFX "no config path defined ( '-c' arg )\n" );
      return -1;
   }

   // read in the marfs config
   marfs_config* config = config_init( config_path );
   if ( config == NULL ) {
      printf( OUTPREFX "ERROR: Failed to initialize config: \"%s\" ( %s )\n",
              config_path, strerror(errno) );
      return -1;
   }
   printf( OUTPREFX "marfs config loaded...\n" );

   // validate the config, if requested
   if ( config_v ) {
      if ( config_validate( config ) ) {
         printf( OUTPREFX "ERROR: Failed to validate config: \"%s\" ( %s )\n",
                 config_path, strerror(errno) );
         config_term( config );
         return -1;
      }
      printf( OUTPREFX "config validated...\n" );
   }

   // enter the main command loop
   int retval = 0;
   if ( command_loop( config ) ) {
      retval = -1;
   }

   // terminate the marfs config
   if ( config_term( config ) ) {
      printf( OUTPREFX "WARNING: Failed to properly terminate MarFS config ( %s )\n",
              strerror(errno) );
      return -1;
   }

   return retval;
}

