=head1 NAME

YaPI::LdapServer

=head1 PREFACE

This package is the public Yast2 API to managing a LDAP Server.

=head1 SYNOPSIS

use YaPI::LdapServer

\@dbList = ReadDatabaseList()

 Returns a List of databases (suffix).

$bool = AddDatabase(\%valueMap)

 Creates a new database section in the configuration file

$bool = EditDatabase($suffix,\%valueMap)

 Edit the database section with the suffix $suffix.

\%valueMap = ReadDatabase($suffix)

 Read the database section with the suffix $suffix.

\@indexList = ReadIndex($suffix)

 Returns a List of Maps with all index statements for this database

$bool = AddIndex($suffix,\%indexMap)

 Add a new index statement %indexMap to the database section

$bool = EditIndex($suffix,$index_md5,\%indexMap)

 Replace the index $index_md5 in the database section

$bool = DeleteIndex($suffix,$index_md5)

 Delete the index $index_md5 statement in the database section

$bool = RecreateIndex($suffix)

 Regenerate indices

\@list = ReadSchemaIncludeList()

 Returns a list of all included schema files

$bool = WriteSchemaIncludeList(\@list)

 Writes all schema includes preserving order.

\@list = ReadAllowList()

 Returns a list of allow statements.

$bool = WriteAllowList(\@list)

 Replaces the complete allow option with the specified list

$loglevel = ReadLoglevel()

 Read the loglevel bitmask.

$bool = AddLoglevel($bit)

 Set the given loglevel bit to 1 in the current bitmask.

$bool = DeleteLoglevel($bit)

 Set the given loglevel bit to 0 in the current bitmask.

$bool = WriteLoglevel($loglevel)

 Replaces the loglevel bitmask.

ModifyService($status)

 Turn on/of the LDAP server runnlevel script

SwitchService($status)

 Start/Stop the LDAP server

$status = ReadService()

 Read out the state of the LDAP server runlevel script

=head1 DESCRIPTION

=over 2

=cut


package YaPI::LdapServer;

BEGIN {
    push @INC, '/usr/share/YaST2/modules/';
}

our $VERSION="1.0.0";

use strict;
use vars qw(@ISA);

use YaST::YCP;
use ycp;

use Locale::gettext;
use POSIX ();     # Needed for setlocale()

POSIX::setlocale(LC_MESSAGES, "");
textdomain("ldap-server");

use Digest::MD5 qw(md5_hex);
use Digest::SHA1 qw(sha1);
use MIME::Base64;
use X500::DN;

use YaPI;
@YaPI::LdapServer::ISA = qw( YaPI );

YaST::YCP::Import ("SCR");
YaST::YCP::Import ("Ldap");
YaST::YCP::Import ("Service");

our %TYPEINFO;
our @CAPABILITIES = (
                     'SLES9'
                    );

=item *
C<\@dbList = ReadDatabaseList()>

Returns a List of databases (suffix). 

EXAMPLE:

 use Data::Dumper;

 my $res = YaPI::LdapServer->ReadDatabaseList();
 if( not defined $res ) {
     # error    
 } else {
     print "OK: \n";
     print STDERR Data::Dumper->Dump([$res])."\n";
 }

=cut

BEGIN { $TYPEINFO{ReadDatabaseList} = ["function", ["list", "string"]]; }
sub ReadDatabaseList {
    my $self = shift;
    
    my $dbList = SCR->Read( ".ldapserver.databaselist" );
    if(! defined $dbList) {
        return $self->SetError(%{SCR->Error(".ldapserver")}); 
    }
    return $dbList;
}

=item *
C<$bool = AddDatabase(\%valueMap)>

Creates a new database section in the configuration file,
start or restart the LDAP Server and add the base object.
If the database exists, nothing is done and undef is returned. 

Supported keys in %valueMap are:
 
 * database: The database type (required)
 
 * suffix: The suffix (required)
 
 * rootdn: The Root DN (required)
 
 * passwd: The plain Root Password (required)

 * cryptmethod: The crypt method; allowed values are (CRYPT, SMD5, SHA, SSHA, PLAIN); default is 'SSHA'
 
 * directory: The Directory where the database files are(bdb/ldbm) (required)
 
 * cachesize: The cachesize(bdb/ldbm) (optional; default 10000)
 
 * checkpoint: The checkpoint(bdb) (optional; default 1024 5)

EXAMPLE:

 my $hash = {
             database    => 'bdb',
             suffix      => 'dc=example,dc=com',
             rootdn      => "cn=Admin,dc=example,dc=com",
             passwd      => "system",
             cryptmethod => 'SMD5',
             directory   => "/var/lib/ldap/db1",
            };

 my $res = YaPI::LdapServer->AddDatabase($hash);
 if( not defined $res ) {
     # error
 } else {
     print "OK: \n";
 }

=cut

