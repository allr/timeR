#!/usr/bin/env perl

use warnings;
use strict;
use feature ':5.10';

# copy lines until the first comment line
while (<>) {
    print;
    last if /^#/;
}

my %bins;
my @binorder;

# read bins
while (<>) {
    chomp;
    my @elems = split "\t", $_;
    my $name = shift @elems;

    if (exists($bins{$name})) {
        # add to existing bin
        my @new;

        for (my $i = 0; $i < scalar(@elems) - 1; $i++) {
            push @new, $elems[$i] + ${$bins{$name}}[$i];
        }

        push @new, ($elems[-1] || $bins{$name}[-1]);

        $bins{$name} = \@new;
    } else {
        # new bin, just copy
        $bins{$name} = \@elems;
        say join(" / ", @{$bins{$name}});
        push @binorder, $name;
    }
}

foreach (@binorder) {
    say "$_\t",join("\t", @{$bins{$_}});
}
