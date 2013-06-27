#!/usr/bin/env perl
#
#  timeR : Deterministic profiling for R
#  Copyright (C) 2013  TU Dortmund Informatik LS XII
#  Inspired by r-timed from the Reactor group at Purdue,
#    http://r.cs.purdue.edu/
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, a copy is available at
#  http://www.r-project.org/Licenses/
#

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
        push @binorder, $name;
    }
}

foreach (@binorder) {
    say "$_\t",join("\t", @{$bins{$_}});
}
