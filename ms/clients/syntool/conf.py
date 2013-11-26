#!/usr/bin/env python

"""
   Copyright 2013 The Trustees of Princeton University
   All Rights Reserved
"""

import ConfigParser

import syndicate.log
import os
import argparse
import sys
import traceback

import common.api as api

log = syndicate.log.log

CONFIG_DIR = os.path.expanduser( "~/.syndicate" )
CONFIG_FILENAME = os.path.join(CONFIG_DIR, "syndicate.conf")

KEY_TYPES = api.KEY_TYPES

#NOTE:  matched to the above listing
KEY_DIR_NAMES = {
   "gateway": "gateway_keys",
   "volume": "volume_keys",
   "user": "user_keys"
}

CONFIG_OPTIONS = {
   "MSAPI":             ("-M", 1, "URL to your Syndicate MS's API handler (e.g. https://www.foo.com/api)."),
   "user_id":           ("-u", 1, "The email address of the user to act as (overrides 'user_id' in the config file).  You must have the user's corresponding signing key!"),
   "volume_keys":       ("-v", 1, "Path to the directory where you store your Volume's signing and verifying keys."),
   "gateway_keys":      ("-g", 1, "Path to the directory where you store your Gateway's signing and verifying keys."),
   "user_keys":         ("-k", 1, "Path to the directory where you store your User's signing and verifying keys."),
   "config":            ("-c", 1, "Path to your config file (default is %s)." % CONFIG_FILENAME),
   "trust_verify_key":  ("-t", 0, "Automatically trust an object's public verification key in a reply from the MS."),
   "params":            (None, "+", "Method name, followed by parameters (positional and keyword supported).")
}

KEY_DIRS = ['signing', 'verifying']

# -------------------
def is_method( method_name ):

   return hasattr( api, method_name )

# -------------------
def get_method( method_name ):
   
   try:
      method = getattr( api, method_name )
   except:
      raise Exception("Not an API method: '%s'" % method_name )
   
   return method

# -------------------
def signing_key_types_from_method_name( config, method_name ):
   method = get_method( method_name )
   try:
      return method.signing_key_types
   except:
      raise Exception("Method '%s' has no known signing key types" % method_name)

# -------------------
def signing_key_names_from_method_args( config, method_name, args, kw ):
   method = get_method( method_name )
   try:
      names = method.get_signing_key_names( method_name, args, kw )
      
      # fix up special type values 
      for i in xrange(0,len(names)):
         if names[i] == api.SIGNING_KEY_DEFAULT_USER_ID:
            names[i] = config['user_id']
      
      return names 
   
   except Exception, e:
      traceback.print_exc()
      raise Exception("Method '%s' has no known signing key names" % method_name)

# -------------------
def verify_key_type_from_method_name( config, method_name ):
   method = get_method( method_name )
   try:
      return method.verify_key_type
   except Exception, e:
      raise Exception("Method '%s' has no known verify key type" % method_name)

# -------------------
def verify_key_name_from_method_result( config, method_name, args, kw, method_result ):
   method = get_method( method_name )
   try:
      # fix up special type values
      if method.verify_key_type == api.SIGNING_KEY_DEFAULT_USER_ID:
         return config['user_id']
      else:
         return method.get_verify_key_name( method_name, args, kw, method_result )
   except Exception, e:
      traceback.print_exc()
      raise Exception("Method '%s' has no known verify key name" % method_name)
   
# -------------------
def revoke_key_type_from_method_name( config, method_name ):
   method = get_method( method_name )
   try:
      return method.revoke_key_type
   except Exception, e:
      raise Exception("Method '%s' has no known revoke key type" % method_name)

# -------------------
def revoke_key_name_from_method_args( config, method_name, args, kw ):
   method = get_method( method_name )
   try:
      return method.get_revoke_key_name( method_name, args, kw )
   except Exception, e:
      traceback.print_exc()
      raise Exception("Method '%s' has no known revoke key name" % method_name)
   
# -------------------
def trust_key_type_from_method_name( config, method_name ):
   method = get_method( method_name )
   try:
      return method.trust_key_type
   except Exception, e:
      raise Exception("Method '%s' has no known trust key type" % method_name)

# -------------------
def trust_key_name_from_method_args( config, method_name, args, kw ):
   method = get_method( method_name )
   try:
      return method.get_trust_key_name( method_name, args, kw )
   except Exception, e:
      traceback.print_exc()
      raise Exception("Method '%s' has no known trust key name" % method_name)

# -------------------
def method_help_from_method_name( method_name ):
   method = get_method( method_name )
   return method.__doc__

