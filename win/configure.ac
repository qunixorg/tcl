#! /bin/bash -norc
# This file is an input file used by the GNU "autoconf" program to
# generate the file "configure", which is run during Tcl installation
# to configure the system for the local environment.

AC_INIT(../generic/tcl.h)
AC_PREREQ(2.69)

# The following define is needed when building with Cygwin since newer
# versions of autoconf incorrectly set SHELL to /bin/bash instead of
# /bin/sh. The bash shell seems to suffer from some strange failures.
SHELL=/bin/sh

TCL_VERSION=9.0
TCL_MAJOR_VERSION=9
TCL_MINOR_VERSION=0
TCL_PATCH_LEVEL="a2"
VER=$TCL_MAJOR_VERSION$TCL_MINOR_VERSION

TCL_DDE_VERSION=1.4
TCL_DDE_MAJOR_VERSION=1
TCL_DDE_MINOR_VERSION=4
DDEVER=$TCL_DDE_MAJOR_VERSION$TCL_DDE_MINOR_VERSION

TCL_REG_VERSION=1.3
TCL_REG_MAJOR_VERSION=1
TCL_REG_MINOR_VERSION=3
REGVER=$TCL_REG_MAJOR_VERSION$TCL_REG_MINOR_VERSION

PKG_CFG_ARGS=$@

#------------------------------------------------------------------------
# Empty slate for bundled packages, to avoid stale configuration
#------------------------------------------------------------------------
rm -Rf pkgs

#------------------------------------------------------------------------
# Handle the --prefix=... option
#------------------------------------------------------------------------

if test "${prefix}" = "NONE"; then
    prefix=/usr/local
fi
if test "${exec_prefix}" = "NONE"; then
    exec_prefix=$prefix
fi
# libdir must be a fully qualified path (not ${exec_prefix}/lib)
eval libdir="$libdir"

#------------------------------------------------------------------------
# Standard compiler checks
#------------------------------------------------------------------------

# If the user did not set CFLAGS, set it now to keep
# the AC_PROG_CC macro from adding "-g -O2".
if test "${CFLAGS+set}" != "set" ; then
    CFLAGS=""
fi

AC_PROG_CC
AC_C_INLINE
AC_HEADER_STDC

AC_CHECK_TOOL(AR, ar)
AC_CHECK_TOOL(RANLIB, ranlib)
AC_CHECK_TOOL(RC, windres)

#--------------------------------------------------------------------
# Checks to see if the make program sets the $MAKE variable.
#--------------------------------------------------------------------

AC_PROG_MAKE_SET

#--------------------------------------------------------------------
# Determines the correct binary file extension (.o, .obj, .exe etc.)
#--------------------------------------------------------------------

AC_OBJEXT
AC_EXEEXT

#------------------------------------------------------------------------
# Embedded configuration information, encoding to use for the values, TIP #59
#------------------------------------------------------------------------

SC_TCL_CFG_ENCODING

#--------------------------------------------------------------------
# The statements below define a collection of symbols related to
# building libtcl as a shared library instead of a static library.
#--------------------------------------------------------------------

SC_ENABLE_SHARED

#--------------------------------------------------------------------
# The statements below define a collection of compile flags.  This
# macro depends on the value of SHARED_BUILD, and should be called
# after SC_ENABLE_SHARED checks the configure switches.
#--------------------------------------------------------------------

SC_CONFIG_CFLAGS

# Cross-compiling
case ${host_alias} in
*mingw32*)
    TCL_EXE="tclsh"
    ;;
*)
    TCL_EXE="TCL_LIBRARY=\"\${LIBRARY_DIR}\"; export TCL_LIBRARY; ./\${TCLSH}"
    ;;
esac

#------------------------------------------------------------------------
#	Add stuff for zlib; note that this is mostly done in the makefile now
#	as we just assume that the platform hasn't got a usable z.lib
#------------------------------------------------------------------------

