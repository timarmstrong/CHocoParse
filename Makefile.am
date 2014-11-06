ACLOCAL_AMFLAGS = -I m4
EXTRA_DIST = autogen.sh README.md

AM_CFLAGS = -std=c99 -pedantic -Wall -Wextra -Wconversion
AM_LDFLAGS =

# Include all code in library
lib_LTLIBRARIES = lib/libtsconfig.la
lib_libtsconfig_la_SOURCES = src/tsconfig.c src/tsconfig_lex.c \
  src/tsconfig_err.c src/tsconfig_tree.c src/tsconfig_tree_reader.c \
  src/tsconfig_tok.c src/tsconfig_paths.c

bin_PROGRAMS = bin/tsconfig_test
bin_tsconfig_test_SOURCES = src/tsconfig_test.c
bin_tsconfig_test_LDADD = lib/libtsconfig.la

include_HEADERS = src/tsconfig.h src/tsconfig_common.h src/tsconfig_tree.h \
      src/tsconfig_reader.h