BEGIN { $TYPEINFO{AddDatabase} = ["function", "boolean", ["map", "string", "any"]]; }
sub AddDatabase {
    my $self = shift;
    my $data = shift;

    my $passwd_string = undef;
    my $cryptMethod   = "SSHA";
    my $cachesize     = undef;
    my $checkpoint    = undef;
    my $hash = {};

    ################
    # check database
    ################
    if(!defined $data->{database} || $data->{database} eq "") {
                                          # error message at parameter check
        return $self->SetError(summary => "Missing parameter 'database'",
                               code => "PARAM_CHECK_FAILED");
    }
    if ( !grep( ($_ eq $data->{database}), ("bdb", "ldbm") ) ) {
        return $self->SetError(summary => sprintf(
                                   # error at paramter check
                                 _("Database type '%s' is not supported. Allowed are 'bdb' and 'ldbm'"),
                                                  $data->{database}),
                               code => "PARAM_CHECK_FAILED");
    }
    $hash->{database} = $data->{database};

    ################
    # check suffix
    ################
    if(!defined $data->{suffix} || $data->{suffix} eq "") {
        return $self->SetError(summary => "Missing parameter 'suffix'",
                               code => "PARAM_CHECK_FAILED");
    }

    my $object = X500::DN->ParseRFC2253($data->{suffix});
    my @attr = $object->getRDN($object->getRDNs()-1)->getAttributeTypes();
    my $val = $object->getRDN($object->getRDNs()-1)->getAttributeValue($attr[0]);
    
    if(!defined $attr[0] || !defined $val) {
        return $self->SetError(summary => "Can not parse 'suffix'",
                               description => "Parsing error for suffix '".$data->{suffix}."'",
                               code => "PARSE_ERROR");
    }
    my $entry = {};
    
    if( lc($attr[0]) eq "ou") {
        $entry = {
                  "objectClass" => [ "organizationalUnit" ],
                  "ou" => $val,
                 }
    } elsif( lc($attr[0]) eq "o") {
        $entry = {
                  "objectClass" => [ "organization" ],
                  "o" => $val,
                 }
    } elsif( lc($attr[0]) eq "c") {
        if($val !~ /^\w{2}$/) {
                                   # parameter check failed
            return $self->SetError(summary => _("The countryName must be a ISO-3166 country 2-letter code"),
                                   description => "Invalid value for 'c' ($val)",
                                   code => "PARAM_CHECK_FAILED");
        }
        $entry = {
                  "objectClass" => [ "country" ],
                  "c" => $val,
                 }
    } elsif( lc($attr[0]) eq "l") {
        $entry = {
                  "objectClass" => [ "locality" ],
                  "l" => $val,
                 }
    } elsif( lc($attr[0]) eq "st") {
        $entry = {
                  "objectClass" => [ "locality" ],
                  "st" => $val,
                 }
    } elsif( lc($attr[0]) eq "dc") {
        $entry = {
                  "objectClass" => [ "dcObject" ],
                  "dc" => $val,
                 }
    } else {
                               # parameter check failed
        return $self->SetError(summary => _("First part of suffix must be c=, st=, l=, o=, ou= or dc="),
                               code => "PARAM_CHECK_FAILED");
    }
    $hash->{suffix} = $data->{suffix};

    ##############
    # check rootdn
    ##############
    if(!defined $data->{rootdn} || $data->{rootdn} eq "") {
                               # parameter check failed
        return $self->SetError(summary => "Missing parameter 'rootdn'",
                               code => "PARAM_CHECK_FAILED");
    }

    if($data->{rootdn} !~ /$data->{suffix}$/) {
                               # parameter check failed
        return $self->SetError(summary => _("'rootdn' must be below the 'suffix'"),
                               code => "PARAM_CHECK_FAILED");
    }
    $hash->{rootdn} = $data->{rootdn};

    ##############################
    # check passwd and cryptmethod
    ##############################

    if(!defined $data->{passwd} || $data->{passwd} eq "") {
                               # parameter check failed
        return $self->SetError(summary => _("You must define 'passwd'"),
                               code => "PARAM_CHECK_FAILED");
    }
    if(!defined $data->{passwd} || $data->{passwd} eq "") {
                               # parameter check failed
        return $self->SetError(summary => _("You must define 'passwd'"),
                               code => "PARAM_CHECK_FAILED");
    }

    if(defined $data->{cryptmethod} && $data->{cryptmethod} ne "") {
        $cryptMethod = $data->{cryptmethod};
    }
    if( !grep( ($_ eq $cryptMethod), ("CRYPT", "SMD5", "SHA", "SSHA", "PLAIN") ) ) {
        return $self->SetError(summary => sprintf(
                               # parameter check failed
                                                  _("'%s' is an unsupported crypt method."),
                                                  $cryptMethod),
                               code => "PARAM_CHECK_FAILED");
    }

    if( $cryptMethod eq "CRYPT" ) {
        my $salt =  pack("C2",(int(rand 26)+65),(int(rand 26)+65));
        $passwd_string = crypt $data->{passwd},$salt;
        $passwd_string = "{crypt}".$passwd_string;
    } elsif( $cryptMethod eq "SMD5" ) {
        my $salt =  pack("C5",(int(rand 26)+65),(int(rand 26)+65),(int(rand 26)+65),
                         (int(rand 26)+65), (int(rand 26)+65));
        my $ctx = new Digest::MD5();
        $ctx->add($data->{passwd});
        $ctx->add($salt);
        $passwd_string = "{smd5}".encode_base64($ctx->digest.$salt, "");
    } elsif( $cryptMethod eq "SHA"){
        my $digest = sha1($data->{passwd});
        $passwd_string = "{sha}".encode_base64($digest, "");
    } elsif( $cryptMethod eq "SSHA"){
        my $salt =  pack("C5",(int(rand 26)+65),(int(rand 26)+65),(int(rand 26)+65),
                         (int(rand 26)+65), (int(rand 26)+65));
        my $digest = sha1($data->{passwd}.$salt);
        $passwd_string = "{ssha}".encode_base64($digest.$salt, "");
    } else {
        $passwd_string = $data->{passwd};
    }
    $hash->{rootpw} = $passwd_string;
    
    #################
    # check directory
    #################
    
    if(!defined $data->{directory} || $data->{directory} eq "") {
                               # parameter check failed
        return $self->SetError(summary => _("You must define 'directory'"),
                               code => "PARAM_CHECK_FAILED");
    }
    if( ! defined  SCR->Read(".target.dir", $data->{directory})) {
                               # parameter check failed
        return $self->SetError(summary => _("The directory does not exist."),
                               description => "The 'directory' (".$data->{directory}.") does not exist.",
                               code => "DIR_DOES_NOT_EXIST");
    }
    $hash->{directory} = $data->{directory};

    ##################
    # check cachesize
    ##################
    if(defined $data->{cachesize} && $data->{cachesize} ne "") {

        if($data->{cachesize} !~ /^\d+$/) {
            return $self->SetError(summary => _("Invalid cachesize value."),
                                   description => "cachesize = '".$data->{cachesize}."'. Must be a integer value",
                                   code => "PARAM_CHECK_FAILED");
        }
        $cachesize = $data->{cachesize};
    }
    if(! exists $data->{cachesize}) {
        # set default if parameter does not exist
        $cachesize = 10000;
    }
    $hash->{cachesize} = $cachesize;
    
    if($data->{database} eq "bdb") {
        ##################
        # check checkpoint
        ##################
        if(defined $data->{checkpoint} && $data->{checkpoint} ne "") {
            my @cp = split(/\s+/, $data->{checkpoint});
            if(!defined $cp[0] || !defined $cp[1] ||
               $cp[0] !~ /^\d+$/ || $cp[1] !~ /^\d+$/) {
                return $self->SetError(summary => _("Invalid checkpoint value."),
                                       description => "checkpoint = '".$data->{checkpoint}."'.\n Must be two integer values seperated by space.",
                                       code => "PARAM_CHECK_FAILED");
            }
            $checkpoint = $cp[0]." ".$cp[1];
        }
        if(! exists $data->{checkpoint}) {
            # set default if parameter does not exist
            $checkpoint = "1024 5";
        }
        $hash->{checkpoint} = $checkpoint;
    }

    if(! SCR->Write(".ldapserver.database", $data->{suffix}, $hash)) {
        return $self->SetError(%{SCR->Error(".ldapserver")});
    }

    if(! $self->SwitchService(1)) {
        return undef;
    }
    
    if(! SCR->Execute(".ldap", {"hostname" => 'localhost',
                                "port"     => 389})) {
        return $self->SetError(summary => "LDAP init failed",
                               code => "SCR_INIT_FAILED");
    }

    if (! SCR->Execute(".ldap.bind", {"bind_dn" => $data->{'rootdn'},
                                      "bind_pw" => $data->{'passwd'}}) ) {
        my $ldapERR = SCR->Read(".ldap.error");
        return $self->SetError(summary => "LDAP bind failed",
                               code => "SCR_INIT_FAILED",
                               description => $ldapERR->{'code'}." : ".$ldapERR->{'msg'});
    }
    
    if (! SCR->Write(".ldap.add", { $data->{'suffix'} } , $entry)) {
        my $ldapERR = SCR->Read(".ldap.error");
        return $self->SetError(summary => "Can not add base entry.",
                               code => "LDAP_ADD_FAILED",
                               description => $ldapERR->{'code'}." : ".$ldapERR->{'msg'});
    }
    
    return 1;
}

