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

#include "driver.h"
#include "http.h"
#include "map-info.h"
#include "core.h"
#include "cache.h"
#include "reversioner.h"

static char const* AG_HTTP_DRIVER_ERROR = "AG driver error\n";

// free a AG_connection_data
static void AG_connection_data_free( struct AG_connection_data* con_data ) {
   
   if( con_data == NULL )
      return;

   
   struct AG_driver* driver = con_data->ctx.driver;
   
   // driver connection cleanup
   if( con_data->ctx.request_type == AG_REQUEST_BLOCK ) {
      AG_driver_cleanup_block( driver, &con_data->ctx );
   }
   
   // free memory
   if( con_data->rb ) {
      response_buffer_free( con_data->rb );
      delete con_data->rb;
      con_data->rb = NULL;
   }

   if( con_data->ctx.args ) {
      free( con_data->ctx.args );
      con_data->ctx.args = NULL;
   }
   
   if( con_data->mi ) {
      AG_map_info_free( con_data->mi );
      free( con_data->mi );
      con_data->mi = NULL;
   }
   
   if( con_data->ctx.query_string ) {
      free( con_data->ctx.query_string );
      con_data->ctx.query_string = NULL;
   }
   
   md_gateway_request_data_free( &con_data->ctx.reqdat );
}

// get the HTTP status given by the driver to an AG_connection_context, or set a default one 
static int AG_get_driver_HTTP_status( struct AG_connection_context* ag_ctx, int default_status ) {
   if( ag_ctx->http_status != 0 ) {
      return ag_ctx->http_status;
   }
   else {
      return default_status;
   }
}

// populate and sign a manifest from the driver's published dataset info
// NOTE: the mi's cached metadata must be present 
int AG_populate_manifest( Serialization::ManifestMsg* mmsg, char const* path, struct AG_map_info* mi, struct AG_driver_publish_info* pub_info ) {
   
   // this only works for files...
   if( mi->type != MD_ENTRY_FILE ) {
      return -EINVAL;
   }
   
   // need all cached metadata 
   if( !AG_has_valid_cached_metadata( path, mi ) ) {
      errorf("Entry for %s does not have all cached metadata\n", path );
      return -EINVAL;
   }
   
   struct AG_state* state = AG_get_state();
   if( state == NULL ) {
      // shutting down
      return -ENOTCONN;
   }
   
   uint64_t volume_id = ms_client_get_volume_id( state->ms );
   uint64_t gateway_id = state->ms->gateway_id;
   uint64_t owner_id = state->ms->owner_id;
   uint64_t block_size = ms_client_get_volume_blocksize( state->ms );
   
   mmsg->set_volume_id( volume_id );
   mmsg->set_coordinator_id( gateway_id );
   mmsg->set_owner_id( owner_id );
   mmsg->set_file_id( mi->file_id );
   mmsg->set_file_version( mi->file_version );
   
   // driver supplies these fields
   mmsg->set_size( pub_info->size );
   mmsg->set_fent_mtime_sec( pub_info->mtime_sec );
   mmsg->set_fent_mtime_nsec( pub_info->mtime_nsec );
   mmsg->set_mtime_sec( pub_info->mtime_sec );
   mmsg->set_mtime_nsec( pub_info->mtime_nsec );
   
   // calculate blockinfo
   uint64_t num_blocks = 0;
   
   if( pub_info->size >= 0 ) {
      num_blocks = pub_info->size / block_size;
      
      if( pub_info->size % block_size != 0 ) {
         num_blocks++;
      }
   }
   
   Serialization::BlockURLSetMsg *bbmsg = mmsg->add_block_url_set();
   bbmsg->set_start_id( 0 );
   bbmsg->set_end_id( num_blocks );
   bbmsg->set_gateway_id( gateway_id );

   // use the random block version that corresponds to this file
   for( uint64_t i = 0; i < num_blocks; i++ ) {
      bbmsg->add_block_versions( mi->block_version );
   }
   
   // NOTE: no hashes, since they're served with the blocks directly
   
   // sign the message
   int rc = md_sign< Serialization::ManifestMsg >( state->ms->my_key, mmsg );
   
   AG_release_state( state );
   
   if( rc != 0 ) {
      errorf("gateway_sign_manifest rc = %d\n", rc );
      return rc;
   }
   
   return 0;
}


