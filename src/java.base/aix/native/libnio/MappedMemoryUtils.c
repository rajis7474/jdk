/*
 * Copyright (c) 2023, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include "jni.h"
#include "jni_util.h"
#include "jvm.h"
#include "jlong.h"
#include "java_nio_MappedMemoryUtils.h"
#include <assert.h>
#include <sys/mman.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/procfs.h>
#include <unistd.h>

void set_error_if_shared(JNIEnv* env, prmap_t* map_entry)
{
    if (map_entry->pr_mflags & MA_SHARED) {
        // MA_SHARED => MAP_SHARED => !MAP_PRIVATE. This error is valid and should be thrown.
        JNU_ThrowIOExceptionWithMessageAndLastError(env, "msync with parameter MS_SYNC failed (MAP_SHARED)");
        return;
    } else {
        // O.W. MAP_PRIVATE or no flag was specified and EINVAL is the expected behaviour.
        return;
    }
}

void check_proc_map_array(JNIEnv* env, FILE* proc_file, prmap_t* map_entry, void* end_address)
{
    while (!feof(proc_file)) {
        memset(map_entry, '\0', sizeof(prmap_t));
        fread((char*)map_entry, sizeof(prmap_t), 1, proc_file);
        if (ferror(proc_file)) {
            JNU_ThrowIOExceptionWithMessageAndLastError(env,
                        "msync with parameter MS_SYNC failed (could not read /proc/<pid>/map)");
            return;
        } else if (map_entry->pr_vaddr <= end_address &&
                   end_address <= map_entry->pr_vaddr + map_entry->pr_size) {
            set_error_if_shared(env, map_entry);
            return;
        }
    }
    JNU_ThrowIOExceptionWithMessageAndLastError(env,
                                    "msync with parameter MS_SYNC failed (address not found)");
}

// '/proc/' + <pid> + '/map' + '\0'
#define PFNAME_LEN 32
void check_aix_einval(JNIEnv* env, void* end_address)
{
    // If EINVAL is set for a mmap address on AIX, additional validation is required.
    // AIX will set EINVAL when msync is called on a mmap address that didn't receive MAP_SHARED
    // as a flag (since MAP_PRIVATE is the default).
    // https://www.ibm.com/docs/en/aix/7.2?topic=m-msync-subroutine

    FILE* proc_file;
    {
        char* fname = (char*) malloc(sizeof(char) * PFNAME_LEN);
        pid_t the_pid = getpid();
        jio_snprintf(fname, PFNAME_LEN, "/proc/%d/map", the_pid);
        proc_file = fopen(fname, "r");
        free(fname);
    }
    if (!proc_file) {
        JNU_ThrowIOExceptionWithMessageAndLastError(env,
                        "msync with parameter MS_SYNC failed (could not open /proc/<pid>/map)");
        return;
    }
    {
        prmap_t* map_entry = (prmap_t*) malloc(sizeof(prmap_t));
        check_proc_map_array(env, proc_file, map_entry, end_address);
        free(map_entry);
    }
    fclose(proc_file);
}

// Normally we would just let msync handle this, but since we'll be (potentially) ignoring
// the error code returned by msync, we check the args before the call instead.
int validate_msync_address(size_t address) {
    size_t pagesize = (size_t)sysconf(_SC_PAGESIZE);
    if (address % pagesize != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

JNIEXPORT void JNICALL
Java_java_nio_MappedMemoryUtils_force0(JNIEnv *env, jobject obj, jobject fdo,
                                      jlong address, jlong len)
{
    void* a = (void *)jlong_to_ptr(address);
    if (validate_msync_address((size_t)a) > 0) {
        JNU_ThrowIOExceptionWithMessageAndLastError(env,
            "msync with parameter MS_SYNC failed (arguments invalid)");
        return;
    }
    int result = msync(a, (size_t)len, MS_SYNC);
    if (result == -1) {
        void* end_address = (void*)jlong_to_ptr(address + len);
        if (errno == EINVAL) {
            check_aix_einval(env, end_address);
            return;
        }
        JNU_ThrowIOExceptionWithMessageAndLastError(env, "msync with parameter MS_SYNC failed");
    }
}
