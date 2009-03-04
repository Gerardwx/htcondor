##**************************************************************
##
## Copyright (C) 1990-2007, Condor Team, Computer Sciences Department,
## University of Wisconsin-Madison, WI.
## 
## Licensed under the Apache License, Version 2.0 (the "License"); you
## may not use this file except in compliance with the License.  You may
## obtain a copy of the License at
## 
##    http://www.apache.org/licenses/LICENSE-2.0
## 
## Unless required by applicable law or agreed to in writing, software
## distributed under the License is distributed on an "AS IS" BASIS,
## WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
## See the License for the specific language governing permissions and
## limitations under the License.
##
##**************************************************************


# CondorTest.pm - a Perl module for automated testing of Condor
#
# 19??-???-?? originally written by Tom Stanis (?)
# 2000-Jun-02 total overhaul by pfc@cs.wisc.edu and wright@cs.wisc.edu

package CondorTest;

require 5.0;
use Carp;
use Condor;
use CondorPersonal;
use FileHandle;
use POSIX;
use Net::Domain qw(hostfqdn);
use Cwd;
use Time::Local;
use strict;
use warnings;

my %securityoptions =
(
"NEVER" => "1",
"OPTIONAL" => "1",
"PREFERRED" => "1",
"REQUIRED" => "1",
);

# Tracking Running Tests
my $RunningFile = "RunningTests";
my $LOCK_EXCLUSIVE = 2;
my $UNLOCK = 8;
my $TRUE = 1;
my $FALSE = 0;
my $teststrt = 0;
my $teststop = 0;


my $MAX_CHECKPOINTS = 2;
my $MAX_VACATES = 3;

my @skipped_output_lines;
my @expected_output;

my $checkpoints;
my $hoststring;
my $submit_file; #RunTest and RunDagTest set.
my $vacates;
my %test;
my %machine_ads;
my $lastconfig;
my $handle; #actually the test name.
my $BaseDir = getcwd();
my $iswindows = IsThisWindows();
my $isnightly = IsThisNightly($BaseDir);

# we want to process and track the collection of cores
my $coredir = "$BaseDir/Cores";
if(!(-d $coredir)) {
	debug("Creating collection directory for cores\n",2);
	system("mkdir -p $coredir");
}

# set up for reading in core/ERROR exemptions
my $errexempts = "ErrorExemptions";
my %exemptions;
my $failed_coreERROR = 0;

BEGIN
{
    # disable command buffering so output is flushed immediately
    STDOUT->autoflush();
    STDERR->autoflush();

    $MAX_CHECKPOINTS = 2;
    $MAX_VACATES = 3;
    $checkpoints = 0;
	$hoststring = "notset:000";
    $vacates = 0;
	$lastconfig = "";

    Condor::DebugOn();
}

sub Reset
{
    %machine_ads = ();
	Condor::Reset();
	$hoststring = "notset:000";
}

sub SetExpected
{
	my $expected_ref = shift;
	foreach my $line (@{$expected_ref}) {
		debug( "$line\n", 2);
	}
	@expected_output = @{$expected_ref};
}

sub SetSkipped
{
	my $skipped_ref = shift;
	foreach my $line (@{$skipped_ref}) {
		debug( "$line\n", 2);
	}
	@skipped_output_lines = @{$skipped_ref};
}

sub ForceVacate
{
    my %info = @_;

    return 0 if ( $checkpoints >= $MAX_CHECKPOINTS ||
		  $vacates >= $MAX_VACATES );

    # let the job run for a few seconds and then send vacate signal
    sleep 5;
    Condor::Vacate( "\"$info{'sinful'}\"" );
    $vacates++;
    return 1;
}

sub RegisterSubmit
{
    my $handle = shift || croak "missing handle argument";
    my $function_ref = shift || croak "submit: missing function reference argument";

    $test{$handle}{"RegisterSubmit"} = $function_ref;
}
sub RegisterExecute
{
    my $handle = shift || croak "missing handle argument";
    my $function_ref = shift || croak "execute: missing function reference argument";

    $test{$handle}{"RegisterExecute"} = $function_ref;
}
sub RegisterEvicted
{
    my $handle = shift || croak "missing handle argument";
    my $function_ref = shift || croak "evict: missing function reference argument";

    $test{$handle}{"RegisterEvicted"} = $function_ref;
}
sub RegisterEvictedWithCheckpoint
{
    my $handle = shift || croak "missing handle argument";
    my $function_ref = shift || croak "missing function reference argument";

    $test{$handle}{"RegisterEvictedWithCheckpoint"} = $function_ref;
}
sub RegisterEvictedWithRequeue
{
    my $handle = shift || croak "missing handle argument";
    my $function_ref = shift || croak "missing function reference argument";

    $test{$handle}{"RegisterEvictedWithRequeue"} = $function_ref;
}
sub RegisterEvictedWithoutCheckpoint
{
    my $handle = shift || croak "missing handle argument";
    my $function_ref = shift || croak "missing function reference argument";

    $test{$handle}{"RegisterEvictedWithoutCheckpoint"} = $function_ref;
}
sub RegisterExited
{
    my $handle = shift || croak "missing handle argument";
    my $function_ref = shift || croak "missing function reference argument";

    $test{$handle}{"RegisterExited"} = $function_ref;
}
sub RegisterExitedSuccess
{
    my $handle = shift || croak "missing handle argument";
    my $function_ref = shift || croak "exit success: missing function reference argument";

    $test{$handle}{"RegisterExitedSuccess"} = $function_ref;
}
sub RegisterExitedFailure
{
    my $handle = shift || croak "missing handle argument";
    my $function_ref = shift || croak "missing function reference argument";

    $test{$handle}{"RegisterExitedFailure"} = $function_ref;
}
sub RegisterExitedAbnormal
{
    my $handle = shift || croak "missing handle argument";
    my $function_ref = shift || croak "missing function reference argument";

    $test{$handle}{"RegisterExitedAbnormal"} = $function_ref;
}
sub RegisterAbort
{
    my $handle = shift || croak "missing handle argument";
    my $function_ref = shift || croak "missing function reference argument";

    $test{$handle}{"RegisterAbort"} = $function_ref;
}
sub RegisterShadow
{
    my $handle = shift || croak "missing handle argument";
    my $function_ref = shift || croak "missing function reference argument";

    $test{$handle}{"RegisterShadow"} = $function_ref;
}
sub RegisterWantError
{
    my $handle = shift || croak "missing handle argument";
    my $function_ref = shift || croak "missing function reference argument";

    $test{$handle}{"RegisterWantError"} = $function_ref;
}
sub RegisterHold
{
    my $handle = shift || croak "missing handle argument";
    my $function_ref = shift || croak "missing function reference argument";

    $test{$handle}{"RegisterHold"} = $function_ref;
}
sub RegisterRelease
{
    my $handle = shift || croak "missing handle argument";
    my $function_ref = shift || croak "missing function reference argument";

    $test{$handle}{"RegisterRelease"} = $function_ref;
}
sub RegisterJobErr
{
    my $handle = shift || croak "missing handle argument";
    my $function_ref = shift || croak "missing function reference argument";

    $test{$handle}{"RegisterJobErr"} = $function_ref;
}