=item *
C<$bool = EditDatabase($suffix,\%valueMap)>

Edit the database section with the suffix B<$suffix> in the configuration file.
Only save parameter are supported. 
Supported keys in %valueMap are:
 
 * rootdn: The Root DN
 
 * passwd: The Root Password
 
 * cryptmethod: The crypt method; allowed values are (CRYPT, SMD5, SHA, SSHA, PLAIN); default is 'SSHA'

 * cachesize: The cachesize(bdb/ldbm)
 
 * checkpoint: The checkpoint(bdb)

If the key is defined, but the value is 'undef' the option will be deleted.
If a key is not defined, the option is not changed.
If the key is defined and a value is specified, this value will be set.

rootdn, passwd and cryptmethod can not be deleted.

EXAMPLE:

 my $hash = { suffix      => "dc=example,dc=com",
              rootdn      => "cn=Administrator,dc=example,dc=com",
              rootpw      => "example",
              cryptmethod => "CRYPT"
            };

 my $res = YaPI::LdapServer->EditDatabase($hash);
 if( not defined $res ) {
     # error
 } else {
     print "OK: \n";
 }

=cut

BEGIN { $TYPEINFO{EditDatabase} = ["function", "boolean", "string", ["map", "string", "any"]]; }
sub EditDatabase {
    my $self   = shift;
    my $suffix = shift;
    my $data   = shift;
    my $cryptMethod = undef;
    my $passwd_string = undef;
    
    if(!defined $suffix || $suffix eq "") {
        return $self->SetError(summary => "Missing parameter 'suffix'",
                               code => "PARAM_CHECK_FAILED");
    }

    ###################
    # work on rootdn
    ###################
    if(exists $data->{rootdn} && ! defined $data->{rootdn}) {
                               # parameter check failed
        return $self->SetError(summary => _("'rootdn' is required. You can not delete it"),
                               code => "PARAM_CHECK_FAILED");
    } elsif(exists $data->{rootdn}) {
        if($data->{rootdn} !~ /$suffix$/) {
            # parameter check failed
            return $self->SetError(summary => _("'rootdn' must be below the 'suffix'"),
                                   code => "PARAM_CHECK_FAILED");
        } else {
            # set new rootdn
            if(! SCR->Write(".ldapserver.database", $suffix, { rootdn => $data->{rootdn} })) {
                my $err = SCR->Error(".ldapserver");
                $err->{description} = $err->{summary}."\n\n".$err->{description};
                $err->{summary} = _("Editing 'rootdn' failed.");
                return $self->SetError(%{$err});
            }
        }
    }

    ###################
    # work on passwd
    ###################
    if(exists $data->{passwd} && ! defined $data->{passwd}) {
                                           # parameter check failed
        return $self->SetError(summary => _("'passwd' is required. You can not delete it"),
                               code => "PARAM_CHECK_FAILED");
    } elsif(exists $data->{passwd}) {

        if(!defined $data->{passwd} || $data->{passwd} eq "") {
                                               # parameter check failed
            return $self->SetError(summary => _("You must define 'passwd'"),
                                   code => "PARAM_CHECK_FAILED");
        }
        if(!defined $data->{passwd} || $data->{passwd} eq "") {
                                   # parameter check failed
            return $self->SetError(summary => _("You must define 'passwd'"),
                                   code => "PARAM_CHECK_FAILED");
        }

        if(defined $data->{cryptmethod} && $data->{cryptmethod} ne "") {
            $cryptMethod = $data->{cryptmethod};
        }
        if( !grep( ($_ eq $cryptMethod), ("CRYPT", "SMD5", "SHA", "SSHA", "PLAIN") ) ) {
            return $self->SetError(summary => sprintf(
                                                      # parameter check failed
                                                      _("'%s' is an unsupported crypt method."),
                                                      $cryptMethod),
                                   code => "PARAM_CHECK_FAILED");
        }

        if( $cryptMethod eq "CRYPT" ) {
            my $salt =  pack("C2",(int(rand 26)+65),(int(rand 26)+65));
            $passwd_string = crypt $data->{passwd},$salt;
            $passwd_string = "{crypt}".$passwd_string;
        } elsif( $cryptMethod eq "SMD5" ) {
            my $salt =  pack("C5",(int(rand 26)+65),(int(rand 26)+65),(int(rand 26)+65),
                             (int(rand 26)+65), (int(rand 26)+65));
            my $ctx = new Digest::MD5();
            $ctx->add($data->{passwd});
            $ctx->add($salt);
            $passwd_string = "{smd5}".encode_base64($ctx->digest.$salt, "");
        } elsif( $cryptMethod eq "SHA"){
            my $digest = sha1($data->{passwd});
            $passwd_string = "{sha}".encode_base64($digest, "");
        } elsif( $cryptMethod eq "SSHA"){
            my $salt =  pack("C5",(int(rand 26)+65),(int(rand 26)+65),(int(rand 26)+65),
                             (int(rand 26)+65), (int(rand 26)+65));
            my $digest = sha1($data->{passwd}.$salt);
            $passwd_string = "{ssha}".encode_base64($digest.$salt, "");
        } else {
            $passwd_string = $data->{passwd};
        }
        # set new rootpw
        if(! SCR->Write(".ldapserver.database", $suffix, { rootpw => $passwd_string })) {
            my $err = SCR->Error(".ldapserver");
            $err->{description} = $err->{summary}."\n\n".$err->{description};
            $err->{summary} = _("Editing 'password' failed.");
            return $self->SetError(%{$err});
        }
    }

    ###################
    # work on cachesize
    ###################
    if(exists $data->{cachesize} && !defined $data->{cachesize}) {
        # Delete cachesize option
        if(! SCR->Write(".ldapserver.database", $suffix, { cachesize => undef })) {
            my $err = SCR->Error(".ldapserver");
            $err->{description} = $err->{summary}."\n\n".$err->{description};
            $err->{summary} = _("Editing 'cachesize' failed.");
            return $self->SetError(%{$err});
        }
    } elsif(exists $data->{cachesize}) {

        if(defined $data->{cachesize} && $data->{cachesize} ne "") {

            if($data->{cachesize} !~ /^\d+$/) {
                return $self->SetError(summary => _("Invalid cachesize value."),
                                       description => "cachesize = '".$data->{cachesize}."'. Must be a integer value",
                                       code => "PARAM_CHECK_FAILED");
            }
            #$cachesize = $data->{cachesize};
            # set new cachesize
            if(! SCR->Write(".ldapserver.database", $suffix, { cachesize => $data->{cachesize} })) {
                my $err = SCR->Error(".ldapserver");
                $err->{description} = $err->{summary}."\n\n".$err->{description};
                $err->{summary} = _("Editing 'cachesize' failed.");
                return $self->SetError(%{$err});
            }
        } else {
            return $self->SetError(summary => _("Invalid cachesize value."),
                                   description => "cachesize = '".$data->{cachesize}."'. Must be a integer value",
                                   code => "PARAM_CHECK_FAILED");
        }
    }

    ####################
    # work on checkpoint
    ####################
    if(exists $data->{checkpoint}) {

        if(!defined $data->{checkpoint}) {
            # Delete checkpoint option
            if(! SCR->Write(".ldapserver.database", $suffix, { checkpoint => undef })) {
                my $err = SCR->Error(".ldapserver");
                $err->{description} = $err->{summary}."\n\n".$err->{description};
                $err->{summary} = _("Editing 'checkpoint' failed.");
                return $self->SetError(%{$err});
            }
        } else {

            my $db = $self->ReadDatabase($suffix);
            return undef if(! defined $db);
            
            if($db->{database} eq "bdb") {

                if($data->{checkpoint} ne "") {
                    my @cp = split(/\s+/, $data->{checkpoint});
                    if(!defined $cp[0] || !defined $cp[1] ||
                       $cp[0] !~ /^\d+$/ || $cp[1] !~ /^\d+$/) {
                        return $self->SetError(summary => _("Invalid checkpoint value."),
                                               description => "checkpoint = '".$data->{checkpoint}."'.\n Must be two integer values seperated by space.",
                                               code => "PARAM_CHECK_FAILED");
                    }
                    my $checkpoint = $cp[0]." ".$cp[1];
                    # set new checkpoint
                    if(! SCR->Write(".ldapserver.database", $suffix, { checkpoint => $checkpoint })) {
                        my $err = SCR->Error(".ldapserver");
                        $err->{description} = $err->{summary}."\n\n".$err->{description};
                        $err->{summary} = _("Editing 'checkpoint' failed.");
                        return $self->SetError(%{$err});
                    }
                } else {
                    return $self->SetError(summary => _("Invalid checkpoint value."),
                                           description => "checkpoint = '".$data->{checkpoint}."'.\n Must be two integer values seperated by space.",
                                           code => "PARAM_CHECK_FAILED");
                }
            }
        }
    }
    return 1;
}