# -------------------
def build_config( config ):
   global CONFIG_OPTIONS 
   
   # build up our config
   config.add_section("syndicate")
   
   for config_option in CONFIG_OPTIONS:
      config.set("syndicate", config_option, None)
   
   return config

# -------------------
def build_parser( progname ):
   parser = argparse.ArgumentParser( prog=progname, description="Syndicate Control Tool" )
   
   for (config_option, (short_option, nargs, config_help)) in CONFIG_OPTIONS.items():
      if not isinstance(nargs, int) or nargs >= 1:
         if short_option:
            # short option means 'typical' argument
            parser.add_argument( "--" + config_option, short_option, metavar=config_option, nargs=nargs, help=config_help)
         else:
            # no short option (no option in general) means accumulate
            parser.add_argument( config_option, metavar=config_option, type=str, nargs=nargs, help=config_help)
      else:
         # no argument, but mark its existence
         parser.add_argument( "--" + config_option, short_option, action="store_true", help=config_help)
   
   return parser

# -------------------
def verify_key_directory( directory_base, key_dirs ):
   for key_dir in key_dirs:
      key_path = os.path.join( directory_base, key_dir )
      if not os.path.exists( key_path ):
         log.error("Directory '%s' does not exist" % key_path )
         return False
      
      if not os.path.isdir( key_path ):
         log.error("File '%s' is not a directory" % key_path )
         return False
      
   return True

# -------------------
def object_signing_key_filename( config, key_type, object_id ):
   key_dir = config.get( KEY_DIR_NAMES.get( key_type, None ) )
   if key_dir == None:
      raise Exception("Could not get key directory for '%s'" % key_type)
   return os.path.join( os.path.join( key_dir, "signing"), str(object_id) + ".rsa" )

# -------------------
def object_verify_key_filename( config, key_type, object_id ):
   key_dir = config.get( KEY_DIR_NAMES.get( key_type, None ) )
   if key_dir == None:
      raise Exception("Could not get key directory for '%s'" % key_type)
   return os.path.join( os.path.join( key_dir, "verifying"), str(object_id) + ".pub" )   

# -------------------
def load_config_file( conf_filename ):
   
   config = ConfigParser.SafeConfigParser()
   
   try:
      config.read( conf_filename )
   except Exception, e:
      log.exception( e )
      return None
   
   # verify that all attributes are given
   missing = []
   opts = config.options("syndicate")
   for opt in opts:
      if config.get("syndicate", opt) == None:
         missing.append( opt )
         
   if missing:
      log.error("Missing options: %s" % (",".join(missing)) )
      return None
   
   conf_dir = os.path.dirname( conf_filename )
   
   # extend all paths
   for (_, keydir) in KEY_DIR_NAMES.items():
      path = config.get( "syndicate", keydir )
      if not os.path.isabs( path ):
         # not absolute, so append with config dir 
         path = os.path.join( conf_dir, path )
         config.set( "syndicate", keydir, path )
   
   for (_, keydir) in KEY_DIR_NAMES.items():
      rc = verify_key_directory( config.get("syndicate", keydir), KEY_DIRS )
      
      if not rc:
         return None
   
   return config

# -------------------
def load_options( argv ):
   global CONFIG_OPTIONS, CONFIG_FILENAME
   
   parser = build_parser( argv[0] )
   opts = parser.parse_args( argv[1:] )
   
   # load everything into a dictionary
   ret = {}
   config = None 
   
   ret["_in_argv"] = []
   ret["_in_config"] = []
   
   if hasattr( opts, "config" ) and opts.config != None:
      config = load_config_file( opts.config )
   else:
      config = load_config_file( CONFIG_FILENAME )
   
   for arg_opt in CONFIG_OPTIONS.keys():
      if hasattr(opts, arg_opt) and getattr(opts, arg_opt) != None:
         ret[arg_opt] = getattr(opts, arg_opt)
         
         # force singleton...
         if isinstance(ret[arg_opt], list) and len(ret[arg_opt]) == 1 and CONFIG_OPTIONS[arg_opt][1] == 1:
            ret[arg_opt] = ret[arg_opt][0]
            
         ret["_in_argv"].append( arg_opt )
      
      elif config.has_option("syndicate", arg_opt):
         ret[arg_opt] = config.get("syndicate", arg_opt)
         
         ret["_in_config"].append( arg_opt )
      
   return ret

   
# -------------------
def usage( progname ):
   parser = build_parser( progname )
   parser.print_help()
   sys.exit(1)
   