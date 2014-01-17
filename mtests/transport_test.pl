# read in config files: system first, then user
use Data::Dumper;
$return = 0;
my @file_list = ("./test_spec", "../test_spec", "../../mtests/test_spec");
ARG: while (@ARGV && $ARGV[0] =~ s/^-(?=.)//) {
    OPT: for (shift @ARGV) {
         m/^$/       && do {                             next ARG; };
         m/^-$/      && do {                             last ARG; };
	 m/^f.*/      && do {@file_list = (); push (@file_list, shift(@ARGV)); 	 print "File list is @file_list\n"; next ARG;};
         m/^o.*/      && do {$output_file = shift(@ARGV);
			    open(OUTFILE, ">", "$output_file") 
				or die "cannot open > $output_file: $!";
			    next ARG; };
         s/^l//      && do { $Generate_Listing++;        redo OPT; };
         s/^i(.*)//  && do { $In_Place = $1 || ".bak";   next ARG; };
         print("Unknown option: $_");
    }
}


for (@file_list)
{
  $file = $_; 
  next unless -r $file;
  unless ($return = do $file) {
    warn "couldn't parse $file: $@" if $@;
    warn "couldn't do $file: $!"    unless defined $return;
    warn "couldn't run $file"       unless $return;
  }
}
die "No test spec file found in list : @file_list\n" unless $return;

$change = 1;

while ( my ($key, $value) = each(%test_set) ) {
    print "$key => ".@{$value}." ". Dumper($value)."\n";
    if (@{$value} == 0) {
	$test_set{$key}=undef;
	$test_set{$key} = [];
	$ret = push @{$test_set{$key}}, $value;
    }
    $change = 1;
    while($change) {
	$change = 0;
	$value = $test_set{$key};
	my $i = 0;
	while( $i < scalar @{$value}) {
	    $this_line = @{$value}[$i];
	    @components = split(/ /, $this_line);
	    $prefix = "";
	    if ($components[0] eq "") {shift(@components);}
	    component_loop : while ($element = shift(@components)) {
		if ($element =~ /.*:.*/) {
		    $prefix = "$prefix $element";
		} else {
		    # if the element has no colon
		    if (! exists $macro{$element}) {
			printf("Macro $element not defined\n");
			next;
		    }
		    $postfix = join(' ', @components);
		    my @new_elements = ();
		    for my $j (0 .. @{$macro{$element}}-1) {
			my $string = "$prefix $macro{$element}[$j] $postfix";
			$string =~ s/^\s+//;
			$string =~ s/\s+$//; 
			push @new_elements, $string;
		    }
		    splice(@{$value}, $i, 1, @new_elements);
		    $i += scalar(@new_elements);
		    $change = 1;
		    $test_set{$key} = $value;
		    last component_loop;
		}
	    }
	    $i++;
	}
    }
}



while ( my ($key, $value) = each(%test_set) ) {
  print "$key $#value=> ".scalar(@{$value}). Dumper($value)."\n";
}
while ( my ($transport, $value) = each(%test_set) ) {
  $value = $test_set{$transport};
  foreach (@{$value}) {
    $_ =~ s/(\w+):(\w+)/-\1 \2/g;
    $_ = "./trans_test -transport $transport $_ 2> /dev/null";
    print "Running: $_ \n";
    my $output = `$_`;
    my $my_exit_code = $?;
    if ($my_exit_code != 0) {
      print ("Error while running $_\n");
      print ("Output was:\n");
      print "$output";
    } else {
      print ("success\n");
      print OUTFILE "$output";
    }
  }
}

close OUTFILE;