=item *
C<\%valueMap = ReadDatabase($suffix)>

Read the database section with the suffix B<$suffix>. 

Returned keys in %valueMap are:
 
 * database: The database type
 
 * suffix: The suffix
 
 * rootdn: The Root DN
 
 * passwd: The Root Password
 
 * directory: The Directory where the database files are(bdb/ldbm)
 
 * cachesize: The cachesize(bdb/ldbm)
 
 * checkpoint: The checkpoint(bdb)
 
There can be some more, if they are in this databse section.

EXAMPLE:

 use Data::Dumper;

 my $res = YaPI::LdapServer->ReadDatabase('"dc=example,dc=com"');
 if( not defined $res ) {
     # error
 } else {
     print "OK: \n";
     print STDERR Data::Dumper->Dump([$res])."\n";
 }

=cut

BEGIN { $TYPEINFO{ReadDatabase} = ["function", ["map", "string", "any"], "string"]; }
sub ReadDatabase {
    my $self = shift;
    my $suffix = shift;

    if(! defined $suffix || $suffix eq "") {
                                          # error message at parameter check
        return $self->SetError(summary => _("Missing Parameter 'suffix'."),
                               code => "PARAM_CHECK_FAILED");
    }
    my $dbHash = SCR->Read( ".ldapserver.database", $suffix );
    if(! defined $dbHash) {
        return $self->SetError(%{SCR->Error(".ldapserver")});
    }
    if(exists $dbHash->{index}) {
        # we have a special function to maintain 'index'
        delete $dbHash->{index};
    }
    return $dbHash;
}

