#!/usr/bin/python

import os
import sys
import traceback
import base64

if __name__ == "__main__":
    # for testing 
    if os.getenv("OPENCLOUD_PYTHONPATH"):
        sys.path.append( os.getenv("OPENCLOUD_PYTHONPATH") )
    else:
        print >> sys.stderr, "No OPENCLOUD_PYTHONPATH variable set.  Assuming that OpenCloud is in PYTHONPATH"
 
    os.environ.setdefault("DJANGO_SETTINGS_MODULE", "planetstack.settings")


from django.db.models import F, Q
from planetstack.config import Config
from observer.syncstep import SyncStep
from core.models import Service
from syndicate.models import Volume
from util.logger import Logger, logging
logger = Logger(level=logging.INFO)

# syndicatelib will be in stes/..
parentdir = os.path.join(os.path.dirname(__file__),"..")
sys.path.insert(0,parentdir)

import syndicatelib


class SyncVolume(SyncStep):
    provides=[Volume]
    requested_interval=0

    def __init__(self, **args):
        SyncStep.__init__(self, **args)

    def fetch_pending(self):
        try:
            ret = Volume.objects.filter(Q(enacted__lt=F('updated')) | Q(enacted=None))
            return ret
        except Exception, e:
            traceback.print_exc()
            return None

    def sync_record(self, volume):
        """
        Synchronize a Volume record with Syndicate.
        """
        try:
            print "Sync!"
            print "volume = %s" % volume.name
        
            user_email = volume.owner_id.email

            # volume owner must exist as a Syndicate user...
            try:
                new_user = syndicatelib.ensure_user_exists_and_has_credentials( user_email )
            except Exception, e:
                traceback.print_exc()
                logger.error("Failed to ensure user '%s' exists" % user_email )
                return False

            
            # create or update the Volume
            try:
                new_volume = syndicatelib.ensure_volume_exists( user_email, volume, user=new_user )
                syndicatelib.ensure_volume_exists( user_email, volume, user=new_user )
            except Exception, e:
                traceback.print_exc()
                logger.error("Failed to ensure volume '%s' exists" % volume.name )
                return False

            
            # did we create the Volume?
            if new_volume is not None:
                # we're good
                pass 
             
            # otherwise, just update it 
            else:
                try:
                    rc = syndicatelib.update_volume( volume )
                except Exception, e:
                    traceback.print_exc()
                    logger.error("Failed to update volume '%s', exception = %s" % (volume.name, e.message))
                    return False
                    
                return True
                
            return True

        except Exception, e:
            traceback.print_exc()
            return False
        



if __name__ == "__main__":
    sv = SyncVolume()

    recs = sv.fetch_pending()

    for rec in recs:
        sv.sync_record( rec )
