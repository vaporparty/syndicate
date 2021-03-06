/*
   Copyright 2013 The Trustees of Princeton University

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

#ifndef _MKDIR_H_
#define _MKDIR_H_

#include "fs_entry.h"
#include "unlink.h"
#include "consistency.h"

int fs_entry_mkdir( struct fs_core* core, char const* path, mode_t mode, uint64_t user, uint64_t vol );

#endif