=item *
C<\@indexList = ReadIndex($suffix)>

Returns a List of Maps with all index statements for this database. The "keys" are:

 * 'attr', an attribute or an attribute list

 * 'param', a number of special index parameters 

 * 'md5', a MD5 sum of this index. This numer is needed for EditIndex and DeleteIndex

EXAMPLE:

 use Data::Dumper;

 my $res = YaPI::LdapServer->ReadIndex('"dc=example,dc=com"');
 if( not defined $res ) {
     # error
 } else {
     print "OK: \n";
     print STDERR Data::Dumper->Dump([$res])."\n";
 }

=cut

BEGIN { $TYPEINFO{ReadIndex} = ["function", ["list", ["map", "string", "string"] ], "string"]; }
sub ReadIndex {
    my $self = shift;
    my $suffix = shift;
    my @idxList = ();

    if(! defined $suffix || $suffix eq "") {
                                          # error message at parameter check
        return $self->SetError(summary => _("Missing Parameter 'suffix'."),
                               code => "PARAM_CHECK_FAILED");
    }
    my $dbHash = SCR->Read( ".ldapserver.database", $suffix );
    if(! defined $dbHash) {
        return $self->SetError(%{SCR->Error(".ldapserver")});
    }
    if(exists $dbHash->{index} && defined $dbHash->{index} &&
       ref $dbHash->{index} eq "ARRAY") {
        
        foreach my $idx (@{$dbHash->{index}}) {
            my $idxHash = {};
            
            my ($attr, $method, $empty) = split(/\s+/, $idx);
            if(!(! defined $empty || $empty eq "")) {
                return $self->SetError(summary => "Index parsing error.",
                                       code => "PARSING_ERROR");
            }
            
            $idxHash->{attr} = $attr;
            $idxHash->{param} = $method;
            
            my $md5 = md5_hex( $attr." ".$method );
            $idxHash->{md5} = $md5;
            push @idxList, $idxHash;
        }
        
    }
    return \@idxList;
}

=item *
C<$bool = AddIndex($suffix,\%indexMap)>

Add a new index statement B<%indexMap> to the database section B<$suffix>.

The indexMap has two keys

 * 'attr', an attribute or an attribute list

 * 'param', a number of special index parameters 

EXAMPLE:

 my $newIndex = {
                 'attr'  => "uid,cn",
                 'param' => "eq"
                };

 my $res = YaPI::LdapServer->AddIndex("dc=example,dc=com", $newIndex);
 if( not defined $res ) {
     # error
 } else {
     print "OK: \n";
 }

=cut

