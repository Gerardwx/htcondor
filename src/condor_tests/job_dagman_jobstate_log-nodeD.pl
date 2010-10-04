#! /usr/bin/env perl

open (OUTPUT, "condor_hold $ARGV[0] 2>&1 |") or die "Can't fork: $!";
while (<OUTPUT>) {
	print "$_";
}
close (OUTPUT) or die "Condor_hold failed: $?";

sleep 10;

open (OUTPUT, "condor_release $ARGV[0] 2>&1 |") or die "Can't fork: $!";
while (<OUTPUT>) {
	print "$_";
}
close (OUTPUT) or die "Condor_release failed: $?";

sleep 20;

print "Node D succeeded\n";
