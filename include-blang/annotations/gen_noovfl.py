#!/usr/bin/env python

import sys
import os

def main(argv=None):
  if argv is None:
    argv = sys.argv
  progname = argv[0]

  if len(argv) != 2:
    print "Usage: " + argv[0] + " <maximum arity>"
    exit(1)

  n = int(argv[1])

  retn_ty = "bool"
  expr_tys = ['unsigned char', 'unsigned short', 'unsigned int', 'unsigned long']

  print "/* MACHINE GENERATED - do not edit this file */"
  print ""
  
  for expr_ty in expr_tys:
    for i in range(1, n + 1):
      print "_DEVICE_QUALIFIER %s __add_noovfl_%s_%d(" % (retn_ty, expr_ty.replace(' ', '_'), i)
      print "  " + ",\n  ".join([ '%s v%d' % (expr_ty, j) for j in range(i) ])
      print ");"

    for i in range(1, n + 1):
      print "_DEVICE_QUALIFIER _BUGLE_INLINE __attribute__((overloadable)) %s __add_noovfl(" % retn_ty
      print "  " + ",\n  ".join([ '%s v%d' % (expr_ty, j) for j in range(i) ])
      print ") { return __add_noovfl_%s_%d(" % (expr_ty.replace(' ', '_'), i)
      print "    " + ",\n    ".join([ 'v%d' % j for j in range(i) ])
      print "    );"
      print "}"

if __name__ == '__main__':
  sys.exit(main())