sub RegisterTimed
{
    my $handle = shift || croak "missing handle argument";
    my $function_ref = shift || croak "missing function reference argument";
	my $alarm = shift || croak "missing wait time argument";

    $test{$handle}{"RegisterTimed"} = $function_ref;
    $test{$handle}{"RegisterTimedWait"} = $alarm;

	# relook at registration and re-register to allow a timer
	# to be set after we are running. 
	# Prior to this change timed callbacks were only regsitered
	# when we call "runTest" and similar calls at the start.

	CheckTimedRegistrations();
}

sub RemoveTimed
{
    my $handle = shift || croak "missing handle argument";

    $test{$handle}{"RegisterTimed"} = undef;
    $test{$handle}{"RegisterTimedWait"} = undef;
    debug( "Remove timer.......\n",4);
    Condor::RemoveTimed( );
}

sub DefaultOutputTest
{
    my %info = @_;

    croak "default_output_test called but no \@expected_output defined"
	unless $#expected_output >= 0;

    debug( "\$info{'output'} = $info{'output'}\n" ,4);

	my $output = "";
	my $error = "";
	my $initialdir = $info{'initialdir'};
	if((defined $initialdir) && ($initialdir ne "")) {
		debug( "Testing with initialdir = $initialdir\n" ,4);
		$output = $initialdir . "/" . $info{'output'};
		$error = $initialdir . "/" . $info{'error'};
		debug( "$output is now being tested.....\n" ,4);
	} else {
		$output = $info{'output'};
		$error = $info{'error'};
	}

    CompareText( $output, \@expected_output, @skipped_output_lines )
	|| die "$handle: FAILURE (STDOUT doesn't match expected output)\n";

    IsFileEmpty( $error ) || 
	die "$handle: FAILURE (STDERR contains data)\n";
}

sub RunTest
{
	DoTest(@_);
}
 
sub RunDagTest
{
	DoTest(@_);
}

sub DoTest
{
    $handle              = shift || croak "missing handle argument";
    $submit_file      = shift || croak "missing submit file argument";
    my $wants_checkpoint = shift;
	my $dagman_args = 	shift;

    my $status           = -1;
	my $monitorpid = 0;
	my $waitpid = 0;
	my $monitorret = 0;
	my $retval = 0;

	if( !(defined $wants_checkpoint)) {
		die "DoTest must get at least 3 args!!!!!\n";
	}

	debug("RunTest says test is<<$handle>>\n",2);;
	# moved the reset to preserve callback registrations which includes
	# an error callback at submit time..... Had to change timing
	CondorTest::Reset();

    croak "too many arguments" if shift;

    # this is kludgey :: needed to happen sooner for an error message callback in runcommand
    Condor::SetHandle($handle);

    # if we want a checkpoint, register a function to force a vacate
    # and register a function to check to make sure it happens
	if( $wants_checkpoint )
	{
		Condor::RegisterExecute( \&ForceVacate );
		Condor::RegisterEvictedWithCheckpoint( sub { $checkpoints++ } );
	} else {
		if(defined $test{$handle}{"RegisterExecute"}) {
			Condor::RegisterExecute($test{$handle}{"RegisterExecute"});
		}
	}

	CheckRegistrations();

	if($isnightly == 1) {
		print "\nCurrent date and load follow:\n";
		system("date");
		if($iswindows == 0) {
			system("uptime");
		}
		print "\n\n";
	}

	my $wrap_test = $ENV{WRAP_TESTS};

	my $config = "";
	if(defined  $wrap_test) {
		print "Config before PersonalCondorTest<<<<$ENV{CONDOR_CONFIG}>>>>\n";
		$lastconfig = $ENV{CONDOR_CONFIG};
		$config = PersonalCondorTest($submit_file, $handle);
		if($config ne "") {
			print "PersonalCondorTest returned this config file<$config>\n";
			print "Saving last config file<<<$lastconfig>>>\n";
			$ENV{CONDOR_CONFIG} = $config;
			print "CONDOR_CONFIG now <<$ENV{CONDOR_CONFIG}>>\n";
			system("condor_config_val -config");
		}
	}

	AddRunningTest($handle);

    # submit the job and get the cluster id
	debug( "Now submitting test job\n",4);
	my $cluster = 0;

	$teststrt = time();;
    # submit the job and get the cluster id
	if(!(defined $dagman_args)) {
		#print "Regular Test....\n";
    	$cluster = Condor::TestSubmit( $submit_file );
	} else {
		#print "Dagman Test....\n";
    	$cluster = Condor::TestSubmitDagman( $submit_file, $dagman_args );
	}
    
    # if condor_submit failed for some reason return an error
    if($cluster == 0){
		print "Why is cluster 0 in RunTest??????\n";
	} else {

    	# monitor the cluster and return its exit status
		# note 1/2/09 bt
		# any exits cause monitor to never return allowing us
		# to kill personal condor wrapping the test :-(
		
		$monitorpid = fork();
		if($monitorpid == 0) {
			# child does monitor
    		$monitorret = Condor::Monitor();

			debug( "Monitor did return on its own status<<<$monitorret>>>\n",4);
    		die "$handle: FAILURE (job never checkpointed)\n"
			if $wants_checkpoint && $checkpoints < 1;

			if(  $monitorret == 1 ) {
				debug( "child happy to exit 0\n",4);
				exit(0);
			} else {
				debug( "child not happy to exit 1\n",4);
				exit(1);
			}
		} else {
			# parent cleans up
			$waitpid = waitpid($monitorpid, 0);
			if($waitpid == -1) {
				debug( "No such process <<$monitorpid>>\n",4);
			} else {
				$retval = $?;
				debug( "Child status was <<$retval>>\n",4);
				if( WIFEXITED( $retval ) && WEXITSTATUS( $retval ) == 0 )
				{
					debug( "Monitor done and status good!\n",4);
					$retval = 1;
				} else {
					$status = WEXITSTATUS( $retval );
					debug( "Monitor done and status bad<<$status>>!\n",4);
					$retval = 0;
				}
			}
		}
	}

	debug( "************** condor_monitor back ************************ \n",4);

	$teststop = time();
	my $timediff = $teststop - $teststrt;

	if($isnightly == 1) {
		print "Test started <$teststrt> ended <$teststop> taking <$timediff> seconds\n";
		print "Current date and load follow:\n";
		system("date");
		if($iswindows == 0) {
			system("uptime");
		}
		print "\n\n";
	}

	##############################################################
	#
	# We ASSUME that each version of each personal condor
	# has its own unique log directory whether we are automatically 
	# wrapping a test at this level OR we have a test which
	# sets up N personal condors like a test like job_condorc_abc_van
	# which sets up 3.
	#
	# Our initial check is to see if we are not running in the
	# outer personal condor because checking there requires
	# more of an idea of begin and end times of the test and
	# some sort of understanding about how many tests are running
	# at once. In the case of more then one, one can not assign
	# fault easily.
	#
	# A wrapped test will be found here. A test running with
	# a personal condor outside of src/condor_tests will show
	# up here. A test submitted as part of a multiple number 
	# of personal condors will show up here. 
	#
	# If we catch calls to "CondorPersonal::KillDaemonPids($config)"
	# and it is used consistently we can check both wrapped
	# tests and tests involved with one or more personal condors
	# as we shut the personal condors down.
	#
	# All the personal condors created by CondorPersonal
	# have a testname.saveme in their path which would
	# show a class of tests to be safe to check even if outside
	# of src/condor_tests even if N tests are running concurrently.
	#
	# If we knew N is one we could do an every test check
	# based on start time and end time of the tests running
	# in any running condor.
	#
	##############################################################
	if(ShouldCheck_coreERROR() == 1){
		debug("Want to Check core and ERROR!!!!!!!!!!!!!!!!!!\n\n",2);
		# running in TestingPersonalCondor
		my $logdir = `condor_config_val log`;
		fullchomp($logdir);
		$failed_coreERROR = CoreCheck($handle, $logdir, $teststrt, $teststop);
	}
	##############################################################
	#
	# When to do core and ERROR checking thoughts 2/5/9
	#
	##############################################################

	if(defined  $wrap_test) {
		my $logdir = `condor_config_val log`;
		fullchomp($logdir);
		$failed_coreERROR = CoreCheck($handle, $logdir, $teststrt, $teststop);
		if($config ne "") {
			print "KillDaemonPids called on this config file<$config>\n";
			CondorPersonal::KillDaemonPids($config);
		} else {
			print "No config setting to call KillDaemonPids with\n";
		}
		print "Restoring this config<<<$lastconfig>>>\n";
		$ENV{CONDOR_CONFIG} = $lastconfig;
	} else {
		debug( "Not currently wrapping tests\n",4);
	}

	# done with this test
	RemoveRunningTest($handle);

    if($cluster == 0){
		if( exists $test{$handle}{"RegisterWantError"} ) {
			return(1);
		} else {
			return(0);
		}
	} else {
		# ok we think we want to pass it but how did core and ERROR
		# checking go
		if($failed_coreERROR == 0) {
    		return $retval;
		} else {
			# oops found a problem fail test
    		return 0;
		}
	}
}