// what kind of request?
static int AG_request_type( struct md_gateway_request_data* reqdat ) {
   
   int request_type = 0;
   
   // what kind of request?
   if( reqdat->manifest_timestamp.tv_sec < 0 && reqdat->manifest_timestamp.tv_nsec < 0 ) {
      request_type = AG_REQUEST_BLOCK;
   }
   else {
      request_type = AG_REQUEST_MANIFEST;
   }
   
   return request_type;
}


// redirect to latest block
// mi must be coherent
static int AG_HTTP_redirect_latest_block( struct AG_state* state, struct md_HTTP_connection_data* md_con_data, struct md_gateway_request_data* reqdat, struct AG_map_info* mi ) {
   
   if( !mi->cache_valid ) {
      return -ESTALE;
   }
   
   md_con_data->status = 302;
   md_con_data->resp = CALLOC_LIST( struct md_HTTP_response, 1 );
   
   char* current_url = md_url_public_block_url( state->conf->content_url, state->conf->volume, reqdat->fs_path, mi->file_id, mi->file_version, reqdat->block_id, mi->block_version );
   
   md_create_HTTP_response_ram_static( md_con_data->resp, "text/plain", 302, MD_HTTP_302_MSG, strlen(MD_HTTP_302_MSG) + 1 );
   md_HTTP_add_header( md_con_data->resp, "Location", current_url );
   md_HTTP_add_header( md_con_data->resp, "Cache-Control", "no-store" );
   
   free( current_url );
   
   return 0;
}

// redirect to latest manifest
// mi must be coherent
static int AG_HTTP_redirect_latest_manifest( struct AG_state* state, struct md_HTTP_connection_data* md_con_data, struct md_gateway_request_data* reqdat, struct AG_map_info* mi, struct AG_driver_publish_info* pubinfo ) {

   if( pubinfo == NULL ) {
      return -EINVAL;
   }
   
   if( !mi->cache_valid ) {
      return -ESTALE;
   }
   
   md_con_data->status = 302;
   md_con_data->resp = CALLOC_LIST( struct md_HTTP_response, 1 );
   
   struct timespec ts;
   ts.tv_sec = pubinfo->mtime_sec;
   ts.tv_nsec = pubinfo->mtime_nsec;
   
   char* current_url = md_url_public_manifest_url( state->conf->content_url, state->conf->volume, reqdat->fs_path, mi->file_id, mi->file_version, &ts );
   
   md_create_HTTP_response_ram_static( md_con_data->resp, "text/plain", 302, MD_HTTP_302_MSG, strlen(MD_HTTP_302_MSG) + 1 );
   md_HTTP_add_header( md_con_data->resp, "Location", current_url );
   md_HTTP_add_header( md_con_data->resp, "Cache-Control", "no-store" );
   
   free( current_url );
   
   return 0;
}


// general error 
static int AG_HTTP_error( struct md_HTTP_connection_data* md_con_data, int http_status, char const* http_msg ) {
   
   md_con_data->status = http_status;
   md_con_data->resp = CALLOC_LIST( struct md_HTTP_response, 1 );
   md_create_HTTP_response_ram_static( md_con_data->resp, "text/plain", http_status, http_msg, strlen(http_msg) + 1 );
   
   return 0;
}


// bad request 
static int AG_HTTP_bad_request( struct md_HTTP_connection_data* md_con_data ) {
   return AG_HTTP_error( md_con_data, 400, MD_HTTP_400_MSG );
}

// internal server error 
static int AG_HTTP_internal_server_error( struct md_HTTP_connection_data* md_con_data ) {
   return AG_HTTP_error( md_con_data, 500, MD_HTTP_500_MSG );
}

// not found 
static int AG_HTTP_not_found( struct md_HTTP_connection_data* md_con_data ) {
   return AG_HTTP_error( md_con_data, 404, MD_HTTP_404_MSG );
}

// try again later 
static int AG_HTTP_try_again( struct md_HTTP_connection_data* md_con_data ) {
   return AG_HTTP_error( md_con_data, 503, MD_HTTP_503_MSG );
}

// driver error 
static int AG_HTTP_driver_error( struct md_HTTP_connection_data* md_con_data, int driver_status ) {
   return AG_HTTP_error( md_con_data, driver_status, AG_HTTP_DRIVER_ERROR );
}

