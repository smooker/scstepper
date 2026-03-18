#!/usr/bin/perl
use strict;
use warnings;

open FILE, "<Makefile";

my $num = 0;

my $flag = 0;


while(<FILE>) {
#	print $_;
#C_INCLUDES =  \
	s/\r\n/\n/g;
	s/\\//g;
	s/-I//g;
	if (m/^C_INCLUDES/ || ($flag == 1) ) {
		if ( !m/^C_INCLUDES/ ) {
			print $_;
		}
		$flag = 1;
	  if ($_ eq "\n" ) {
			$flag = 0;
		}
	}
}

close FILE;