BEGIN { $TYPEINFO{AddIndex} = ["function", "boolean", "string", [ "map", "string", "string"] ]; }
sub AddIndex {
    my $self = shift;
    my $suffix = shift;
    my $indexHash = shift;
    my $orig_idxArray = undef;
    my @new_idx = ();

    if(!defined $suffix || $suffix eq "") {
        return $self->SetError(summary => "Missing parameter 'suffix'",
                               code => "PARAM_CHECK_FAILED");
    }

    if(!defined $indexHash || !defined $indexHash->{attr} || 
       !defined $indexHash->{param} ) {
        return $self->SetError(summary => "Missing parameter 'index'",
                               code => "PARAM_CHECK_FAILED");
    }
    
    $orig_idxArray = ReadIndex($suffix);
    if(! defined $orig_idxArray) {
        return $self->SetError(%{SCR->Error(".ldapserver")});
    }
    
    foreach my $idx (@{$orig_idxArray}) {
        push @new_idx, $orig_idxArray->{$idx}->{attr}." ".$orig_idxArray->{$idx}->{param};
    }
    push @new_idx, $indexHash->{attr}." ".$indexHash->{param};
    
    if(! SCR->Write(".ldapserver.database", $suffix, { index => \@new_idx })) {
        return $self->SetError(%{SCR->Error(".ldapserver")});
    }
    return 1;
}

=item *
C<$bool = EditIndex($suffix,$index_md5,\%indexMap)>

Replace the index B<$index_md5> in the database section B<$suffix> by the new index
statement B<%indexMap>.

The indexMap has two keys

 * 'attr', an attribute or an attribute list

 * 'param', a number of special index parameters 

EXAMPLE:

 my $newIndex = {
                 'attr'  => "uid,cn",
                 'param' => "eq"
                };

 my $res = YaPI::LdapServer->EditIndex("dc=example,dc=com", "eacc11456b6c2ae4e1aef0fa287e02b0",
                                       $newIndex);
 if( not defined $res ) {
     # error
 } else {
        print "OK: \n";
 }

=cut

BEGIN { $TYPEINFO{EditIndex} = ["function", "boolean", "string", "string", 
                                [ "map", "string", "string"]]; }
sub EditIndex {
    my $self = shift;
    my $suffix = shift;
    my $idx_md5 = shift;
    my $indexHash = shift;
    my $orig_idxArray = undef;
    my @new_idx = ();
    my $found = 0;

    if(!defined $suffix) {
        return $self->SetError(summary => "Missing parameter 'suffix'",
                               code => "PARAM_CHECK_FAILED");
    }

    if(!defined $idx_md5 || $idx_md5 !~ /^[[:xdigit:]]+$/ ) {
        return $self->SetError(summary => "Missing parameter 'idx_md5'",
                               code => "PARAM_CHECK_FAILED");
    }

    if(!defined $indexHash || !defined $indexHash->{attr} || 
       !defined $indexHash->{param} ) {
        return $self->SetError(summary => "Missing parameter 'index'",
                               code => "PARAM_CHECK_FAILED");
    }
    
    $orig_idxArray = ReadIndex($suffix);
    if(! defined $orig_idxArray) {
        return $self->SetError(%{SCR->Error(".ldapserver")});
    }
    
    foreach my $idx (@{$orig_idxArray}) {
        if($idx->{md5} eq $idx_md5) {
            push @new_idx, $indexHash->{attr}." ".$indexHash->{param};
            $found = 1;
        } else {
            push @new_idx, $idx->{attr}." ".$idx->{param};
        }
    }
    if(!$found) {
        return $self->SetError(summary => "No such 'index'.",
                               description => "MD5 '$idx_md5' not found in this database",
                               code => "PARAM_CHECK_FAILED");
    }
    
    if(! SCR->Write(".ldapserver.database", $suffix, { index => \@new_idx })) {
        return $self->SetError(%{SCR->Error(".ldapserver")});
    }
    return 1;
}

=item *
C<$bool = DeleteIndex($suffix,$index_md5)>

Delete the index B<$index_md5> statement in the database section B<$suffix>. 

EXAMPLE:

 my $res = YaPI::LdapServer->DeleteIndex("dc=example,dc=com", "338a980b4eebe87365a4077067ce1559");
 if( not defined $res ) {
     # error
 } else {
     print "OK: \n";
 }

=cut

BEGIN { $TYPEINFO{DeleteIndex} = ["function", "boolean", "string", "string" ]; }
sub DeleteIndex {
    my $self = shift;
    my $suffix = shift;
    my $idx_md5 = shift;
    my $orig_idxArray = undef;
    my @new_idx = ();
    my $found = 0;

    if(!defined $suffix) {
        return $self->SetError(summary => "Missing parameter 'suffix'",
                               code => "PARAM_CHECK_FAILED");
    }

    if(!defined $idx_md5 || $idx_md5 !~ /^[[:xdigit:]]+$/ ) {
        return $self->SetError(summary => "Missing parameter 'idx_md5'",
                               code => "PARAM_CHECK_FAILED");
    }
    
    $orig_idxArray = ReadIndex($suffix);
    if(! defined $orig_idxArray) {
        return $self->SetError(%{SCR->Error(".ldapserver")});
    }
    
    foreach my $idx (@{$orig_idxArray}) {
        if($idx->{md5} eq $idx_md5) {
            $found = 1;
        } else {
            push @new_idx, $idx->{attr}." ".$idx->{param};
        }
    }
    if(!$found) {
        return $self->SetError(summary => "No such 'index'.",
                               description => "MD5 '$idx_md5' not found in this database",
                               code => "PARAM_CHECK_FAILED");
    }
    
    if(! SCR->Write(".ldapserver.database", $suffix, { index => \@new_idx })) {
        return $self->SetError(%{SCR->Error(".ldapserver")});
    }
    return 1;
}

=item *
C<$bool = RecreateIndex($suffix)>

Regenerate indices based upon the current contents of a 
database determined by $suffix. This function stops the 
ldapserver, call slapindex and start the ldapserver again.

EXAMPLE:

 my $res = YaPI::LdapServer->RecreateIndex("dc=example,dc=com");
 if( not defined $res ) {
     # error
 } else {
     print "OK: \n";
 }

=cut