// verify that the request is fresh.  mi must be coherent
// return 1 if redirected
// return 0 if fresh.
static int AG_HTTP_verify_fresh( struct AG_state* state, struct md_HTTP_connection_data* md_con_data, struct md_gateway_request_data* reqdat, struct AG_map_info* mi, struct AG_driver_publish_info* pubinfo ) {
   
   // determine request type 
   int request_type = AG_request_type( reqdat );
   int rc = 0;
   
   // sanity check: file version must match, or we must redirect
   if( reqdat->file_version != mi->file_version ) {
      
      errorf("Stale file version %" PRId64 " (expected %" PRId64 ")\n", reqdat->file_version, mi->file_version );
      
      if( request_type == AG_REQUEST_BLOCK ) {
         rc = AG_HTTP_redirect_latest_block( state, md_con_data, reqdat, mi );
      }
      else {
         rc = AG_HTTP_redirect_latest_manifest( state, md_con_data, reqdat, mi, pubinfo );
      }
      
      if( rc == -ESTALE ) {
         AG_HTTP_try_again( md_con_data );
      }
      
      return 1;
   }
   
   // sanity check: if this is a manifest, timestamp must match 
   if( request_type == AG_REQUEST_MANIFEST && (reqdat->manifest_timestamp.tv_sec != pubinfo->mtime_sec || reqdat->manifest_timestamp.tv_nsec != pubinfo->mtime_nsec) ) {
      
      errorf("Stale manifest timestamp %" PRId64 ".%" PRId64 " (expected %" PRId64 ".%d)\n",
             (int64_t)reqdat->manifest_timestamp.tv_sec, (int64_t)reqdat->manifest_timestamp.tv_nsec, pubinfo->mtime_sec, pubinfo->mtime_nsec );
      
      rc = AG_HTTP_redirect_latest_manifest( state, md_con_data, reqdat, mi, pubinfo );
      
      if( rc == -ESTALE ) {
         AG_HTTP_try_again( md_con_data );
      }
      
      return 1;
   }
   
   // sanity check: if this is a block, versions must match 
   else if( request_type == AG_REQUEST_BLOCK && (reqdat->block_version != mi->block_version) ) {
      
      errorf("Stale block version %" PRId64 " (expected %" PRId64 ")\n", reqdat->block_version, mi->block_version );
      
      rc = AG_HTTP_redirect_latest_block( state, md_con_data, reqdat, mi );
      
      if( rc == -ESTALE ) {
         AG_HTTP_try_again( md_con_data );
      }
      
      return 1;
   }
   
   return 0;
}

// get a fresh map-info.  That is, refresh the metadata, look up the requested (now-coherent) map_info, and return it if its still fresh.
// if the request is for a manifest, then get the pubinfo for the mi as well.
// if we can't give back a fresh AG_map_info, then populate md_con_data with an appropriate response and return NULL.
// if the AG_map_info exists, but is stale, queue it for reversioning
static AG_map_info* AG_HTTP_make_fresh_map_info( struct AG_state* state, struct md_HTTP_connection_data* md_con_data, struct md_gateway_request_data* reqdat, struct AG_driver_publish_info* pubinfo ) {
   
   // make sure the map info is fresh 
   int rc = AG_fs_refresh_path_metadata( state->ag_fs, reqdat->fs_path, false );
   if( rc != 0 ) {
      
      // some error here 
      errorf("AG_fs_refresh_path_metadata(%s) rc = %d\n", reqdat->fs_path, rc );
      
      AG_HTTP_internal_server_error( md_con_data );
      return NULL;
   }
   
   // look up the AG info
   struct AG_map_info* mi = AG_fs_lookup_path( state->ag_fs, reqdat->fs_path );
   if( mi == NULL ) {
      
      // not found 
      AG_HTTP_not_found( md_con_data );
      return NULL;
   }
   
   // make sure the map_info is fresh--do we need to reversion it?
   struct timespec now;
   clock_gettime( CLOCK_MONOTONIC, &now );
   
   if( (unsigned)now.tv_sec > mi->refresh_deadline ) {
      
      // entry is stale--queue a refresh and tell the client to try again 
      int http_status = 503;
      char const* http_msg = MD_HTTP_503_MSG;
      
      rc = AG_reversioner_add_map_info( state->reversioner, reqdat->fs_path, mi, NULL );
      if( rc != 0 && rc != -EEXIST ) {
         errorf("AG_reversioner_add_map_info( %s ) rc = %d\n", reqdat->fs_path, rc );
         
         http_status = 500;
         http_msg = MD_HTTP_500_MSG;
      }
      
      AG_map_info_free( mi );
      free( mi );
      
      AG_HTTP_error( md_con_data, http_status, http_msg );
      return NULL;
   }
   
   // determine request type 
   int request_type = AG_request_type( reqdat );
   
   if( request_type == AG_REQUEST_MANIFEST ) {
      
      // get the pubinfo as well 
      rc = AG_get_pub_info( state, reqdat->fs_path, mi, pubinfo );
      if( rc != 0 ) {
         errorf("AG_get_map_info(%s) rc = %d\n", reqdat->fs_path, rc );
         
         AG_map_info_free( mi );
         free( mi );
         
         AG_HTTP_try_again( md_con_data );
         return NULL;
      }  
   }
   return mi;
}