sub CheckTimedRegistrations
{
	# this one event should be possible from ANY state
	# that the monitor reports to us. In this case which
	# initiated the change I wished to start a timer from
	# when the actual runtime was reached and not from
	# when we started the test and submited it. This is the time 
	# at all other regsitrations have to be registered by....

    if( defined $test{$handle}{"RegisterTimed"} )
    {
		debug( "Found a timer to regsiter.......\n",4);
		Condor::RegisterTimed( $test{$handle}{"RegisterTimed"} , $test{$handle}{"RegisterTimedWait"});
    }
}

sub CheckRegistrations
{
    # any handle-associated functions with the cluster
    # or else die with an unexpected event
    if( defined $test{$handle}{"RegisterExitedSuccess"} )
    {
        Condor::RegisterExitSuccess( $test{$handle}{"RegisterExitedSuccess"} );
    }
    else
    {
	Condor::RegisterExitSuccess( sub {
	    die "$handle: FAILURE (got unexpected successful termination)\n";
	} );
    }

    if( defined $test{$handle}{"RegisterExitedFailure"} )
    {
	Condor::RegisterExitFailure( $test{$handle}{"RegisterExitedFailure"} );
    }
    else
    {
	Condor::RegisterExitFailure( sub {
	    my %info = @_;
	    die "$handle: FAILURE (returned $info{'retval'})\n";
	} );
    }

    if( defined $test{$handle}{"RegisterExitedAbnormal"} )
    {
	Condor::RegisterExitAbnormal( $test{$handle}{"RegisterExitedAbnormal"} );
    }
    else
    {
	Condor::RegisterExitAbnormal( sub {
	    my %info = @_;
	    die "$handle: FAILURE (got signal $info{'signal'})\n";
	} );
    }

    if( defined $test{$handle}{"RegisterShadow"} )
    {
	Condor::RegisterShadow( $test{$handle}{"RegisterShadow"} );
    }

    if( defined $test{$handle}{"RegisterWantError"} )
    {
	Condor::RegisterWantError( $test{$handle}{"RegisterWantError"} );
    }

    if( defined $test{$handle}{"RegisterAbort"} )
    {
	Condor::RegisterAbort( $test{$handle}{"RegisterAbort"} );
    }
    else
    {
	Condor::RegisterAbort( sub {
	    my %info = @_;
	    die "$handle: FAILURE (job aborted by user)\n";
	} );
    }

    if( defined $test{$handle}{"RegisterHold"} )
    {
	Condor::RegisterHold( $test{$handle}{"RegisterHold"} );
    }
    else
    {
	Condor::RegisterHold( sub {
	    my %info = @_;
	    die "$handle: FAILURE (job held by user)\n";
	} );
    }

    if( defined $test{$handle}{"RegisterSubmit"} )
    {
	Condor::RegisterSubmit( $test{$handle}{"RegisterSubmit"} );
    }

    if( defined $test{$handle}{"RegisterRelease"} )
    {
	Condor::RegisterRelease( $test{$handle}{"RegisterRelease"} );
    }
    #else
    #{
	#Condor::RegisterRelease( sub {
	    #my %info = @_;
	    #die "$handle: FAILURE (job released by user)\n";
	#} );
    #}

    if( defined $test{$handle}{"RegisterJobErr"} )
    {
	Condor::RegisterJobErr( $test{$handle}{"RegisterJobErr"} );
    }
    else
    {
	Condor::RegisterJobErr( sub {
	    my %info = @_;
	    die "$handle: FAILURE (job error -- see $info{'log'})\n";
	} );
    }

    # if we wanted to know about requeues.....
    if( defined $test{$handle}{"RegisterEvictedWithRequeue"} )
    {
        Condor::RegisterEvictedWithRequeue( $test{$handle}{"RegisterEvictedWithRequeue"} );
    } 

    # if evicted, call condor_resched so job runs again quickly
    if( !defined $test{$handle}{"RegisterEvicted"} )
    {
        Condor::RegisterEvicted( sub { sleep 5; Condor::Reschedule } );
    } else {
	Condor::RegisterEvicted( $test{$handle}{"RegisterEvicted"} );
    }

    if( defined $test{$handle}{"RegisterTimed"} )
    {
		Condor::RegisterTimed( $test{$handle}{"RegisterTimed"} , $test{$handle}{"RegisterTimedWait"});
    }
}


sub CompareText
{
    my $file = shift || croak "missing file argument";
    my $aref = shift || croak "missing array reference argument";
    my @skiplines = @_;
    my $linenum = 0;
	my $line;
	my $expectline;
	my $debuglevel = 4;

	debug("opening file $file to compare to array of expected results\n",$debuglevel);
    open( FILE, "<$file" ) || die "error opening $file: $!\n";
    
    while( <FILE> )
    {
	fullchomp($_);
	$line = $_;
	$linenum++;

	debug("linenum $linenum\n",$debuglevel);
	debug("\$line: $line\n",$debuglevel);
	debug("\$\$aref[0] = $$aref[0]\n",$debuglevel);

	debug("skiplines = \"@skiplines\"\n",$debuglevel);
	#print "grep returns ", grep( /^$linenum$/, @skiplines ), "\n";

	next if grep /^$linenum$/, @skiplines;

	$expectline = shift @$aref;
	if( ! defined $expectline )
	{
	    die "$file contains more text than expected\n";
	}
	fullchomp($expectline);

	debug("\$expectline: $expectline\n",$debuglevel);

	# if they match, go on
	next if $expectline eq $line;

	# otherwise barf
	warn "$file line $linenum doesn't match expected output:\n";
	warn "actual: $line\n";
	warn "expect: $expectline\n";
	return 0;
    }
	close(FILE);

    # barf if we're still expecting text but the file has ended
    ($expectline = shift @$aref ) && 
        die "$file incomplete, expecting:\n$expectline\n";

    # barf if there are skiplines we haven't hit yet
    foreach my $num ( @skiplines )
    {
	if( $num > $linenum )
	{
	    warn "skipline $num > # of lines in $file ($linenum)\n";
	    return 0;
	}
	croak "invalid skipline argument ($num)" if $num < 1;
    }
    
	debug("CompareText successful\n",$debuglevel);
    return 1;
}

