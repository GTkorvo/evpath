
AUTOMAKE_OPTIONS = no-dependencies foreign

SUBDIRS = .

INCLUDES = -I$(srcdir)/.. -I..

TESTS = evtest chain_test filter_test

evtest_SOURCES = evtest.c test_support.c

chain_test_SOURCES = chain_test.c test_support.c

filter_test_SOURCES = filter_test.c test_support.c

noinst_PROGRAMS = $(TESTS) 

LDADD = ../libevpath.la 

LDFLAGS = -static