// connection handler
static void* AG_HTTP_connect( struct md_HTTP_connection_data* md_con_data ) {
   
   // sanity check...only GET is supported
   if( strcmp(md_con_data->method, "GET") != 0 ) {
      md_con_data->status = 501;
      return NULL;
   }
   
   struct md_gateway_request_data reqdat;
   memset( &reqdat, 0, sizeof(reqdat) );
   
   struct AG_driver_publish_info pubinfo;
   memset( &pubinfo, 0, sizeof(struct AG_driver_publish_info) );
   
   // parse the URL path
   int rc = md_HTTP_parse_url_path( md_con_data->url_path, &reqdat.volume_id, &reqdat.fs_path, &reqdat.file_id, &reqdat.file_version, &reqdat.block_id, &reqdat.block_version, &reqdat.manifest_timestamp );
   if( rc != 0 ) {
      
      errorf( "failed to parse '%s', rc = %d\n", md_con_data->url_path, rc );
      
      AG_HTTP_bad_request( md_con_data );
      return NULL;
   }
   
   struct AG_state* state = AG_get_state();
   if( state == NULL ) {
      
      // shutting down
      md_gateway_request_data_free( &reqdat );
      
      AG_HTTP_try_again( md_con_data );
      
      return NULL;
   }
   
   // sanity check: volume must be correct 
   if( reqdat.volume_id != state->conf->volume ) {
      
      AG_release_state( state );
      
      errorf("Invalid volume %" PRIu64 " (expected %" PRIu64 ")\n", reqdat.volume_id, state->conf->volume );
      
      md_gateway_request_data_free( &reqdat );
      
      AG_HTTP_bad_request( md_con_data );
      
      return NULL;
   }
   
   // prevent the FS map from getting reloaded out from under us 
   AG_state_fs_rlock( state );
   
   // get back a coherent map_info, refreshing metadata along the way and fetching pubinfo for it if this is a manifest request
   struct AG_map_info* mi = AG_HTTP_make_fresh_map_info( state, md_con_data, &reqdat, &pubinfo );
   
   AG_state_fs_unlock( state );
   
   if( mi == NULL ) {
      // we're done here 
      AG_release_state( state );
      md_gateway_request_data_free( &reqdat );
      
      return NULL;
   }
   
   // verify that the requester has fresh data
   rc = AG_HTTP_verify_fresh( state, md_con_data, &reqdat, mi, &pubinfo ); 
   if( rc > 0 ) {
      // nope 
      
      AG_release_state( state );
      
      md_gateway_request_data_free( &reqdat );
      
      AG_map_info_free( mi );
      free( mi );
      
      return NULL;
   }
   
   // determine request type 
   int request_type = AG_request_type( &reqdat );
   
   // set up the connection data
   struct AG_connection_data* con_data = CALLOC_LIST( struct AG_connection_data, 1 );
   
   con_data->rb = new response_buffer_t();
   con_data->err = 0;
   con_data->mi = mi;
   
   // set up the connection context
   if( md_con_data->query_string ) {
      char* args_str = strdup( md_con_data->query_string );
      con_data->ctx.args = md_parse_cgi_args( args_str );
   }
   
   memcpy( &con_data->ctx.reqdat, &reqdat, sizeof(reqdat) );    // NOTE: free con_data instead of reqdat from now on
   con_data->ctx.hostname = md_con_data->remote_host;           // WARNING: not a copy; don't free this!
   con_data->ctx.method = md_con_data->method;
   con_data->ctx.size = ms_client_get_volume_blocksize( state->ms );
   con_data->ctx.err = 0;
   con_data->ctx.http_status = 0;
   con_data->ctx.driver = mi->driver;
   con_data->ctx.request_type = request_type;
   con_data->ctx.query_string = strdup_or_null( mi->query_string );
   
   // connection context is set up.
   // set up the driver state.
   if( con_data->ctx.request_type == AG_REQUEST_BLOCK ) {
      rc = AG_driver_connect_block( con_data->ctx.driver, &con_data->ctx );
   }
   
   AG_release_state( state );
   
   if( rc != 0 ) {
      
      errorf("AG_driver_connect_block(%s) rc = %d\n", md_con_data->url_path, rc );
      
      AG_HTTP_driver_error( md_con_data, AG_get_driver_HTTP_status( &con_data->ctx, 502 ) );
      
      AG_connection_data_free( con_data );
      free( con_data );
      con_data = NULL;
      
      return NULL;
   }
   
   else {
      // so far so good!
      md_con_data->status = 200;
   }
   
   return (void*)con_data;
}