AS_IF([test "${enable_shared+set}" = "set"], [
  enableval="$enable_shared"
  tcl_ok=$enableval
], [
  tcl_ok=yes
])
AS_IF([test "$tcl_ok" = "yes"], [
  AC_SUBST(ZLIB_DLL_FILE,[\${ZLIB_DLL_FILE}])
  AS_IF([test "$do64bit" != "no"], [
    AS_IF([test "$GCC" == "yes"],[
      AC_SUBST(ZLIB_LIBS,[\${ZLIB_DIR_NATIVE}/win64/libz.dll.a])
    ], [
      AC_SUBST(ZLIB_LIBS,[\${ZLIB_DIR_NATIVE}/win64/zdll.lib])
    ])
  ], [
    AC_SUBST(ZLIB_LIBS,[\${ZLIB_DIR_NATIVE}/win32/zdll.lib])
  ])
], [
  AC_SUBST(ZLIB_OBJS,[\${ZLIB_OBJS}])
])
AC_DEFINE(HAVE_ZLIB, 1, [Is there an installed zlib?])

AC_CHECK_TYPE([intptr_t], [
    AC_DEFINE([HAVE_INTPTR_T], 1, [Do we have the intptr_t type?])], [
    AC_CACHE_CHECK([for pointer-size signed integer type], tcl_cv_intptr_t, [
    for tcl_cv_intptr_t in "int" "long" "long long" none; do
	if test "$tcl_cv_intptr_t" != none; then
	    AC_COMPILE_IFELSE([AC_LANG_BOOL_COMPILE_TRY([AC_INCLUDES_DEFAULT],
		    [[sizeof (void *) <= sizeof ($tcl_cv_intptr_t)]])],
		[tcl_ok=yes], [tcl_ok=no])
	    test "$tcl_ok" = yes && break; fi
    done])
    if test "$tcl_cv_intptr_t" != none; then
	AC_DEFINE_UNQUOTED([intptr_t], [$tcl_cv_intptr_t], [Signed integer
	   type wide enough to hold a pointer.])
    fi
])
AC_CHECK_TYPE([uintptr_t], [
    AC_DEFINE([HAVE_UINTPTR_T], 1, [Do we have the uintptr_t type?])], [
    AC_CACHE_CHECK([for pointer-size unsigned integer type], tcl_cv_uintptr_t, [
    for tcl_cv_uintptr_t in "unsigned int" "unsigned long" "unsigned long long" \
	    none; do
	if test "$tcl_cv_uintptr_t" != none; then
	    AC_COMPILE_IFELSE([AC_LANG_BOOL_COMPILE_TRY([AC_INCLUDES_DEFAULT],
		    [[sizeof (void *) <= sizeof ($tcl_cv_uintptr_t)]])],
		[tcl_ok=yes], [tcl_ok=no])
	    test "$tcl_ok" = yes && break; fi
    done])
    if test "$tcl_cv_uintptr_t" != none; then
	AC_DEFINE_UNQUOTED([uintptr_t], [$tcl_cv_uintptr_t], [Unsigned integer
	   type wide enough to hold a pointer.])
    fi
])


#--------------------------------------------------------------------
#	Zipfs support - Tip 430
#--------------------------------------------------------------------
AC_ARG_ENABLE(zipfs,
    AC_HELP_STRING([--enable-zipfs],
	[build with Zipfs support (default: on)]),
    [tcl_ok=$enableval], [tcl_ok=yes])
if test "$tcl_ok" = "yes" ; then
    #
    # Find a native compiler
    #
    AX_CC_FOR_BUILD
    #
    # Find a native zip implementation
    #
    SC_PROG_TCLSH
    SC_ZIPFS_SUPPORT
	ZIPFS_BUILD=1
	TCL_ZIP_FILE=libtcl_${TCL_MAJOR_VERSION}_${TCL_MINOR_VERSION}_${TCL_PATCH_LEVEL}.zip
else
	ZIPFS_BUILD=0
	TCL_ZIP_FILE=
fi
# Do checking message here to not mess up interleaved configure output
AC_MSG_CHECKING([for building with zipfs])
if test "${ZIPFS_BUILD}" = 1; then
    if test "${SHARED_BUILD}" = 0; then
       ZIPFS_BUILD=2;
       AC_DEFINE(ZIPFS_BUILD, 2, [Are we building with zipfs enabled?])
       INSTALL_LIBRARIES=install-libraries-zipfs-static
       AC_MSG_RESULT([yes])
     else
       AC_DEFINE(ZIPFS_BUILD, 1, [Are we building with zipfs enabled?])\
       INSTALL_LIBRARIES=install-libraries-zipfs-shared
       AC_MSG_RESULT([yes])
    fi