BEGIN { $TYPEINFO{RecreateIndex} = ["function", "boolean", "string" ]; }
sub RecreateIndex {
    my $self = shift;
    my $suffix = shift;
    my $err = 0;

    if(!defined $suffix || $suffix eq "") {
        return $self->SetError(summary => "Missing parameter 'suffix'",
                               code => "PARAM_CHECK_FAILED");
    }

    if(! $self->SwitchService(0)) {
        return undef;
    }

    if(0 != SCR->Execute(".target.bash", "slapindex -b $suffix") ) {
        $err = 1;
    }

    if(! $self->SwitchService(1)) {
        return undef;
    }
    
    if($err) {
        return $self->SetError(summary => "Can not create new index",
                               code => "SCR_EXECUTE_FAILED");
    }
    return 1;
}

=item *
C<\@list = ReadSchemaIncludeList()>

Returns a list of all included schema files in the order they appear in the config files.

EXAMPLE:

 use Data::Dumper;

 my $res = YaPI::LdapServer->ReadSchemaIncludeList();
 if( not defined $res ) {
     # error
 } else {
     print "OK: \n";
     print STDERR Data::Dumper->Dump([$res])."\n";
 }

=cut

BEGIN { $TYPEINFO{ReadSchemaIncludeList} = ["function", ["list", "string"] ]; }
sub ReadSchemaIncludeList {
    my $self = shift;

    my $global = SCR->Read( ".ldapserver.global" );
    if(! defined $global) {
        return $self->SetError(%{SCR->Error(".ldapserver")});
    }
    if(exists $global->{schemainclude} && defined $global->{schemainclude} &&
       ref $global->{schemainclude} eq "ARRAY") {
        return $global->{schemainclude};
    }
    return ();
}

=item *
C<$bool = WriteSchemaIncludeList(\@list)>

Writes all schema includes preserving order.

EXAMPLE:

 my $schemas = [
                '/etc/openldap/schema/core.schema',
                '/etc/openldap/schema/cosine.schema',
                '/etc/openldap/schema/inetorgperson.schema',
                '/etc/openldap/schema/rfc2307bis.schema',
                '/etc/openldap/schema/yast2userconfig.schema',
                '/etc/openldap/schema/samba3.schema'
               ];

 my $res = YaPI::LdapServer->WriteSchemaIncludeList($schemas);
 if( not defined $res ) {
     # error
 } else {
     print "OK: \n";
 }

=cut

BEGIN { $TYPEINFO{WriteSchemaIncludeList} = ["function", "boolean", ["list", "string"] ]; }
sub WriteSchemaIncludeList {
    my $self = shift;
    my $incList = shift;

    if(!defined $incList || ref($incList) ne "ARRAY" ) {
        return $self->SetError(summary => "Include list is missing",
                               code => "PARAM_CHECK_FAILED");
    }

    foreach my $path (@{$incList}) {
        if(! -e $path) {
            return $self->SetError(summary => "File does not exist.",
                                   description => "'$path' does not exist",
                                   code => "PARAM_CHECK_FAILED");
        }
    }
    
    if(! SCR->Write(".ldapserver.global", { schemainclude => $incList })) {
        return $self->SetError(%{SCR->Error(".ldapserver")});
    }
    return 1;
}

=item *
C<\@list = ReadAllowList()>

Returns a list of allow statements. 

EXAMPLE:

 use Data::Dumper;

 my $res = YaPI::LdapServer->ReadAllowList();
 if( not defined $res ) {
     # error
 } else {
     print "OK: \n";
     print STDERR Data::Dumper->Dump([$res])."\n";
 }

=cut

BEGIN { $TYPEINFO{ReadAllowList} = ["function", ["list", "string"] ]; }
sub ReadAllowList {
    my $self = shift;
    my @allowList = ();

    my $global = SCR->Read( ".ldapserver.global" );
    if(! defined $global) {
        return $self->SetError(%{SCR->Error(".ldapserver")});
    }
    if(exists $global->{allow} && defined $global->{allow} &&
       ref $global->{allow} eq "ARRAY") {
        
        foreach my $a (@{$global->{allow}}) {
            next if( $a eq "");
            my @al = split(/\s+/, $a);
            foreach my $value ( @al ) {
                $value =~ s/\s+/ /sg;
                $value =~ s/\s+$//;
                next if( $value eq "");
                push @allowList, $value;
            }
        }
    }
    return \@allowList;
}

=item *
C<$bool = WriteAllowList(\@list)>

Replaces the complete allow option with the specified feature list.

EXAMPLE:

 my @list = ( "bind_v2" );

 $res = YaPI::LdapServer->WriteAllowList( \@list );
 if( not defined $res ) {
     # error
 } else {
     print "OK: \n";
 }

=cut

BEGIN { $TYPEINFO{WriteAllowList} = ["function", "boolean", [ "list", "string"] ]; }
sub WriteAllowList {
    my $self = shift;
    my $allowList = shift;

    if(!defined $allowList || ref($allowList) ne "ARRAY") {
        return $self->SetError(summary => "'Allow list' is missing",
                               code => "PARAM_CHECK_FAILED");
    }
    
    if(scalar @$allowList == 0) {
        if(!SCR->Write(".ldapserver.global", { allow => undef })) {
            return $self->SetError(%{SCR->Error(".ldapserver")});
        }
    } else {
        if(!SCR->Write(".ldapserver.global", { allow => $allowList })) {
            return $self->SetError(%{SCR->Error(".ldapserver")});
        }
    }
    return 1;
}

=item *
C<$loglevel = ReadLoglevel()>

Read the loglevel bitmask.

