/*
   Copyright 2014 The Trustees of Princeton University

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "diffdir.h"

int usage( char const* prog_name ) {
   
   fprintf(stderr, "Usage: %s [SYNDICATE_OPTS] LEAST_UNKNOWN_GENERATION FILE_ID\n");
   return 0;
}

int main( int argc, char** argv ) {
   
   int rc = 0;
   struct syndicate_state state;
   struct md_opts opts;
   struct UG_opts ug_opts;
   int local_optind = 0;
   uint64_t file_id = 0;
   uint64_t parent_id = 0;
   int64_t least_unknown_generation = 0;
   ms_path_t path;
   struct ms_client_multi_result result;
   vector<uint64_t> file_ids;
   
   if( argc < 3 ) {
      usage( argv[0] );
      exit(1);
   }
   
   memset( &result, 0, sizeof(struct ms_client_multi_result) );
   
   memset( &opts, 0, sizeof(struct md_opts) );
   
   memset( &ug_opts, 0, sizeof(struct UG_opts) );
   
   // get options
   rc = md_opts_parse( &opts, argc, argv, &local_optind, NULL, NULL );
   if( rc != 0 ) {
      SG_error("md_opts_parse rc = %d\n", rc );
      md_common_usage( argv[0] );
      usage( argv[0] );
      exit(1);
   }
   
   // connect to syndicate
   rc = syndicate_client_init( &state, &opts, &ug_opts );
   if( rc != 0 ) {
      SG_error("syndicate_client_init rc = %d\n", rc );
      exit(1);
   }
   
   // get parent id and parse it
   rc = sscanf( argv[local_optind], "%" PRIX64, &parent_id );
   if( rc != 1 ) {
      SG_error("failed to parse parent ID '%s'\n", argv[local_optind] );
      exit(1);
   }
   
   // get least unknown generation 
   rc = sscanf( argv[local_optind+1], "%" PRId64, &least_unknown_generation );
   if( rc != 1 ) {
      SG_error("failed to parse least unkonwn generation '%s'\n", argv[local_optind+1] );
      exit(1);
   }
   
   printf("\n\n\nBegin diffdir\n\n\n");

   rc = ms_client_diffdir( state.ms, parent_id, 0, 0, 100, least_unknown_generation, &result );
   
   if( rc != 0 ) {
      SG_error("ms_client_diffdir rc = %d\n", rc );
      exit(1);
   }
   
   printf("\n\n\n");
      
   for( size_t i = 0; i < result.num_ents; i++ ) {
      
      if( result.ents[i].file_id != 0 ) {
         printf("Entry: %" PRIX64 " %s mode=%o version=%" PRId64 " write_nonce=%" PRId64 " generation=%d\n",
               result.ents[i].file_id, result.ents[i].name, result.ents[i].mode, result.ents[i].version, result.ents[i].write_nonce, result.ents[i].generation );
      }
   }
   
   ms_client_multi_result_free( &result );
   
   printf("\n\n\nEnd diffdir\n\n\n");
   
   syndicate_client_shutdown( &state, 0 );
   
   return 0;
}