else
AC_MSG_RESULT([no])
INSTALL_LIBRARIES=install-libraries
INSTALL_MSGS=install-msgs
fi
AC_SUBST(ZIPFS_BUILD)
AC_SUBST(TCL_ZIP_FILE)
AC_SUBST(INSTALL_LIBRARIES)
AC_SUBST(INSTALL_MSGS)


#--------------------------------------------------------------------
# Perform additinal compiler tests.
#--------------------------------------------------------------------

# See if declarations like FINDEX_INFO_LEVELS are
# missing from winbase.h. This is known to be
# a problem with VC++ 5.2.

AC_CACHE_CHECK(for FINDEX_INFO_LEVELS in winbase.h,
    tcl_cv_findex_enums,
AC_TRY_COMPILE([
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
],
[
  FINDEX_INFO_LEVELS i;
  FINDEX_SEARCH_OPS j;
],
        tcl_cv_findex_enums=yes,
        tcl_cv_findex_enums=no)
)
if test "$tcl_cv_findex_enums" = "no"; then
    AC_DEFINE(HAVE_NO_FINDEX_ENUMS, 1,
            [Defined when enums are missing from winbase.h])
fi

# See if the compiler supports intrinsics.

AC_CACHE_CHECK(for intrinsics support in compiler,
    tcl_cv_intrinsics,
AC_TRY_LINK([
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#include <intrin.h>
],
[
  __cpuidex(0,0,0);
],
        tcl_cv_intrinsics=yes,
        tcl_cv_intrinsics=no)
)
if test "$tcl_cv_intrinsics" = "yes"; then
    AC_DEFINE(HAVE_INTRIN_H, 1,
            [Defined when the compilers supports intrinsics])
fi

# See if the <wspiapi.h> header file is present

AC_CACHE_CHECK(for wspiapi.h,
    tcl_cv_wspiapi_h,
AC_TRY_COMPILE([
#include <wspiapi.h>
], [],
        tcl_cv_wspiapi_h=yes,
        tcl_cv_wspiapi_h=no)
)
if test "$tcl_cv_wspiapi_h" = "yes"; then
    AC_DEFINE(HAVE_WSPIAPI_H, 1,
            [Defined when wspiapi.h exists])
fi

# See if declarations like FINDEX_INFO_LEVELS are
# missing from winbase.h. This is known to be
# a problem with VC++ 5.2.

AC_CACHE_CHECK(for FINDEX_INFO_LEVELS in winbase.h,
    tcl_cv_findex_enums,
AC_TRY_COMPILE([
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
],
[
  FINDEX_INFO_LEVELS i;
  FINDEX_SEARCH_OPS j;
],
        tcl_cv_findex_enums=yes,
        tcl_cv_findex_enums=no)
)
if test "$tcl_cv_findex_enums" = "no"; then
    AC_DEFINE(HAVE_NO_FINDEX_ENUMS, 1,
            [Defined when enums are missing from winbase.h])
fi

#--------------------------------------------------------------------
# Set the default compiler switches based on the --enable-symbols
# option.  This macro depends on C flags, and should be called
# after SC_CONFIG_CFLAGS macro is called.
#--------------------------------------------------------------------

SC_ENABLE_SYMBOLS

TCL_DBGX=${DBGX}

#--------------------------------------------------------------------
# Embed the manifest if we can determine how
#--------------------------------------------------------------------

SC_EMBED_MANIFEST

#------------------------------------------------------------------------
# tclConfig.sh refers to this by a different name
#------------------------------------------------------------------------

TCL_SHARED_BUILD=${SHARED_BUILD}

#--------------------------------------------------------------------
# Perform final evaluations of variables with possible substitutions.
#--------------------------------------------------------------------

TCL_SHARED_LIB_SUFFIX="\${NODOT_VERSION}${DLLSUFFIX}"
TCL_UNSHARED_LIB_SUFFIX="\${NODOT_VERSION}${LIBSUFFIX}"
TCL_EXPORT_FILE_SUFFIX="\${NODOT_VERSION}${LIBSUFFIX}"

eval "TCL_SRC_DIR=\"`cd $srcdir/..; $CYGPATH $(pwd)`\""

eval "TCL_DLL_FILE=tcl${VER}${DLLSUFFIX}"