EXAMPLE:

 my $res = YaPI::LdapServer->ReadLoglevel();
 if( not defined $res ) {

 } else {
     print "OK: \n";
     print STDERR Data::Dumper->Dump([$res])."\n";
 }

=cut

BEGIN { $TYPEINFO{ReadLoglevel} = ["function", "integer" ]; }
sub ReadLoglevel {
    my $self = shift;
    my $loglevel = 0;

    my $global = SCR->Read( ".ldapserver.global" );
    if(! defined $global) {
        return $self->SetError(%{SCR->Error(".ldapserver")});
    }
    if(exists $global->{loglevel} && defined $global->{loglevel} && $global->{loglevel} ne "" ) {
        
        $loglevel = $global->{loglevel};
        
    }
    return $loglevel;
}

=item *
C<$bool = AddLoglevel($bit)>

Set the given loglevel bit to 1 in the current bitmask.

EXAMPLE:

 my $res = YaPI::LdapServer->AddLoglevel( 0x04 );
 if( not defined $res ) {
     # error
 } else {
     print "OK: \n";
 }

=cut

BEGIN { $TYPEINFO{AddLoglevel} = ["function", "boolean", "integer" ]; }
sub AddLoglevel {
    my $self = shift;
    my $bit  = shift;
 
    if(!defined $bit || $bit !~ /^\d+$/) {
        return $self->SetError(summary => "Wrong parameter 'bit'",
                               code => "PARAM_CHECK_FAILED");
    }
    
    my $loglevel = $self->ReadLoglevel();
    return undef if(!defined $loglevel);
    
    $loglevel = $loglevel | $bit;
    
    my $ret = $self->WriteLoglevel($loglevel);
    return undef if(!defined $loglevel);
    
    return 1;
}

=item *
C<$bool = DeleteLoglevel($bit)>

Set the given loglevel bit to 0 in the current bitmask.

EXAMPLE:

 my $res = YaPI::LdapServer->DeleteLoglevel( 0x04 );
 if( not defined $res ) {

 } else {
     print "OK: \n";
 }

=cut

BEGIN { $TYPEINFO{DeleteLoglevel} = ["function", "boolean", "integer" ]; }
sub DeleteLoglevel {
    my $self = shift;
    my $bit  = shift;
 
    if(!defined $bit || $bit !~ /^\d+$/) {
        return $self->SetError(summary => "Wrong parameter 'bit'",
                               code => "PARAM_CHECK_FAILED");
    }
    
    my $loglevel = $self->ReadLoglevel();
    return undef if(!defined $loglevel);
    
    $loglevel = ( $loglevel & (~ $bit) );
    
    my $ret = $self->WriteLoglevel($loglevel);
    return undef if(!defined $loglevel);
    
    return 1;
}

=item *
C<$bool = WriteLoglevel($loglevel)>

Replaces the loglevel bitmask. 

EXAMPLE:

 my $res = YaPI::LdapServer->WriteLoglevel( 0x06 );
 if( not defined $res ) {

 } else {
     print "OK: \n";
 }

=cut

BEGIN { $TYPEINFO{WriteLoglevel} = ["function", "boolean", "integer" ]; }
sub WriteLoglevel {
    my $self = shift;
    my $loglevel = shift;

    if(!defined $loglevel || $loglevel !~ /^\d+$/) {
        return $self->SetError(summary => "Wrong parameter 'loglevel'",
                               code => "PARAM_CHECK_FAILED");
    }

    if(! SCR->Write(".ldapserver.global", { loglevel => $loglevel } )) {
        return $self->SetError(%{SCR->Error(".ldapserver")});
    }
    return 1;
}

=item *
C<ModifyService($status)>

with this function you can turn on and off the LDAP server
runlevel script.
Turning off means, no LDAP server start at boot time.

EXAMPLE

 ModifyService(0); # turn LDAP server off at boot time
 ModifyService(1); # turn LDAP server on at boot time

=cut

BEGIN { $TYPEINFO{ModifyService} = ["function", "boolean", "boolean" ]; }
sub ModifyService {
    my $self = shift;
    my $enable = shift;

    if( $enable ) {
        Service->Adjust( "ldap", "enable" );
    } else {
        Service->Adjust( "ldap", "disable" );
    }
    return 1;
}

=item *
C<SwitchService($status)>

with this function you can start and stop the LDAP server
service.

EXAMPLE

 SwitchService( 0 ); # turning off the LDAP server service
 SwitchService( 1 ); # turning on the LDAP server service

=cut

sub SwitchService {
    my $self = shift;
    my $enable = shift;
    my $ret = undef;

    if( $enable ) {
        $ret = Service->RunInitScript( "ldap", "restart");
        if(! defined $ret || $ret != 0) {
            return $self->SetError(summary => _("Can not restart the service"),
                                   description => "LDAP restart failed ($ret)",
                                   code => "SERVICE_RESTART_FAILED");
        }
    } else {
        $ret = Service->RunInitScript( "ldap", "stop" );
        if(! defined $ret || $ret != 0) {
            return $self->SetError(summary => _("Can not stop the service"),
                                   description => "LDAP stop failed ($ret)",
                                   code => "SERVICE_STOP_FAILED");
        }
    }
    return 1;
}

=item *
C<$status = ReadService()>

with this function you can read out the state of the
LDAP server runlevel script (starting LDAP server at boot time).

EXAMPLE

 print "LDAP is ".( (ReadService())?('on'):('off') )."\n";

=cut
BEGIN { $TYPEINFO{ReadService} = ["function", "boolean"]; }
sub ReadService {
    my $self = shift;
    return Service->Enabled('ldap');
}
