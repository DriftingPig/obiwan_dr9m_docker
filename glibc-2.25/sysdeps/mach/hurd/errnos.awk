# Copyright (C) 1991-2017 Free Software Foundation, Inc.
# This file is part of the GNU C Library.

# The GNU C Library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.

# The GNU C Library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.

# You should have received a copy of the GNU Lesser General Public
# License along with the GNU C Library; if not, see
# <http://www.gnu.org/licenses/>.

# errno.texinfo contains lines like:
# @comment errno.h
# @comment POSIX.1: Function not implemented
# @deftypevr Macro int ENOSYS
# @comment errno 123

BEGIN {
    print "/* This file generated by errnos.awk.  */";
    print "";
    print "/* The Hurd uses Mach error system 0x10, currently only subsystem 0. */";
    print "#ifndef _HURD_ERRNO";
    print "#define _HURD_ERRNO(n)\t((0x10 << 26) | ((n) & 0x3fff))";
    print "#endif";
    print "";
    print "#ifdef _ERRNO_H\n";
    print "enum __error_t_codes\n{";
    print "\t/* The value zero always means success and it is perfectly fine for";
    print "\t   code to use 0 explicitly (or implicitly, e.g. via Boolean coercion).";
    print "\t   Having an enum entry for zero both makes the debugger print the name";
    print "\t   for error_t-typed zero values, and prevents the compiler from";
    print "\t   issuing warnings about 'case 0:' in a switch on an error_t-typed";
    print "\t   value.  */";
    print "\tESUCCESS = 0,"
    print "";
    errnoh = 0;
    maxerrno = 0;
    in_mach_errors = "";
    in_math = 0;
    edom = erange = "";
    print "#undef EDOM\n#undef ERANGE";
    lno = 0;
  }

$1 == "@comment" && $2 == "errno.h" { errnoh=1; next }
$1 == "@comment" && errnoh == 1 \
  {
    ++errnoh;
    etext = "";
    for (i = 3; i <= NF; ++i)
      etext = etext " " $i;
    next;
  }

errnoh == 2 && $1 == "@deftypevr"  && $2 == "Macro" && $3 == "int" \
  { ++errnoh; e = $4; next; }

errnoh == 3 && $1 == "@comment" && $2 == "errno" {
    if (e == "EWOULDBLOCK")
      {
	lines[lno++]="#define EWOULDBLOCK EAGAIN /* Operation would block */";
	next;
      }
    errno = $3 + 0;
    if (errno == 0)
      next;
    if (errno > maxerrno) maxerrno = errno;
    x = sprintf ("%-40s/*%s */", sprintf ("%-24s%s", "#define\t" e,
					  "_HURD_ERRNO (" errno ")"),
		 etext);
    if (e == "EDOM")
      edom = x;
    else if (e == "ERANGE")
      erange = x;
    comma[lno] = 1;
    lines[lno++] = sprintf("\t%-16s= _HURD_ERRNO (%d)", e, errno);
    lines[lno++] = x;
    next;
  }
{ errnoh=0 }

NF == 3 && $1 == "#define" && $2 == "MACH_SEND_IN_PROGRESS" \
  {
    in_mach_errors = FILENAME;
    lines[lno++] = "\n\t/* Errors from <mach/message.h>.  */";
  }
NF == 3 && $1 == "#define" && $2 == "KERN_SUCCESS" \
  {
    in_mach_errors = FILENAME;
    lines[lno++] = "\n\t/* Errors from <mach/kern_return.h>.  */";
    next;
  }

in_mach_errors != "" && $2 == "MACH_IPC_COMPAT" \
  {
    in_mach_errors = "";
  }

in_mach_errors == FILENAME && NF == 3 && $1 == "#define" \
  {
    comma[lno] = 1;
    lines[lno++] = sprintf("\t%-32s= %s", "E" $2, $3);
  }

$1 == "#define" && $2 == "_MACH_MIG_ERRORS_H_" \
  {
    in_mig_errors = 1;
    lines[lno++] = "\n\t/* Errors from <mach/mig_errors.h>.  */";
    next;
  }
in_mig_errors && $1 == "#endif" && $3 == "_MACH_MIG_ERRORS_H_" \
  {
    in_mig_errors = 0;
  }

(in_mig_errors && $1 == "#define" && $3 <= -300) || \
(in_device_errors && $1 == "#define" && /D_/ && NF > 3) \
  {
    comment = "";
    for (i = 4; i <= NF; ++i)
      comment = comment " " $i;
    comma[lno] = 1;
    lines[lno++] = sprintf("%-32s", sprintf ("\t%-24s= %s", "E" $2, $3)) comment;
  }

$1 == "#define" && $2 == "D_SUCCESS" \
  {
    in_device_errors = 1;
    lines[lno++] = "\n\t/* Errors from <device/device_types.h>.  */";
    next;
  }
in_device_errors && $1 == "#endif" \
  {
    in_device_errors = 0;
  }


END \
  {
    for (i = 0; i < lno - 1; ++i)
      printf "%s%s\n", lines[i], (comma[i] ? "," : "");
    print lines[i];
    print "";
    print "};";
    print "";
    printf "#define\t_HURD_ERRNOS\t%d\n", maxerrno+1;
    print "";
    print "\
/* User-visible type of error codes.  It is ok to use `int' or\n\
   `kern_return_t' for these, but with `error_t' the debugger prints\n\
   symbolic values.  */";
    print "#ifdef __USE_GNU";
    print "typedef enum __error_t_codes error_t;"
    print "#define __error_t_defined\t1"
    print "#endif";
    print "";
    print "\
/* Return the current thread's location for `errno'.\n\
   The syntax of this function allows redeclarations like `int errno'.  */\n\
extern int *__errno_location (void) __THROW __attribute__ ((__const__));\n\
\n\
#define errno			(*__errno_location ())\n\
";
    print "#endif /* <errno.h> included.  */";
    print "";
    print "#if !defined (_ERRNO_H) && defined (__need_Emath)";
    print edom; print erange;
    print "#endif /* <errno.h> not included and need math error codes.  */";
  }