eval "TCL_STUB_LIB_FILE=\"${LIBPREFIX}tclstub${VER}${LIBSUFFIX}\""
eval "TCL_STUB_LIB_FLAG=\"-ltclstub${VER}${LIBFLAGSUFFIX}\""
eval "TCL_BUILD_STUB_LIB_SPEC=\"-L`$CYGPATH $(pwd)` ${TCL_STUB_LIB_FLAG}\""
eval "TCL_STUB_LIB_SPEC=\"-L${libdir} ${TCL_STUB_LIB_FLAG}\""
eval "TCL_BUILD_STUB_LIB_PATH=\"`$CYGPATH $(pwd)`/${TCL_STUB_LIB_FILE}\""
eval "TCL_STUB_LIB_PATH=\"${libdir}/${TCL_STUB_LIB_FILE}\""

eval "TCL_LIB_FILE=\"${LIBPREFIX}tcl${VER}${LIBSUFFIX}\""
eval "TCL_BUILD_LIB_SPEC=\"-L`$CYGPATH $(pwd)` -ltcl${VER}${FLAGSUFFIX}\""
eval "TCL_LIB_SPEC=\"-L${libdir} -ltcl${VER}${FLAGSUFFIX}\""

# Install time header dir can be set via --includedir
eval "TCL_INCLUDE_SPEC=\"-I${includedir}\""


eval "DLLSUFFIX=${DLLSUFFIX}"
eval "LIBPREFIX=${LIBPREFIX}"
eval "LIBSUFFIX=${LIBSUFFIX}"
eval "EXESUFFIX=${EXESUFFIX}"

CFG_TCL_SHARED_LIB_SUFFIX=${TCL_SHARED_LIB_SUFFIX}
CFG_TCL_UNSHARED_LIB_SUFFIX=${TCL_UNSHARED_LIB_SUFFIX}
CFG_TCL_EXPORT_FILE_SUFFIX=${TCL_EXPORT_FILE_SUFFIX}

#--------------------------------------------------------------------
# Adjust the defines for how the resources are built depending
# on symbols and static vs. shared.
#--------------------------------------------------------------------

if test ${SHARED_BUILD} = 0 ; then
    if test "${DBGX}" = "g"; then
        RC_DEFINES="${RC_DEFINE} STATIC_BUILD ${RC_DEFINE} DEBUG"
    else
        RC_DEFINES="${RC_DEFINE} STATIC_BUILD"
    fi
else
    if test "${DBGX}" = "g"; then
        RC_DEFINES="${RC_DEFINE} DEBUG"
    else
        RC_DEFINES=""
    fi
fi

#--------------------------------------------------------------------
#	The statements below define the symbol TCL_PACKAGE_PATH, which
#	gives a list of directories that may contain packages.  The list
#	consists of one directory for machine-dependent binaries and
#	another for platform-independent scripts.
#--------------------------------------------------------------------

if test "$prefix/lib" != "$libdir"; then
    TCL_PACKAGE_PATH="${libdir} ${prefix}/lib"
else
    TCL_PACKAGE_PATH="${prefix}/lib"
fi

# The tclsh.exe.manifest requires these
# TCL_WIN_VERSION is the 4 dotted pair Windows version format which needs
# the release level, and must account for interim release versioning
case "$TCL_PATCH_LEVEL" in
     *a*) TCL_RELEASE_LEVEL=0 ;;
     *b*) TCL_RELEASE_LEVEL=1 ;;
     *)   TCL_RELEASE_LEVEL=2 ;;
esac
TCL_WIN_VERSION="$TCL_VERSION.$TCL_RELEASE_LEVEL.`echo $TCL_PATCH_LEVEL | tr -d ab.`"
AC_SUBST(TCL_WIN_VERSION)
# X86|AMD64|IA64 for manifest
AC_SUBST(MACHINE)

AC_SUBST(TCL_VERSION)
AC_SUBST(TCL_MAJOR_VERSION)
AC_SUBST(TCL_MINOR_VERSION)
AC_SUBST(TCL_PATCH_LEVEL)
AC_SUBST(PKG_CFG_ARGS)
AC_SUBST(TCL_EXE)

