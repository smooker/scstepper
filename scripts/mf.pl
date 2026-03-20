#!/usr/bin/perl
# mf.pl — extract C_INCLUDES from Makefile (strips CubeMX CR/LF, removes -I prefix)
# Output used by Qt Creator / makefiles.sh for include path list

use strict;
use warnings;

my $makefile = (defined $ARGV[0]) ? $ARGV[0] : do {
    # run from scripts/ or project root
    my $dir = $0;
    $dir =~ s|/[^/]+$||;
    "$dir/../Makefile";
};

open(my $fh, '<', $makefile) or die "Cannot open $makefile: $!";

my $flag = 0;

while (<$fh>) {
    s/\r\n/\n/g;
    s/\\//g;
    s/-I//g;

    if (m/^C_INCLUDES/ || $flag) {
        if (!m/^C_INCLUDES/) {
            print $_;
        }
        $flag = 1;
        if ($_ eq "\n") {
            $flag = 0;
        }
    }
}

close $fh;
