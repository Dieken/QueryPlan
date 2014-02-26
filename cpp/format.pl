#!/usr/bin/perl
use Getopt::Long;
use strict;
use warnings;

die "Usage: g++ -std=c++11 -E some.cpp | $0 [--only] ModuleName... | astyle\n" unless @ARGV > 0;
my $re = join("|", @ARGV);
my $only = 0;

GetOptions('only!'      => \$only);

while (<STDIN>) {
    if (/\s*class\s(?:$re)\s/) {
        # break line after "{" and ";"
        s/[{;]\s/$&\n/g;

        # break line after "public:" and "private:"
        s/\S:\s/$&\n/g;

        # break line and insert blank line after "}"
        s/}\s/$&\n\n/g;

        # break line after "),"
        s/\),/$&\n/g;

        # break line before "<<"
        s/<</\n$&/g;

        print if $only;
    }

    print unless $only;
}

