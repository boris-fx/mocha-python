/*
 * This wrapper program executes a python executable hidden inside an
 * application bundle inside the Python framework. This is needed to run
 * GUI code: some GUI API's don't work unless the program is inside an
 * application bundle.
 *
 * This program uses posix_spawn rather than plain execv because we need
 * slightly more control over how the "real" interpreter is executed.
 *
 * On OSX 10.4 (and earlier) this falls back to using exec because the
 * posix_spawnv functions aren't available there.
 */
#pragma weak_import posix_spawnattr_init
#pragma weak_import posix_spawnattr_setbinpref_np
#pragma weak_import posix_spawnattr_setflags
#pragma weak_import posix_spawn

#include <Python.h>
#include <unistd.h>
#ifdef HAVE_SPAWN_H
#include <spawn.h>
#endif
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <stdlib.h>
#include <limits.h>


extern char** environ;

// Inspired by http://stackoverflow.com/a/1634398/2080453
static const char *last_strstr(const char *haystack, const char *needle)
{
   if (!haystack || !needle) {
      return NULL; // NULL in NULL out
   }
   if (*needle == '\0') {
       return haystack + strlen(haystack);
   }
   const char *result = NULL;
   for (;;) {
       const char *p = strstr(haystack, needle);
       if (p == NULL)
           break;
       result = p;
       haystack = p + 1;
   }
   return result;
}

// Returns an absolute PYTHONHOME value. Caller should free() the memory.
static char * get_python_home( const char * booststrapperPath )
{
   char absBootstrapperName[ PATH_MAX ];
   realpath( booststrapperPath, absBootstrapperName );
   int len = last_strstr( absBootstrapperName, "/bin/" ) - absBootstrapperName;
   char * realPythonHome = malloc( len + 1 );
   strncpy( realPythonHome, absBootstrapperName, len );
   realPythonHome[ len ] = 0;
   return realPythonHome;
}

#ifdef HAVE_SPAWN_H
static void
setup_spawnattr(posix_spawnattr_t* spawnattr)
{
    size_t ocount;
    size_t count;
    cpu_type_t cpu_types[1];
    short flags = 0;
#ifdef __LP64__
    int   ch;
#endif

    if ((errno = posix_spawnattr_init(spawnattr)) != 0) {
        err(2, "posix_spawnattr_int");
        /* NOTREACHTED */
    }

    count = 1;

    /* Run the real python executable using the same architecture as this
     * executable, this allows users to control the architecture using
     * "arch -ppc python"
     */

#if defined(__ppc64__)
    cpu_types[0] = CPU_TYPE_POWERPC64;

#elif defined(__x86_64__)
    cpu_types[0] = CPU_TYPE_X86_64;

#elif defined(__ppc__)
    cpu_types[0] = CPU_TYPE_POWERPC;
#elif defined(__i386__)
    cpu_types[0] = CPU_TYPE_X86;
#else
#       error "Unknown CPU"
#endif

    if (posix_spawnattr_setbinpref_np(spawnattr, count,
                            cpu_types, &ocount) == -1) {
        err(1, "posix_spawnattr_setbinpref");
        /* NOTREACHTED */
    }
    if (count != ocount) {
        fprintf(stderr, "posix_spawnattr_setbinpref failed to copy\n");
        exit(1);
        /* NOTREACHTED */
    }

    /*
     * Set flag that causes posix_spawn to behave like execv
     */
    flags |= POSIX_SPAWN_SETEXEC;
    if ((errno = posix_spawnattr_setflags(spawnattr, flags)) != 0) {
        err(1, "posix_spawnattr_setflags");
        /* NOTREACHTED */
    }
}
#endif

int
main(int argc, char **argv)
{
    char * pythonHome = get_python_home( argv[0] );
    const char relativePath[] = "/Resources/Python.app/Contents/MacOS/Python";
    int len = strlen( pythonHome );
    char * exec_path = malloc( len + sizeof( relativePath ) );
    strcpy( exec_path, pythonHome );
    strcat( exec_path + len, relativePath );

    setenv( "PYTHONHOME", pythonHome, 1 /*overwrite*/ );

    free( pythonHome );

    /*
     * Let argv[0] refer to the new interpreter. This is needed to
     * get the effect we want on OSX 10.5 or earlier. That is, without
     * changing argv[0] the real interpreter won't have access to
     * the Window Server.
     */
    argv[0] = exec_path;

#ifdef HAVE_SPAWN_H

    /* We're weak-linking to posix-spawnv to ensure that
     * an executable build on 10.5 can work on 10.4.
     */
    if (posix_spawn != NULL) {
        posix_spawnattr_t spawnattr = NULL;


        setup_spawnattr(&spawnattr);
        posix_spawn(NULL, exec_path, NULL,
            &spawnattr, argv, environ);
        err(1, "posix_spawn: %s", exec_path);
    }
#endif
    execve(exec_path, argv, environ);
    err(1, "execve: %s", argv[0]);
    /* NOTREACHED */
}