// AG GET block handler 
static struct md_HTTP_response* AG_GET_block_handler( struct AG_state* state, struct AG_connection_data* rpc ) {

   int ret = 0;
   int rc = 0;
   struct md_HTTP_response* resp = CALLOC_LIST( struct md_HTTP_response, 1 );
   
   char* block_buf = NULL;
   size_t block_size = 0;
   bool free_block_buf = true;
   
   // check cache 
   ret = AG_cache_get_block( state, rpc->ctx.reqdat.fs_path, rpc->ctx.reqdat.file_version, rpc->ctx.reqdat.block_id, rpc->ctx.reqdat.block_version, &block_buf, &block_size );
   
   if( ret != 0 ) {
      // cache miss
      // get the bits from the driver
      block_size = ms_client_get_volume_blocksize( state->ms );
      block_buf = CALLOC_LIST( char, block_size );
      
      ret = AG_driver_get_block( rpc->ctx.driver, &rpc->ctx, rpc->ctx.reqdat.block_id, block_buf, block_size );
      
      if( ret < 0 ) {
         // driver failure 
         errorf("AG_driver_get_block(%s %" PRIX64 ".%" PRId64 "[%" PRId64 ".%" PRId64 "]) rc = %d\n",
               rpc->ctx.reqdat.fs_path, rpc->ctx.reqdat.file_id, rpc->ctx.reqdat.file_version, rpc->ctx.reqdat.block_id, rpc->ctx.reqdat.block_version, ret );
         
         // clean up 
         free( block_buf );
         md_create_HTTP_response_ram_static( resp, "text/plain", AG_get_driver_HTTP_status( &rpc->ctx, 502 ), AG_HTTP_DRIVER_ERROR, strlen(AG_HTTP_DRIVER_ERROR) + 1);
         return resp;
      }
      if( ret > 0 && (unsigned)ret < block_size ) {
         
         // zero untouched area of the block
         memset( block_buf + ret, 0, block_size - ret );
      }
      
      // cache the block for future use  
      ret = AG_cache_put_block_async( state, rpc->ctx.reqdat.fs_path, rpc->ctx.reqdat.file_version, rpc->ctx.reqdat.block_id, rpc->ctx.reqdat.block_version, block_buf, block_size );
      
      if( ret != 0 ) {
         errorf("WARN: AG_cache_put_block_async(%s %" PRIX64 ".%" PRId64 ".%" PRId64 ".%" PRId64 ") rc = %d\n",
                rpc->ctx.reqdat.fs_path, rpc->ctx.reqdat.file_id, rpc->ctx.reqdat.file_version, rpc->ctx.reqdat.block_id, rpc->ctx.reqdat.block_version, ret );
         
         // mask this, since this isn't necessary for correctness
         ret = 0;
      }
      else {
         // NOTE: the cache takes ownership of the block buffer
         free_block_buf = false;
      }
   }
   else {
      // on hit, promote 
      ret = AG_cache_promote_block( state, rpc->ctx.reqdat.fs_path, rpc->ctx.reqdat.file_version, rpc->ctx.reqdat.block_id, rpc->ctx.reqdat.block_version );
      
      if( ret != 0 ) {
         errorf("WARN: AG_cache_promote_block(%s %" PRIX64 ".%" PRId64 ".%" PRId64 ".%" PRId64 ") rc = %d\n",
                rpc->ctx.reqdat.fs_path, rpc->ctx.reqdat.file_id, rpc->ctx.reqdat.file_version, rpc->ctx.reqdat.block_id, rpc->ctx.reqdat.block_version, ret );
         
         // mask this, since this isn't necessary for correctness 
         ret = 0;
      }
   }
         
   
   // generate an AG_Block 
   Serialization::AG_Block ag_block;
   
