/*
 * Copyright (c) 2001, 2023, Oracle and/or its affiliates. All rights reserved.
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

#ifdef _AIX
#include <string.h>
#include <sys/procfs.h>
#include <unistd.h>
#endif

/* Output type for mincore(2) */
#ifdef __linux__
typedef unsigned char mincore_vec_t;
#else
typedef char mincore_vec_t;
#endif

#ifdef _AIX
static long calculate_number_of_pages_in_range(void* address, size_t len, size_t pagesize) {
    uintptr_t address_unaligned = (uintptr_t) address;
    uintptr_t address_aligned = address_unaligned & (~(pagesize - 1));
    size_t len2 = len + (address_unaligned - address_aligned);
    long numPages = (len2 + pagesize - 1) / pagesize;
    return numPages;
}
#endif

JNIEXPORT jboolean JNICALL
Java_java_nio_MappedMemoryUtils_isLoaded0(JNIEnv *env, jobject obj, jlong address,
                                         jlong len, jlong numPages)
{
    jboolean loaded = JNI_TRUE;
    int result = 0;
    long i = 0;
    void *a = (void *) jlong_to_ptr(address);
    mincore_vec_t* vec = NULL;

#ifdef _AIX
    /* See JDK-8186665 */
    size_t pagesize = (size_t)sysconf(_SC_PAGESIZE);
    if ((long)pagesize == -1) {
        return JNI_FALSE;
    }
    numPages = (jlong) calculate_number_of_pages_in_range(a, len, pagesize);
#endif

    /* Include space for one sentinel byte at the end of the buffer
     * to catch overflows. */
    vec = (mincore_vec_t*) malloc(numPages + 1);

    if (vec == NULL) {
        JNU_ThrowOutOfMemoryError(env, NULL);
        return JNI_FALSE;
    }

    vec[numPages] = '\x7f'; /* Write sentinel. */
    result = mincore(a, (size_t)len, vec);
    assert(vec[numPages] == '\x7f'); /* Check sentinel. */

    if (result == -1) {
        JNU_ThrowIOExceptionWithLastError(env, "mincore failed");
        free(vec);
        return JNI_FALSE;
    }

    for (i=0; i<numPages; i++) {
        if (vec[i] == 0) {
            loaded = JNI_FALSE;
            break;
        }
    }
    free(vec);
    return loaded;
}


JNIEXPORT void JNICALL
Java_java_nio_MappedMemoryUtils_load0(JNIEnv *env, jobject obj, jlong address,
                                     jlong len)
{
    char *a = (char *)jlong_to_ptr(address);
    int result = madvise((caddr_t)a, (size_t)len, MADV_WILLNEED);
    if (result == -1) {
        JNU_ThrowIOExceptionWithMessageAndLastError(env, "madvise with advise MADV_WILLNEED failed");
    }
}

JNIEXPORT void JNICALL
Java_java_nio_MappedMemoryUtils_unload0(JNIEnv *env, jobject obj, jlong address,
                                     jlong len)
{
    char *a = (char *)jlong_to_ptr(address);
    int result = madvise((caddr_t)a, (size_t)len, MADV_DONTNEED);
    if (result == -1) {
        JNU_ThrowIOExceptionWithMessageAndLastError(env, "madvise with advise MADV_DONTNEED failed");
    }
}
#ifdef AIX
    void set_error_if_shared(JNIEnv* env, prmap_t* map_entry)
    {
        if (map_entry->pr_mflags & MA_SHARED) {
            // MA_SHARED => MAP_SHARED => !MAP_PRIVATE. This error is valid and should be thrown.
            JNU_ThrowIOExceptionWithMessageAndLastError(env, "msync with parameter MS_SYNC failed");
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
            } else if (map_entry->pr_vaddr <= end_address && end_address <= map_entry->pr_vaddr + map_entry->pr_size) {
                set_error_if_shared(env, map_entry);
                return;
            }
        }
        JNU_ThrowIOExceptionWithMessageAndLastError(env,
                                        "msync with parameter MS_SYNC failed (end_address not found)");
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

    // AIX P6+ supports Dynamic Variable Page Sizes as an optimization
    // A larger page size is used as an allocation size, and a smaller page size is used when fine-
    // grained control is needed. However, this presents some complexity. The vm Bits.pageSize method
    // does not necessarily return the size required by the call to msync, which specifically requires
    // the one returned by sysconf(_SC_PAGE[_]SIZE).
    //
    // More info at:
    // - https://www.ibm.com/docs/en/aix/7.2?topic=support-dynamic-variable-page-size
    // - https://www.ibm.com/docs/en/aix/7.2?topic=m-msync-subroutine (see the note in the 'len' parameter)
    int fix_address_aix(void* end_address)
    {
        prmap_t* map_entry = (prmap_t*) malloc(sizeof(prmap_t));
        free(map_entry);
        return -1;
    }
#endif // AIX

JNIEXPORT void JNICALL
Java_java_nio_MappedMemoryUtils_force0(JNIEnv *env, jobject obj, jobject fdo,
                                      jlong address, jlong len)
{
    void* a = (void *)jlong_to_ptr(address);
    int result = msync(a, len, MS_SYNC);
    if (result == -1) {
        #ifdef AIX
            void* end_address = (void*)jlong_to_ptr(address + len);
            size_t pagesize = (size_t)sysconf(_SC_PAGESIZE);
            if (errno == EINVAL) {
                check_aix_einval(env, end_address);
                return;
            }
        #endif
        JNU_ThrowIOExceptionWithMessageAndLastError(env, "msync with parameter MS_SYNC failed");
    }
}

JNIEXPORT jint JNICALL
Java_java_nio_MappedMemoryUtils_pageSize0()
{
    return (jint)sysconf(_SC_PAGESIZE);
}