sub IsFileEmpty
{
    my $file = shift || croak "missing file argument";
    return -z $file;
}

sub verbose_system 
{
	my $args = shift @_;


	my $catch = "vsystem$$";
	$args = $args . " 2>" . $catch;
	my $rc = 0xffff & system $args;

	if ($rc != 0) { 
		printf "system(%s) returned %#04x: ", $args, $rc;
	}

	if ($rc == 0) 
	{
		#print "ran with normal exit\n";
		return $rc;
	}
	elsif ($rc == 0xff00) 
	{
		print "command failed: $!\n";
	}
	elsif (($rc & 0xff) == 0) 
	{
		$rc >>= 8;
		print "ran with non-zero exit status $rc\n";
	}
	else 
	{
		print "ran with ";
		if ($rc &   0x80) 
		{
			$rc &= ~0x80;
			print "coredump from ";
		}
		print "signal $rc\n"
	}

	if( !open( MACH, "<$catch" )) { 
		warn "Can't look at command  output <$catch>:$!\n";
	} else {
    	while(<MACH>) {
        	print "ERROR: $_";
    	}
    	close(MACH);
	}

	return $rc;
}

sub MergeOutputFiles
{
	my $Testhash = shift || croak "Missing Test hash to Merge Output\n";
	my $basename = $Testhash->{corename};

	foreach my $m ( 0 .. $#{$Testhash->{extensions}} )
	{
		my $newlog = $basename . $Testhash->{extensions}[$m];
		#print "Creating core log $newlog\n";
		open(LOG,">$newlog") || return "1";
		print LOG "***************************** Merge Sublogs ***************************\n";
		foreach my $n ( 0 .. $#{$Testhash->{tests}} )
		{
			# get file if it exists
			#print "Add logs for test $Testhash->{tests}[$n]\n";
			my $sublog = $Testhash->{tests}[$n] . $Testhash->{extensions}[$m];
			if( -e "$sublog" )
			{
				print LOG "\n\n***************************** $sublog ***************************\n\n";
				open(INLOG,"<$sublog") || return "1";
				while(<INLOG>)
				{
					print LOG "$_";
				}
				close(INLOG);
			}
			else
			{
				#print "Can not find $sublog\n";
			}
			#print "$n = $Testhash->{tests}[$n]\n";
			#print "$m = $Testhash->{extensions}[$m]\n";
		}
		close(LOG);
	}
}

sub ParseMachineAds
{
    my $machine = shift || croak "missing machine argument";
    my $line = 0;
	my $variable;
	my $value;

	if( ! open(PULL, "condor_status -l $machine 2>&1 |") )
    {
		print "error getting Ads for \"$machine\": $!\n";
		return 0;
    }
    
    debug( "reading machine ads from $machine...\n" ,5);
    while( <PULL> )
    {
	fullchomp($_);
	debug("Raw AD is $_\n",5);
	$line++;

	# skip comments & blank lines
	next if /^#/ || /^\s*$/;

	# if this line is a variable assignment...
	if( /^(\w+)\s*\=\s*(.*)$/ )
	{
	    $variable = lc $1;
	    $value = $2;

	    # if line ends with a continuation ('\')...
	    while( $value =~ /\\\s*$/ )
	    {
		# remove the continuation
		$value =~ s/\\\s*$//;

		# read the next line and append it
		<PULL> || last;
		$value .= $_;
	    }

	    # compress whitespace and remove trailing newline for readability
	    $value =~ s/\s+/ /g;
	    fullchomp($value);

	
		# Do proper environment substitution
	    if( $value =~ /(.*)\$ENV\((.*)\)(.*)/ )
	    {
			my $envlookup = $ENV{$2};
	    	debug( "Found $envlookup in environment \n",5);
			$value = $1.$envlookup.$3;
	    }

	    debug( "$variable = $value\n" ,5);
	    
	    # save the variable/value pair
	    $machine_ads{$variable} = $value;
	}
	else
	{
	    debug( "line $line of $submit_file not a variable assignment... " .
		   "skipping\n" ,5);
	}
    }
	close(PULL);
    return 1;
}

sub FetchMachineAds
{
	return %machine_ads;
}

sub FetchMachineAdValue
{
	my $key = shift @_;
	if(exists $machine_ads{$key})
	{
		return $machine_ads{$key};
	}
	else
	{
		return undef;
	}
}

#
# Some tests need to wait to be started and as such we will
# use qedit to change the job add.
#

sub setJobAd
{
	my @status;
	my $qstatcluster = shift;
	my $qattribute = shift; # change which job ad?
	my $qvalue = shift;		# whats the new value?
	my $qtype = shift;		# quote if a string...
	my $cmd = "condor_qedit $qstatcluster $qattribute ";
	if($qtype eq "string") {
		$cmd = $cmd . "\"$qvalue\"";
	} else {
		$cmd = $cmd . "$qvalue";
	}
	print "Running this command: <$cmd> \n";
	# shhhhhhhh third arg 0 makes it hush its output
	my $qstat = CondorTest::runCondorTool($cmd,\@status,0);
	if(!$qstat)
	{
		print "Test failure due to Condor Tool Failure<$cmd>\n";
	    return(1)
	}
	foreach my $line (@status)
	{
		#print "Line: $line\n";
	}
}

#
# Is condor_q able to respond at this time? We'd like to get
# a valid response for whoever is asking.
#

sub getJobStatus
{
	my @status;
	my $qstatcluster = shift;
	my $cmd = "condor_q $qstatcluster -format %d JobStatus";
	# shhhhhhhh third arg 0 makes it hush its output
	my $qstat = CondorTest::runCondorTool($cmd,\@status,0);
	if(!$qstat)
	{
		print "Test failure due to Condor Tool Failure<$cmd>\n";
	    return(1)
	}

	foreach my $line (@status)
	{
		#print "jobstatus: $line\n";
		if( $line =~ /^(\d).*/)
		{
			return($1);
		}
		else
		{
			return(-1);
		}
	}
}

#
# Run a condor tool and look for exit value. Apply multiplier
# upon failure and return 1 on failure.
#

sub runCondorTool
{
	my $trymultiplier = 1;
	my $start_time = time; #get start time
	my $delta_time = 0;
	my $status = 1;
	my $done = 0;
	my $cmd = shift;
	my $arrayref = shift;
	# use unused third arg to skip the noise like the time
	my $quiet = shift;
	my $force = "";
	$force = shift;
	my $count = 0;
	my $catch = "runCTool$$";

	# clean array before filling

	my $attempts = 6;
	$count = 0;
	while( $count < $attempts) {
		@{$arrayref} = (); #empty return array...
		my @tmparray;
		debug( "Try command <$cmd>\n",4);
		open(PULL, "$cmd 2>$catch |");
		while(<PULL>)
		{
			fullchomp($_);
			debug( "Process: $_\n",4);
			push @tmparray, $_; # push @{$arrayref}, $_;
		}
		close(PULL);
		$status = $? >> 8;
		debug("Status is $status after command\n",4);
		if(( $status != 0 ) && ($attempts == ($count + 1)))
		{
				print "runCondorTool: $cmd timestamp $start_time failed!\n";
				print "************* std out ***************\n";
				foreach my $stdout (@tmparray) {
					print "STDOUT: $stdout \n";
				}
				print "************* std err ***************\n";
				if( !open( MACH, "<$catch" )) { 
					warn "Can't look at command output <$catch>:$!\n";
				} else {
    				while(<MACH>) {
        				print "ERROR: $_";
    				}
    				close(MACH);
				}
				print "************* GetQueue() ***************\n";
				GetQueue();
				print "************* GetQueue() DONE ***************\n";

				return(0);
		}

		if ($status == 0) {
			my $line = "";
			foreach my $value (@tmparray)
			{
				push @{$arrayref}, $value;
			}
			$done = 1;
			# There are times like the security tests when we want
			# to see the stderr even when the command works.
			if( (defined $force) && ($force ne "" )) {
				if( !open( MACH, "<$catch" )) { 
					warn "Can't look at command output <$catch>:$!\n";
				} else {
    				while(<MACH>) {
						fullchomp($_);
						$line = $_;
						push @{$arrayref}, $line;
    				}
    				close(MACH);
				}
			}
			my $current_time = time;
			$delta_time = $current_time - $start_time;
			debug("runCondorTool: its been $delta_time since call\n",4);
			return(1);
		}
		$count = $count + 1;
		debug("runCondorTool: iteration<$count> failed sleep 10 * $count \n",1);
		sleep((10*$count));
	}
	debug( "runCondorTool: $cmd worked!\n",1);

	return(0);
}

# Sometimes `which ...` is just plain broken due to stupid fringe vendor
# not quite bourne shells. So, we have our own implementation that simply
# looks in $ENV{"PATH"} for the program and return the "usual" response found
# across unicies. As for windows, well, for now it just sucks.
sub Which
{
	my $exe = shift(@_);

	if(!( defined  $exe)) {
		return "CT::Which called with no args\n";
	}
	my @paths = split /:/, $ENV{PATH};

	foreach my $path (@paths) {
		fullchomp($path);
		if (-x "$path/$exe") {
			return "$path/$exe";
		}
	}

	return "$exe: command not found";
}

# Lets be able to drop some extra information if runCondorTool
# can not do what it is supposed to do....... short and full
# output from condor_q 11/13

sub GetQueue
{
	my @cmd = ("condor_q", "condor_q -l" );
	foreach my $request (@cmd) {
		print "Queue command <$request>\n";
		open(PULL, "$request 2>&1 |");
		while(<PULL>)
		{
			fullchomp($_);
			print "GetQueue: $_\n";
		}
		close(PULL);
	}
}

#
# Cygwin's perl chomp does not remove cntrl-m but this one will
# and linux and windows can share the same code. The real chomp
# totals the number or changes but I currently return the modified
# array. bt 10/06
#

sub fullchomp
{
	push (@_,$_) if( scalar(@_) == 0);
	foreach my $arg (@_) {
		$arg =~ s/\012+$//;
		$arg =~ s/\015+$//;
	}
	return(0);
}

sub changeDaemonState
{
	my $timeout = 0;
	my $daemon = shift;
	my $state = shift;
	$timeout = shift; # picks up number of tries... back off on how soon we try.
	my $counter = 0;
	my $cmd = "";
	my $foundTotal = "no";
	my $status;
	my (@cmdarray1, @cmdarray2);

	print "Checking for $daemon being $state\n";
	if($state eq "off") {
		$cmd = "condor_off -fast -$daemon";
	} elsif($state eq "on") {
		$cmd = "condor_on -$daemon";
	} else {
		die "Bad state given in changeScheddState<$state>\n";
	}

	$status = runCondorTool($cmd,\@cmdarray1,2);
	if(!$status)
	{
		print "Test failure due to Condor Tool Failure<$cmd>\n";
		exit(1);
	}

	my $sleeptime = 0;
	$cmd = "condor_status -$daemon";
	while($counter < $timeout ) {
		$foundTotal = "no";
		@cmdarray2 = {};
		print "about to run $cmd try $counter previous sleep $sleeptime\n";
		$status = CondorTest::runCondorTool($cmd,\@cmdarray2,2);
		if(!$status)
		{
			print "Test failure due to Condor Tool Failure<$cmd>\n";
			exit(1)
		}

		foreach my $line (@cmdarray2)
		{
			print "<<$line>>\n";
			if($daemon eq "schedd") {
				if( $line =~ /.*Total.*/ ) {
					# hmmmm  scheduler responding
					print "Schedd running\n";
					$foundTotal = "yes";
				}
			} elsif($daemon eq "startd") {
				if( $line =~ /.*Backfill.*/ ) {
					# hmmmm  Startd responding
					print "Startd running\n";
					$foundTotal = "yes";
				}
			}
		}

		if( $state eq "on" ) {
			if($foundTotal eq "yes") {
				# is running again
				return(1);
			} else {
				$counter = $counter + 1;
				$sleeptime = ($counter**2);
				sleep($sleeptime);
			}
		} elsif( $state eq "off" ) {
			if($foundTotal eq "no") {
				#is stopped
				return(1);
			} else {
				$counter = $counter + 1;
				$sleeptime = ($counter**2);
				sleep($sleeptime);
			}
		}

	}
	print "Timeout watching for $daemon state change to <<$state>>\n";
	return(0);
}

#######################
#
# find_pattern_in_array
#
#	Find the array index which contains a particular pattern.
#
# 	First used to strip off variant line in output from
#	condor_q -direct when passed quilld, schedd and rdbms
#	prior to comparing the arrays collected from the output
#	of each command....

sub find_pattern_in_array
{
    my $pattern = shift;
    my $harray = shift;
    my $place = 0;

    debug( "Looking for <<$pattern>> size <<$#{$harray}>>\n",4);
    foreach my $member (@{$harray}) {
        debug( "consider $member\n",5);
        if($member =~ /.*$pattern.*/) {
            debug( "Found <<$member>> line $place\n",4);
            return($place);
        } else {
            $place = $place + 1;
        }
    }
    print "Got to end without finding it....<<$pattern>>\n";
    return(-1);
}

#######################
#
# compare_arrays
#
#	We hash each line from an array and verify that each array has the same contents
# 	by having a value for each key equalling the number of arrays. First used to
#	compare output from condor_q -direct quilld, schedd and rdbms

sub compare_arrays
{
    my $startrow = shift;
    my $endrow = shift;
    my $numargs = shift;
    my %lookup = ();
    my $counter = 0;
    debug( "Check $numargs starting row $startrow end row $endrow\n",4);
    while($counter < $numargs) {
        my $href = shift;
        my $thisrow = 0;
        for my $item (@{$href}) {
            if( $thisrow >= $startrow) {
                if($counter == 0) {
                    #initialize each position
                    $lookup{$item} = 1;
                } else {
                    $lookup{$item} = $lookup{$item} + 1;
                    debug( "Set at:<$lookup{$item}:$item>\n",4);
                }
                debug( "Store: $item\n",5);
            } else {
                debug( "Skip: $item\n",4);
            }
            $thisrow = $thisrow + 1;
        }
        $counter = $counter + 1;
    }
    #loaded up..... now look!
    foreach my $key (keys %lookup) {
        debug( " $key equals $lookup{$key}\n",4);
		if($lookup{$key} != $numargs) {
			print "Arrays are not the same! key <$key> is $lookup{$key} and not $numargs\n";
			return(1);
		}
    }
	return(0);
}

##############################################################################
#
# spawn_cmd
#
#	For a process to start two processes. One will start the passed system
# 	call and the other will wait around for completion where it will stuff 
# 	the results where we can check when we care
#
##############################################################################

sub spawn_cmd
{
	my $cmdtowatch = shift;
	my $resultfile = shift;
	my $result;
	my $toppid = fork();
	my $res;
	my $child;
	my $mylog;
	my $retval;

	if($toppid == 0) {

		my $pid = fork();
		if ($pid == 0) {
			# child 1 code....
			$mylog = $resultfile . ".spawn";
			open(LOG,">$mylog") || die "Can not open log: $mylog: $!\n";
			$res = 0;
			print LOG "Starting this cmd <$cmdtowatch>\n";
			$res = system("$cmdtowatch");
			print LOG "Result from $cmdtowatch is <$res>\n";
			print LOG "File to watch is <$resultfile>\n";
			if($res != 0) {
				print LOG " failed\n";
				close(LOG);
				exit(1);
			} else {
				print LOG " worked\n";
				close(LOG);
				exit(0);
			}
		} 

		open(RES,">$resultfile") || die "Can't open results file<$resultfile>:$!\n";
		$mylog = $resultfile . ".watch";
		open(LOG,">$mylog") || die "Can not open log: $mylog: $!\n";
		print LOG "waiting on pid <$pid>\n";
		while(($child = waitpid($pid,0)) != -1) { 
			$retval = $?;
			debug( "Child status was <<$retval>>\n",4);
			if( WIFEXITED( $retval ) && WEXITSTATUS( $retval ) == 0 ) {
				debug( "Monitor done and status good!\n",4);
				$retval = 0;
			} else {
				my $status = WEXITSTATUS( $retval );
				debug( "Monitor done and status bad<<$status>>!\n",4);
				$retval = 1;
			}
			print RES "Exit $retval \n";
			print LOG "Pid $child res was $retval\n";
		}
		print LOG "Done waiting on pid <$pid>\n";
		close(RES);
		close(LOG);

		exit(0);
	} else {
		# we are done
		return($toppid);
	}
}

##############################################################################
#
# getFqdnHost
#
# hostname sometimes does not return a fully qualified
# domain name and we must ensure we have one. We will call
# this function in the tests wherever we were calling
# hostname.
##############################################################################

sub getFqdnHost
{
	my $host = hostfqdn();
	return($host);
}

##############################################################################
#
# PersonalSearchLog
#
# Serach a log for a pattern
#
##############################################################################

sub PersonalSearchLog
{
    my $pid = shift;
    my $personal = shift;
    my $searchfor = shift;
    my $logname = shift;

	my $logdir = `condor_config_val log`;
	fullchomp($logdir);

    #my $logloc = $pid . "/" . $pid . $personal . "/log/" . $logname;
    my $logloc = $logdir . "/" . $logname;
    CondorTest::debug("Search this log <$logloc> for <$searchfor>\n",2);
    open(LOG,"<$logloc") || die "Can not open logfile<$logloc>: $!\n";
    while(<LOG>) {
        if( $_ =~ /$searchfor/) {
            CondorTest::debug("FOUND IT! $_",2);
            return(0);
        }
    }
    return(1);
}

##############################################################################
#
# PersonalPolicySearchLog
#
# Serach a log for a security policy
#
##############################################################################


sub PersonalPolicySearchLog
{
    my $pid = shift;
    my $personal = shift;
    my $policyitem = shift;
    my $logname = shift;

	my $logdir = `condor_config_val log`;
	fullchomp($logdir);

    #my $logloc = $pid . "/" . $pid . $personal . "/log/" . $logname;
    my $logloc = $logdir . "/" . $logname;
    debug("Search this log <$logloc> for <$policyitem>\n",2);
    open(LOG,"<$logloc") || die "Can not open logfile<$logloc>: $!\n";
    while(<LOG>) {
        if( $_ =~ /^.*Security Policy.*$/) {
            while(<LOG>) {
                if( $_ =~ /^\s*$policyitem\s*=\s*\"(\w+)\"\s*$/ ) {
                    #print "FOUND IT! $1\n";
                    if(!defined $securityoptions{$1}){
                        debug("Returning <<$1>>\n",2);
                        return($1);
                    }
                }
            }
        }
    }
    return("bad");
}

sub OuterPoolTest
{
	my $cmd = "condor_config_val log";
	my $locconfig = "";
    debug( "Running this command: <$cmd> \n",2);
    # shhhhhhhh third arg 0 makes it hush its output
	my $logdir = `condor_config_val log`;
	fullchomp($logdir);
	debug( "log dir is<$logdir>\n",2);
	if($logdir =~ /^.*condor_tests.*$/){
		print "Running within condor_tests\n";
		if($logdir =~ /^.*TestingPersonalCondor.*$/){
			debug( "Running with outer testing personal condor\n",2);
			return(1);
		}
	} else {
		print "Running outside of condor_tests\n";
	}
	return(0);
}

sub PersonalCondorTest
{
	my $submitfile = shift;
	my $testname = shift;
	my $cmd = "condor_config_val log";
	my $locconfig = "";
    print "Running this command: <$cmd> \n";
    # shhhhhhhh third arg 0 makes it hush its output
	my $logdir = `condor_config_val log`;
	fullchomp($logdir);
	print "log dir is<$logdir>\n";
	if($logdir =~ /^.*condor_tests.*$/){
		print "Running within condor_tests\n";
		if($logdir =~ /^.*TestingPersonalCondor.*$/){
			print "Running with outer testing personal condor\n";
			#my $testname = findOutput($submitfile);
			#print "findOutput saya test is $testname\n";
			my $version = "local";
			
			# get a local scheduler running (side a)
			my $configloc = CondorPersonal::StartCondor( $testname, "x_param.basic_personal" ,$version);
			my @local = split /\+/, $configloc;
			$locconfig = shift @local;
			my $locport = shift @local;
			
			debug("---local config is $locconfig and local port is $locport---\n",2);

			#$ENV{CONDOR_CONFIG} = $locconfig;
		}
	} else {
		print "Running outside of condor_tests\n";
	}
	return($locconfig);
}

sub findOutput
{
	my $submitfile = shift;
	open(SF,"<$submitfile") or die "Failed to open <$submitfile>:$!\n";
	my $testname = "UNKNOWN";
	my $line = "";
	while(<SF>) {
		fullchomp($_);
		$line = $_;
		if($line =~ /^\s*[Ll]og\s+=\s+(.*)(\..*)$/){
			$testname = $1;
			my $previouslog = $1 . $2;
			system("rm -f $previouslog");
		}
	}
	close(SF);
	print "findOutput returning <$testname>\n";
	if($testname eq "UNKNOWN") {
		print "failed to find testname in this submit file:$submitfile\n";
		system("cat $submitfile");
	}
	return($testname);
}

# Call down to Condor Perl Module for now

sub debug
{
    my $string = shift;
	my $level = shift;
	my $newstring = "CT:$string";
	Condor::debug($newstring,$level);
}

##############################################################################
#
# Lets stash the test name which will be consistent even
# with multiple personal condors being used by the test
#
##############################################################################


sub StartPersonal
{
    my $testname = shift;
    my $paramfile = shift;
    my $version = shift;
	
	$handle = $testname;
    debug("Starting Perosnal($$) for $testname/$paramfile/$version\n",2);

    my $configloc = CondorPersonal::StartCondor( $testname, $paramfile ,$version);
    return($configloc);
}

sub KillPersonal
{
	my $personal_config = shift;
	my $logdir = "";
	if($personal_config =~ /^(.*[\\\/])(.*)$/) {
		print "LOG dir is $1/log\n";
		$logdir = $1 . "/log";
	} else {
		debug("KillPersonal passed this config<<$personal_config>>\n",2);
		die "Can not extract log directory\n";
	}
	debug("Doing core ERROR check in  KillPersonal\n",2);
	$failed_coreERROR = CoreCheck($handle, $logdir, $teststrt, $teststop);
	CondorPersonal::KillDaemonPids($personal_config);
}

##############################################################################
#
# core and ERROR checking code plus ERROR exemption handling 
#
##############################################################################

sub ShouldCheck_coreERROR
{
	my $logdir = `condor_config_val log`;
	fullchomp($logdir);
	my $testsrunning = CountRunningTests();
	if(($logdir =~ /TestingPersonalCondor/) &&($testsrunning > 1)) {
		# no because we are doing concurrent testing
		return(0);
	}
	my $saveme = $handle . ".saveme";
	debug("Not /TestingPersonalCondor/ based, saveme is $saveme\n",2);
	debug("Logdir is $logdir\n",2);
	if($logdir =~ /$saveme/) {
		# no because KillPersonal will do it
		return(0);
	}
	debug("Does not look like its in a personal condor\n",2);
	return(1);
}

sub CoreCheck {
	my $test = shift;
	my $logdir = shift;
	my $tstart = shift;
	my $tend = shift;
	my $count = 0;
	my $scancount = 0;
	my $fullpath = "";
	
	debug("Checking <$logdir> for test <$test>\n",2);
	my @files = `ls $logdir`;
	foreach my $perp (@files) {
		fullchomp($perp);
		$fullpath = $logdir . "/" . $perp;
		if(-f $fullpath) {
			if($fullpath =~ /^.*\/(core.*)$/) {
				# returns printable string
				debug("Checking <$logdir> for test <$test> Found Core <$fullpath>\n",2);
				my $filechange = GetFileTime($fullpath);
				# running sequentially or wrapped core should always
				# belong to the current test. Even if the test has ended
				# assign blame and move file so we can investigate.
				my $newname = MoveCoreFile($fullpath,$coredir);
				print "\nFound core <$fullpath>\n";
				AddFileTrace($fullpath,$filechange,$newname);
				$count += 1;
			} else {
				debug("Checking <$fullpath> for test <$test> for ERROR\n",2);
				$scancount = ScanForERROR($fullpath,$test,$tstart,$tend);
				$count += $scancount;
				debug("After ScanForERROR error count <$scancount>\n",2);
			}
		} else {
			debug( "Not File: $fullpath\n",2);
		}
	}
	
	return($count);
}

sub ScanForERROR
{
	my $daemonlog = shift;
	my $testname = shift;
	my $tstart = shift;
	my $tend = shift;
	my $count = 0;
	my $ignore = 1;
	open(MDL,"<$daemonlog") or die "Can not open daemon log<$daemonlog>:$!\n";
	my $line = "";
	while(<MDL>) {
		fullchomp();
		$line = $_;
		# ERROR preceeded by white space and trailed by white space, :, ; or -
		if($line =~ /^\s*(\d+\/\d+\s+\d+:\d+:\d+)\s+ERROR[\s;:\-!].*$/){
			debug("$line TStamp $1\n",2);
			$ignore = IgnoreError($testname,$1,$line,$tstart,$tend);
			if($ignore == 0) {
				$count += 1;
				print "\nFound ERROR <$line>\n";
				AddFileTrace($daemonlog, $1, $line);
			}
		} elsif($line =~ /^\s*(\d+\/\d+\s+\d+:\d+:\d+)\s+.*\s+ERROR[\s;:\-!].*$/){
			debug("$line TStamp $1\n",2);
			$ignore = IgnoreError($testname,$1,$line,$tstart,$tend);
			if($ignore == 0) {
				$count += 1;
				print "\nFound ERROR <$line>\n";
				AddFileTrace($daemonlog, $1, $line);
			}
		} elsif($line =~ /^.*ERROR.*$/){
			debug("Skipping this error<<$line>> \n",2);
		}
	}
	close(MDL);
	return($count);
}

sub CheckTriggerTime
{	
	my $teststartstamp = shift;
	my $timestring = shift;
	my $tsmon = 0;

	my ($sec,$min,$hour,$mday,$mon,$year,$wday,$yday,$isdst) = localtime();

	if($timestring =~ /^(\d+)\/(\d+)\s+(\d+):(\d+):(\d+)$/) {
		$tsmon = $1 - 1;
		my $timeloc = timelocal($5,$4,$3,$mday,$tsmon,$year,0,0,$isdst);
		print "timestamp from test start is $teststartstamp\n";
		print "timestamp fromlocaltime is $timeloc \n";
		if($timeloc > $teststartstamp) {
			return(1);
		}
	}
}

sub GetFileChangeTime
{
	my $file = shift;
	my ($dev, $ino, $mode, $nlink, $uid, $gid, $rdev, $size, $atime, $mtime, $ctime, $blksize, $blocks) = stat($file);

	return($ctime);
}

sub GetFileTime
{
	my $file = shift;
	my ($dev, $ino, $mode, $nlink, $uid, $gid, $rdev, $size, $atime, $mtime, $ctime, $blksize, $blocks) = stat($file);

	my ($sec,$min,$hour,$mday,$mon,$year,$wday,$yday,$isdst) = localtime($ctime);

	$mon = $mon + 1;
	$year = $year + 1900;

	return("$mon/$mday $hour:$min:$sec");
}

sub AddFileTrace
{
	my $file = shift;
	my $time = shift;
	my $entry = shift;

	my $tracefile = $coredir . "/core_error_trace";
	my $newtracefile = $coredir . "/core_error_trace.new";

	# make sure the trace file exists
	if(!(-f $tracefile)) {
		open(TF,">$tracefile") or die "Can not create ERROR/CORE trace file<$tracefile>:$!\n";
		print TF "Tracking file for core files and ERROR prints in daemonlogs\n";
		close(TF);
	}
	open(TF,"<$tracefile") or die "Can not create ERROR/CORE trace file<$tracefile>:$!\n";
	open(NTF,">$newtracefile") or die "Can not create ERROR/CORE trace file<$newtracefile>:$!\n";
	while(<TF>) {
		print NTF "$_";
	}
	close(TF);
	my $buildentry = "$time	$file	$entry\n";
	print NTF "$buildentry";
	debug("\n$buildentry",2);
	close(NTF);
	system("mv $newtracefile $tracefile");

}

sub MoveCoreFile
{
	my $oldname = shift;
	my $targetdir = shift;
	my $newname = "";
	# get number for core file rename into trace dir
	my $entries = CountFileTrace();
	if($oldname =~ /^.*\/(core.*)\s*.*$/) {
		$newname = $coredir . "/" . $1 . "_$entries";
		system("mv $oldname $newname");
		#system("rm $oldname");
		return($newname);
	} else {
		debug("Only move core files<$oldname>\n",2);
		return("badmoverequest");
	}
}

sub CountFileTrace
{
	my $tracefile = $coredir . "/core_error_trace";
	my $count = 0;

	open(CT,"<$tracefile") or die "Can not count<$tracefile>:$!\n";
	while(<CT>) {
		$count += 1;
	}
	close(CT);
	return($count);
}

sub LoadExemption
{
	my $line = shift;
	debug("LoadExemption: <$line>\n",2);
    my ($test, $required, $message) = split /,/, $line;
    my $save = $required . "," . $message;
    if(exists $exemptions{$test}) {
        push @{$exemptions{$test}}, $save;
		debug("LoadExemption: added another for test $test\n",2);
    } else {
        $exemptions{$test} = ();
        push @{$exemptions{$test}}, $save;
		debug("LoadExemption: added new for test $test\n",2);
    }
}

sub IgnoreError
{
	my $testname = shift;
	my $errortime = shift;
	my $errorstring = shift;
	my $tstart = shift;
	my $tend = shift;
	my $timeloc = 0;
	my $tsmon = 0;

	my ($sec,$min,$hour,$mday,$mon,$year,$wday,$yday,$isdst) = localtime();

	if($errortime =~ /^(\d+)\/(\d+)\s+(\d+):(\d+):(\d+)$/) {
		$tsmon = $1 - 1;
		$timeloc = timelocal($5,$4,$3,$mday,$tsmon,$year,0,0,$isdst);
	} else {
		die "Time string into IgnoreError: Bad Format: $errortime\n";
	}
	debug("Start <$tstart> ERROR <$timeloc> End <$tend>\n",2);

	# First item we care about is if this ERROR hapened during this test
	if(($tstart == 0) && ($tend == 0)) {
		# this is happening within a personal condor so do not ignore
	} elsif( ($timeloc < $tstart) || ($timeloc > $tend)) {
		debug("IgnoreError: Did not happen during test\n",2);
		return(1); # not on our watch so ignore
	}

	# no no.... must acquire array for test and check all substrings
	# against current large string.... see DropExemptions below
	debug("IgnoreError called for test <$testname> and string <$errorstring>\n",2);
	# get list of per/test specs
	if( exists $exemptions{$testname}) {
		my @testarray = @{$exemptions{$testname}};
		foreach my $oneexemption (@testarray) {
			my( $must, $partialstr) = split /,/,  $oneexemption;
			my $quoted = quotemeta($partialstr);
			debug("Looking for <$quoted> in this error <$errorstring>\n",2);
			if($errorstring =~ m/$quoted/) {
				debug("IgnoreError: Valid exemption\n",2);
				debug("IgnoreError: Ignore ******** <<$quoted>> ******** \n",2);
				return(1);
			} 
		}
	}
	# no exemption for this one
	return(0);
}

sub DropExemptions
{
	foreach my $key (sort keys %exemptions) {
    	print "$key\n";
    	my @array = @{$exemptions{$key}};
    	foreach my $p (@array) {
        	print "$p\n";
    	}
	}
}

##############################################################################
#
#	File utilities. We want to keep an up to date record of every
#	test currently running. If we only have one, then the tests are
#	executing sequentially and we can do full core and ERROR detecting
#
##############################################################################
# Tracking Running Tests
# my $RunningFile = "RunningTests";
# my $LOCK_EXCLUSIVE = 2;
# my $UNLOCK = 8;
# my $TRUE = 1;
# my $FALSE = 0;
my $debuglevel = 2;

sub FindControlFile
{
	my $cwd = getcwd();
	my $runningfile = "";
	fullchomp($cwd);
	debug( "Current working dir is <$cwd>\n",$debuglevel);
	if($cwd =~ /^(.*condor_tests)(.*)$/) {
		$runningfile = $1 . "/" . $RunningFile;
		debug( "Running file test is <$runningfile>\n",$debuglevel);
		if(!(-d $runningfile)) {
			debug( "Creating control file directory<$runningfile>\n",$debuglevel);
			system("mkdir -p $runningfile");
		}
	} else {
		die "Lost relative to where <$RunningFile> is :-(\n";
	}
	return($runningfile);
}

sub CleanControlFile
{
	my $controlfile = FindControlFile();
	if( -d $controlfile) {
		debug( "Cleaning old active test running file holding:\n",$debuglevel);
		system("ls $controlfile");
		system("rm -rf $controlfile");
	} else {
		debug( "Creating new active test running file\n",$debuglevel);
	}
	system("mkdir -p $controlfile");
}


sub CountRunningTests
{
	my $runningfile = FindControlFile();
	my $ret;
	my $line = "";
	my $count = 0;
	my $here = getcwd();
	chdir($runningfile);
	my $targetdir = '.';
	opendir DH, $targetdir or die "Can not open $targetdir:$!\n";
	foreach my $file (readdir DH) {
		next if $file =~ /^\.\.?$/;
		next if (-d $file) ;
		$count += 1;
		debug("Counting this test<$file> count now <$count>\n",2);
	}
	chdir($here);
	return($count);
}

sub AddRunningTest
{
	my $test = shift;
	my $runningfile = FindControlFile();
	my $retRF;
	my $tmpfile;
	my $line = "";
	debug( "Wanting to add <$test> to running tests\n",$debuglevel);
	system("touch $runningfile/$test");
}

sub RemoveRunningTest
{
	my $test = shift;
	my $runningfile = FindControlFile();
	my $retRF;
	my $tmpfile;
	my $line = "";
	debug( "Wanting to remove <$test> from running tests\n",$debuglevel);
	system("rm -f $runningfile/$test");
}

sub IsThisWindows
{
	my $path = CondorTest::Which("cygpath");
	debug("Path return from which cygpath: $path\n",2);
	if($path =~ /^.*\/bin\/cygpath.*$/ ) {
		#print "This IS windows\n";
		return(1);
	}
	#print "This is NOT windows\n";
	return(0);
}

sub IsThisNightly
{
	my $mylocation = shift;
	my $configlocal = "";
	my $configmain = "";

	debug("IsThisNightly passed <$mylocation>\n",2);
	if($mylocation =~ /^.*(\/execute\/).*$/) {
		#print "Nightly testing\n";
		$configlocal = "../condor_examples/condor_config.local.central.manager";
		$configmain = "../condor_examples/condor_config.generic";
		if(!(-f $configmain)) {
			system("ls ..");
			system("ls ../condor_examples");
			die "No base config file!!!!!\n";
		}
		return(1);
	} else {
		#print "Workspace testing\n";
		$configlocal = "../condor_examples/condor_config.local.central.manager";
		$configmain = "../condor_examples/condor_config.generic";
		if(!(-f $configmain)) {
			system("ls ..");
			system("ls ../condor_examples");
			die "No base config file!!!!!\n";
		}
		return(0);
	}
}

1;
