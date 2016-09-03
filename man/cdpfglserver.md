% cdpfglserver(1) cdpfglserver user manual | version 0.0.9

# NAME

**cdpfglserver** - server where your files are being saved.

# SYNOPSIS

**cdpfglserver** [options]

# DESCRIPTION

cdpfglserver will wait for cdpfglclient to send files to save and cdpfglrestore to send request to restore those files.

# COMMAND-LINE OPTIONS

-h, --help Gives some basic help about program invocation and its options.  
-d, --debug=0|1 When invoked with 0 debug mode is turned off. Debug mode is turned on when invoked with 1. When on this mode is really verbose and may slow down the program. You should not use this option in daily normal use.  
-v, --version Gives compiled version of cdpfglserver plus some informations about libraries that were compiled with it and also some configuration informations as the program has loaded them.  
-c, --configuration=FILENAME Specify an alternative configuration file. This file is read instead of the default one.  


# SEE ALSO

**cdpfglrestore**(1), **cdpfglclient**(1)

# FILES

/etc/cdpfgl/server.conf

# BUGS

Please report bugs by either filling a pull request at (http://example.com/ [https://github.com/dupgit/sauvegarde/pulls](https://github.com/dupgit/sauvegarde/pulls "Github") or by sending a mail to Olivier Delhomme <olivier.delhomme@free.fr>

# AUTHORS

Authors in alphabetical order are :

* Pierre Bourgin <pierre.bourgin@free.fr>  
* Olivier Delhomme <olivier.delhomme@free.fr>  
* Sebastien Tricaud <sebastien@honeynet.org>  