AC_SUBST(TCL_LIB_FILE)
AC_SUBST(TCL_LIB_FLAG)
AC_SUBST(TCL_STATIC_LIB_FILE)
AC_SUBST(TCL_STATIC_LIB_FLAG)
AC_SUBST(TCL_IMPORT_LIB_FILE)
AC_SUBST(TCL_IMPORT_LIB_FLAG)
# empty on win
AC_SUBST(TCL_LIB_SPEC)
AC_SUBST(TCL_STUB_LIB_FILE)
AC_SUBST(TCL_STUB_LIB_FLAG)
AC_SUBST(TCL_STUB_LIB_SPEC)
AC_SUBST(TCL_STUB_LIB_PATH)
AC_SUBST(TCL_INCLUDE_SPEC)
AC_SUBST(TCL_BUILD_STUB_LIB_SPEC)
AC_SUBST(TCL_BUILD_STUB_LIB_PATH)
AC_SUBST(TCL_DLL_FILE)

AC_SUBST(TCL_SRC_DIR)
AC_SUBST(TCL_BIN_DIR)
AC_SUBST(TCL_DBGX)
AC_SUBST(CFG_TCL_SHARED_LIB_SUFFIX)
AC_SUBST(CFG_TCL_UNSHARED_LIB_SUFFIX)
AC_SUBST(CFG_TCL_EXPORT_FILE_SUFFIX)

# win/tcl.m4 doesn't set (CFLAGS)
AC_SUBST(CFLAGS_DEFAULT)
AC_SUBST(EXTRA_CFLAGS)
AC_SUBST(CYGPATH)
AC_SUBST(DEPARG)
AC_SUBST(CC_OBJNAME)
AC_SUBST(CC_EXENAME)

# win/tcl.m4 doesn't set (LDFLAGS)
AC_SUBST(LDFLAGS_DEFAULT)
AC_SUBST(LDFLAGS_DEBUG)
AC_SUBST(LDFLAGS_OPTIMIZE)
AC_SUBST(LDFLAGS_CONSOLE)
AC_SUBST(LDFLAGS_WINDOW)
AC_SUBST(AR)
AC_SUBST(RANLIB)

AC_SUBST(STLIB_LD)
AC_SUBST(SHLIB_LD)
AC_SUBST(SHLIB_LD_LIBS)
AC_SUBST(SHLIB_CFLAGS)
AC_SUBST(SHLIB_SUFFIX)
AC_SUBST(TCL_SHARED_BUILD)

AC_SUBST(LIBS)
AC_SUBST(LIBS_GUI)
AC_SUBST(DLLSUFFIX)
AC_SUBST(LIBPREFIX)
AC_SUBST(LIBSUFFIX)
AC_SUBST(EXESUFFIX)
AC_SUBST(LIBRARIES)
AC_SUBST(MAKE_LIB)
AC_SUBST(MAKE_STUB_LIB)
AC_SUBST(POST_MAKE_LIB)
AC_SUBST(MAKE_DLL)
AC_SUBST(MAKE_EXE)

# empty on win, but needs sub'ing
AC_SUBST(TCL_BUILD_LIB_SPEC)
AC_SUBST(TCL_CC_SEARCH_FLAGS)
AC_SUBST(TCL_LD_SEARCH_FLAGS)
AC_SUBST(TCL_NEEDS_EXP_FILE)
AC_SUBST(TCL_BUILD_EXP_FILE)
AC_SUBST(TCL_EXP_FILE)
AC_SUBST(DL_LIBS)
AC_SUBST(TCL_LIB_VERSIONS_OK)
AC_SUBST(TCL_PACKAGE_PATH)

# win only
AC_SUBST(TCL_DDE_VERSION)
AC_SUBST(TCL_DDE_MAJOR_VERSION)
AC_SUBST(TCL_DDE_MINOR_VERSION)
AC_SUBST(TCL_REG_VERSION)
AC_SUBST(TCL_REG_MAJOR_VERSION)
AC_SUBST(TCL_REG_MINOR_VERSION)

AC_SUBST(RC)
AC_SUBST(RC_OUT)
AC_SUBST(RC_TYPE)
AC_SUBST(RC_INCLUDE)
AC_SUBST(RC_DEFINE)
AC_SUBST(RC_DEFINES)
AC_SUBST(RES)

AC_OUTPUT(Makefile tclConfig.sh tcl.hpj tclsh.exe.manifest)

dnl Local Variables:
dnl mode: autoconf;
dnl End:
