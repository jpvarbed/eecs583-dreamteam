##===- projects/sample/Makefile ----------------------------*- Makefile -*-===##
#
# This is a sample Makefile for a project that uses LLVM.
#
##===----------------------------------------------------------------------===##

#
# Indicates our relative path to the top of the project's root directory.
#
LEVEL = .
DIRS = lib tools
EXTRA_DIST = include

LOADABLE_MODULE=1
#
# Include the Master Makefile that knows how to build all.
#
include $(LEVEL)/Makefile.common
EXTRA_OPTIONS = -fexceptions