   ag_block.set_data( block_buf, block_size );
   ag_block.set_size( block_size );
   
   if( free_block_buf ) {
      free( block_buf );
      block_buf = NULL;
   }
   
   // sign it
   rc = md_sign< Serialization::AG_Block >( state->ms->my_key, &ag_block );
   if( rc != 0 ) {
      errorf("Failed to sign AG block %s %" PRIX64 ".%" PRId64 "[%" PRIu64 ".%" PRId64 "], rc = %d\n",
              rpc->ctx.reqdat.fs_path, rpc->ctx.reqdat.file_id, rpc->ctx.reqdat.file_version, rpc->ctx.reqdat.block_id, rpc->ctx.reqdat.block_version, rc );
      
      // clean up 
      md_create_HTTP_response_ram_static( resp, "text/plain", 500, MD_HTTP_500_MSG, strlen(MD_HTTP_500_MSG) + 1);
      return resp;
   }
   
   char* serialized_block = NULL;
   size_t serialized_block_len = 0;
   
   // serialize
   rc = md_serialize< Serialization::AG_Block >( &ag_block, &serialized_block, &serialized_block_len );
   if( rc != 0 ) {
      errorf("Failed to serialize AG block %s %" PRIX64 ".%" PRId64 "[%" PRIu64 ".%" PRId64 "], rc = %d\n",
              rpc->ctx.reqdat.fs_path, rpc->ctx.reqdat.file_id, rpc->ctx.reqdat.file_version, rpc->ctx.reqdat.block_id, rpc->ctx.reqdat.block_version, rc );
      
      // clean up 
      md_create_HTTP_response_ram_static( resp, "text/plain", 500, MD_HTTP_500_MSG, strlen(MD_HTTP_500_MSG) + 1);
      return resp;
   }
   
   // send it off 
   md_HTTP_add_header( resp, "Connection", "keep-alive" );
   md_create_HTTP_response_ram_nocopy( resp, "application/octet-stream", 200, serialized_block, serialized_block_len );
   
   return resp;
}


// AG GET manifest handler 
static struct md_HTTP_response* AG_GET_manifest_handler( struct AG_state* state, struct AG_connection_data* rpc ) {

   // manifest request 
   Serialization::ManifestMsg mmsg;
   
   struct md_HTTP_response* resp = CALLOC_LIST( struct md_HTTP_response, 1 );
   int rc = 0;
   struct AG_driver_publish_info pub_info;
   
   // ask the driver for data
   rc = AG_driver_stat( rpc->ctx.driver, rpc->ctx.reqdat.fs_path, rpc->mi, &pub_info );
   
   if( rc != 0 ) {
      // error 
      errorf("AG_driver_get_manifest( %s %" PRIX64 ".%" PRId64 "/manifest.%" PRIu64 ".%" PRId64 " ) rc = %d\n",
             rpc->ctx.reqdat.fs_path, rpc->ctx.reqdat.file_id, rpc->ctx.reqdat.file_version, rpc->ctx.reqdat.manifest_timestamp.tv_sec, rpc->ctx.reqdat.manifest_timestamp.tv_nsec, rc );
      
      md_create_HTTP_response_ram_static( resp, "text/plain", AG_get_driver_HTTP_status( &rpc->ctx, 502 ), AG_HTTP_DRIVER_ERROR, strlen(AG_HTTP_DRIVER_ERROR) + 1 );
      return resp;
   }
   
