#!/usr/bin/perl
use strict;
use warnings;
my $target = $ARGV[0];

opendir(DIR, ".");
my @files = grep(/\.tsts$/,readdir(DIR));
closedir(DIR);

open CTEST_CONFIG, ">CTestConfig.cmake" or die $1;
my $dartsubmitinfo = <<'END';
SET (CTEST_NIGHTLY_START_TIME "10:00:00 GMT")
SET (CTEST_START_WITH_EMPTY_BINARY_DIRECTORY FALSE)

SET(CTEST_PROJECT_NAME "Evpath")
SET(CTEST_NIGHTLY_START_TIME "10:49:51 GMT")

IF(NOT DEFINED CTEST_DROP_METHOD)
  SET(CTEST_DROP_METHOD "http")
ENDIF(NOT DEFINED CTEST_DROP_METHOD)

IF(CTEST_DROP_METHOD STREQUAL "http")
  SET(CTEST_DROP_SITE_CDASH TRUE)
  SET(CTEST_DROP_SITE "cdash.cercs.gatech.edu")
  SET(CTEST_DROP_LOCATION "/submit.php?project=Evpath")
  SET(CTEST_TRIGGER_SITE "cdash.cercs.gatech.edu")
ENDIF(CTEST_DROP_METHOD STREQUAL "http")
END
print CTEST_CONFIG "$dartsubmitinfo\n";
close CTEST_CONFIG;

open CTEST, ">CTestTestfile.cmake" or die $1;

print CTEST "# \n";
print CTEST "# generated from files: ";
print CTEST join( ', ', @files );
print CTEST "\n";
print CTEST "# \n";
print CTEST "message (\"SSH params are \$ENV{SSH_PARAMS}\")";

foreach my $file (@files) {
    open(TSTS, $file) or die("Could not open  $file.");
    print CTEST "# \n";
    print CTEST "# tests from $file\n";
    print CTEST "# \n";
    my $remote_spec = "jedi080.cc.gatech.edu:/users/c/chaos/evpath_tests/rhe6-64";
    foreach my $line (<TSTS>)  {
	chomp $line;
	print CTEST "ADD_TEST($line \"$line\" \"-ssh\" \"\$ENV{SSH_PARAMS}\")\n";
    }
}

close CTEST;

open TEST, ">$target.cmake" or die $1;
my $TEST_BODY = <<'END';
## -- Set hostname
## --------------------------
find_program(HOSTNAME_CMD NAMES hostname)
exec_program(${HOSTNAME_CMD} ARGS OUTPUT_VARIABLE HOSTNAME)
set(CTEST_SITE                          "${HOSTNAME}")
set(MODEL                               "Experimental")
set(CMAKE_C_COMPILER "/usr/bin/gcc")
SET(CTEST_NIGHTLY_START_TIME "11:56:44 GMT")
SET (CTEST_START_WITH_EMPTY_BINARY_DIRECTORY FALSE)
set(CTEST_BINARY_DIRECTORY              "${CTEST_SOURCE_DIRECTORY}")

set(CTEST_TIMEOUT           "7200")
set( $ENV{LC_MESSAGES}      "en_EN" )
ctest_start(${MODEL} TRACK ${MODEL})

message(" -- Test ${MODEL} - ${CTEST_BUILD_NAME} --")
ctest_test(     BUILD  "${CTEST_BINARY_DIRECTORY}" RETURN_VALUE res)


## -- SUBMIT
message(" -- Submit ${MODEL} - ${CTEST_BUILD_NAME} --")
ctest_submit(                                              RETURN_VALUE res)

message(" -- Finished ${MODEL}  - ${CTEST_BUILD_NAME} --")

END
print TEST "set(CTEST_SOURCE_DIRECTORY 	\"/home/eisen/evpath_tests\")\n";
print TEST "set(CTEST_BUILD_NAME 	\"Pi-to-Jedi\")\n";
print TEST "SET( ENV{SSH_PARAMS}    \"jedi080.cc.gatech.edu:/users/c/chaos/evpath_tests/rhe6-64\" )\n";
print TEST "SET( ENV{CM_HOSTNAME} \"eisenhauer.dyndns.org\")\n";
print TEST "SET( ENV{CM_PORT_RANGE} \"62000:62100\")\n";
print TEST $TEST_BODY;
close TEST;

system("ctest -V -S $target.cmake");
