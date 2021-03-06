package ExtUtils::MM;

use strict;
use Config;
use vars qw(@ISA $VERSION);
$VERSION = 0.04;

require ExtUtils::Liblist;

# Original version.
# require ExtUtils::MakeMaker;
# Solaris/ON build version.
use ExtUtils::MakeMaker qw($Verbose);

@ISA = qw(ExtUtils::Liblist ExtUtils::MakeMaker);

=head1 NAME

ExtUtils::MM - OS adjusted ExtUtils::MakeMaker subclass

=head1 SYNOPSIS

  require ExtUtils::MM;
  my $mm = MM->new(...);

=head1 DESCRIPTION

B<FOR INTERNAL USE ONLY>

ExtUtils::MM is a subclass of ExtUtils::MakeMaker which automatically
chooses the appropriate OS specific subclass for you
(ie. ExtUils::MM_Unix, etc...).

It also provides a convenient alias via the MM class (I didn't want
MakeMaker modules outside of ExtUtils/).

This class might turn out to be a temporary solution, but MM won't go
away.

=cut

{
    # Convenient alias.
    package MM;
    use vars qw(@ISA);
    @ISA = qw(ExtUtils::MM);
    sub DESTROY {}
}

my %Is = ();
$Is{VMS}    = 1 if $^O eq 'VMS';
$Is{OS2}    = 1 if $^O eq 'os2';
$Is{MacOS}  = 1 if $^O eq 'MacOS';
if( $^O eq 'MSWin32' ) {
    Win32::IsWin95() ? $Is{Win95} = 1 : $Is{Win32} = 1;
}
$Is{UWIN}   = 1 if $^O eq 'uwin';
$Is{Cygwin} = 1 if $^O eq 'cygwin';
$Is{NW5}    = 1 if $Config{osname} eq 'NetWare';  # intentional
$Is{BeOS}   = 1 if $^O =~ /beos/i;    # XXX should this be that loose?
$Is{DOS}    = 1 if $^O eq 'dos';

# Original version.
# $Is{Unix}   = 1 if !keys %Is;

# Start of special test for Solaris/ON builds.
if (! keys(%Is)) {
    if ($^O  eq 'solaris' && grep(/^ENV(?:CPPFLAGS|LDLIBS)\d+$/, keys(%ENV))) {
        $Is{Solaris_ON} = 1;
    } else {
        $Is{Unix} = 1;
    }
}
# End of special test for Solaris/ON builds

if( $Is{NW5} ) {
    $^O = 'NetWare';
    delete $Is{Win32};
}

_assert( keys %Is == 1 );
my($OS) = keys %Is;


my $class = "ExtUtils::MM_$OS";
eval "require $class" unless $INC{"ExtUtils/MM_$OS.pm"};
die $@ if $@;
unshift @ISA, $class;


sub _assert {
    my $sanity = shift;
    die sprintf "Assert failed at %s line %d\n", (caller)[1,2] unless $sanity;
    return;
}