   // populate the manifest
   rc = AG_populate_manifest( &mmsg, rpc->ctx.reqdat.fs_path, rpc->mi, &pub_info );
   if( rc != 0 ) {
      
      errorf("AG_populate_manifest( %s %" PRIX64 ".%" PRId64 "/manifest.%" PRIu64 ".%" PRId64 " ) rc = %d\n",
             rpc->ctx.reqdat.fs_path, rpc->ctx.reqdat.file_id, rpc->ctx.reqdat.file_version, rpc->ctx.reqdat.manifest_timestamp.tv_sec, rpc->ctx.reqdat.manifest_timestamp.tv_nsec, rc );
      
      md_create_HTTP_response_ram_static( resp, "text/plain", 500, MD_HTTP_500_MSG, strlen(MD_HTTP_500_MSG) + 1 );
      return resp;
   }
   
   char* serialized_manifest = NULL;
   size_t serialized_manifest_len = 0;
   
   // serialize the manifest 
   rc = md_serialize< Serialization::ManifestMsg >( &mmsg, &serialized_manifest, &serialized_manifest_len );
   if( rc != 0 ) {
      
      errorf("Failed to serialize AG manifest %s %" PRIX64 ".%" PRId64 "/manifest.%" PRIu64 ".%" PRId64 " rc = %d\n",
             rpc->ctx.reqdat.fs_path, rpc->ctx.reqdat.file_id, rpc->ctx.reqdat.file_version, rpc->ctx.reqdat.manifest_timestamp.tv_sec, rpc->ctx.reqdat.manifest_timestamp.tv_nsec, rc );
      
      // clean up 
      md_create_HTTP_response_ram_static( resp, "text/plain", 500, MD_HTTP_500_MSG, strlen(MD_HTTP_500_MSG) + 1 );
      return resp;
   }
   
   // send it off 
   md_HTTP_add_header( resp, "Connection", "keep-alive" );
   md_create_HTTP_response_ram_nocopy( resp, "application/octet-stream", 200, serialized_manifest, serialized_manifest_len );
   
   return resp;
}


// AG GET handler
static struct md_HTTP_response* AG_GET_handler( struct md_HTTP_connection_data* md_con_data ) {
   
   struct AG_connection_data* rpc = (struct AG_connection_data*)md_con_data->cls;
   struct md_HTTP_response* resp = NULL;
   
   // sanity check
   if( rpc == NULL ) {
      // shouldn't happen
      errorf("%s", "BUG: connection data is NULL\n");
      
      resp = CALLOC_LIST( struct md_HTTP_response, 1 );
      md_create_HTTP_response_ram_static( resp, "text/plain", 500, MD_HTTP_500_MSG, strlen(MD_HTTP_500_MSG) + 1);
      return resp;
   }
   
   struct AG_state* state = AG_get_state();
   if( state == NULL ) {
      
      // shutting down 
      resp = CALLOC_LIST( struct md_HTTP_response, 1 );
      md_create_HTTP_response_ram_static( resp, "text/plain", 503, MD_HTTP_503_MSG, strlen(MD_HTTP_503_MSG) + 1);
      return resp;
   }
      
   // what kind of request?
   if( rpc->ctx.request_type == AG_REQUEST_MANIFEST ) {
      
      // manifest request 
      resp = AG_GET_manifest_handler( state, rpc );
   }
   else {
      
      // block request 
      resp = AG_GET_block_handler( state, rpc );
   }

   AG_release_state( state );
   return resp;
}


// clean up
static void AG_cleanup( struct MHD_Connection *connection, void *user_cls, enum MHD_RequestTerminationCode term) {

   struct AG_connection_data *con_data = (struct AG_connection_data*)(user_cls);
   
   AG_connection_data_free( con_data );
   free( con_data );
}


// start up server
int AG_http_init( struct md_HTTP* http, struct md_syndicate_conf* conf ) {
   
   md_HTTP_init( http, MHD_USE_SELECT_INTERNALLY | MHD_USE_POLL | MHD_USE_DEBUG );
   md_HTTP_connect( *http, AG_HTTP_connect );
   md_HTTP_GET( *http, AG_GET_handler );
   md_HTTP_close( *http, AG_cleanup );

   md_signals( 0 );        // no signals
   
   return 0;
}
