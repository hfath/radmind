/*
 * Copyright (c) 2003 Regents of The University of Michigan.
 * All Rights Reserved.  See COPYRIGHT.
 */

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/evp.h>

#include "applefile.h"
#include "base64.h"
#include "argcargv.h"
#include "cksum.h"
#include "code.h"
#include "pathcmp.h"
#include "largefile.h"

void            (*logger)( char * ) = NULL;

int		linenum = 0;
int		cksum = 0;
int		verbose = 1;
const EVP_MD	*md;
extern char	*version, *checksumlist;
char            prepath[ MAXPATHLEN ] = {0};

/*
 * exit codes:
 *	0 	No changes found, everything okay
 *	1	Changes necessary / changes made
 *	2	System error
 */

    int
main( int argc, char **argv )
{
    int			ufd, c, err = 0, updatetran = 1, updateline = 0;
    int			ucount = 0, len, tac, amode = R_OK | W_OK, lcount = 0;
    int			prefixfound = 0;
    int			remove = 0;
    int			lastpct = -1;
    float		pct = 0.0;
    extern int          optind;
    char		*transcript = NULL, *tpath = NULL, *line;
    char		*prefix = NULL;
    char                **targv;
    char                tline[ 2 * MAXPATHLEN ];
    char		path[ 2 * MAXPATHLEN ];
    char		upath[ 2 * MAXPATHLEN ];
    char		lcksum[ SZ_BASE64_E( EVP_MAX_MD_SIZE ) ];
    FILE		*f, *ufs = NULL;
    struct stat		st;
    off_t		cksumsize;

    while ( ( c = getopt ( argc, argv, "c:P:nqVv" ) ) != EOF ) {
	switch( c ) {
	case 'c':
	    OpenSSL_add_all_digests();
	    md = EVP_get_digestbyname( optarg );
	    if ( !md ) {
		fprintf( stderr, "%s: unsupported checksum\n", optarg );
		exit( 2 );
	    }
	    cksum = 1;  
	    break; 
	case 'P':
	    prefix = optarg;
	    break;
	case 'n':
	    amode = R_OK;
	    updatetran = 0;
	    break;
	case 'V':
	    printf( "%s\n", version );
	    printf( "%s\n", checksumlist );
	    exit( 0 );

	case 'v':
	    verbose++;
	    break;

	case 'q':
	    verbose = 0;
	    break;
	case '?':
	    err++;
	    break;
	default:
	    err++;
	    break;
	}
    }

    if ( cksum == 0 ) {
	err++;
    }

    tpath = argv[ optind ];

    if ( err || ( argc - optind != 1 ) ) {
	fprintf( stderr, "usage: %s [ -nqVv ] [ -P prefix ] ", argv[ 0 ] );
	fprintf( stderr, "-c checksum transcript\n" );
	exit( 2 );
    }

    if ( stat( tpath, &st ) != 0 ) {
	perror( tpath );
	exit( 2 );
    }
    if ( !S_ISREG( st.st_mode )) {
	fprintf( stderr, "%s: not a regular file\n", tpath );
	return( 2 );
    }

    if ( access( tpath, amode ) !=0 ) {
	perror( tpath );
	exit( 2 );
    }

    if ( ( f = fopen( tpath, "r" ) ) == NULL ) {
	perror( tpath );
	exit( 2 );
    }

    if ( updatetran ) {
	memset( upath, 0, MAXPATHLEN );
	if ( snprintf( upath, MAXPATHLEN, "%s.%i", tpath, (int)getpid() )
		> MAXPATHLEN - 1) {
	    fprintf( stderr, "%s.%i: path too long\n", tpath, (int)getpid() );
	}

	if ( stat( tpath, &st ) != 0 ) {
	    perror( tpath );
	    exit( 2 );
	}

	/* Open file */
	if ( ( ufd = open( upath, O_WRONLY | O_CREAT | O_EXCL,
		st.st_mode ) ) < 0 ) {
	    perror( upath );
	    exit( 2 );
	}
	if ( ( ufs = fdopen( ufd, "w" ) ) == NULL ) {
	    perror( upath );
	    exit( 2 );
	}
    }

    /* Get transcript name from transcript path */
    if ( ( transcript = strrchr( tpath, '/' ) ) == NULL ) {
	transcript = tpath;
	tpath = ".";
    } else {
	*transcript = (char)'\0';
	transcript++;
    }

    /* count the lines */
    if ( verbose == 2 ) {
	while ( fgets( tline, MAXPATHLEN, f ) != NULL ) {
	    lcount++;
	}

	rewind( f );
    }

    while ( fgets( tline, MAXPATHLEN, f ) != NULL ) {
	linenum++;
	updateline = 0;

	/* Check line length */
	len = strlen( tline );
	if (( tline[ len - 1 ] ) != '\n' ) {
	    fprintf( stderr, "%s: %d: line too long\n", tpath, linenum);
	    exit( 2 );
	}
	/* save transcript line -- must free */
	if ( ( line = strdup( tline ) ) == NULL ) {
	    perror( "strdup" );
	    exit( 2 );
	}

	tac = acav_parse( NULL, tline, &targv );

        /* Skip blank lines and comments */
        if (( tac == 0 ) || ( *targv[ 0 ] == '#' )) {
	    if ( updatetran ) {
		fprintf( ufs, "%s", line );
	    }
            goto done;
        }
	if ( tac == 1 ) {
	    fprintf( stderr, "line %d: invalid transcript line\n", linenum );
	    exit( 2 );
	}

	if ( *targv[ 0 ] == '-' ) {
	    remove = 1;
	    targv++;
	} else {
	    remove = 0;
	}

	if ( snprintf( path, MAXPATHLEN, "%s", decode( targv[ 1 ] ))
		> MAXPATHLEN - 1) {
	    fprintf( stderr, "line %d: path too long\n", linenum );
	    exit( 2 );
	}
	    
	/* Check transcript order */
	if ( prepath != 0 ) {
	    if ( pathcmp( path, prepath ) < 0 ) {
		fprintf( stderr, "line %d: bad sort order\n", linenum );
		exit( 2 );
	    }
	}
	len = strlen( targv[ 1 ] );
	if ( snprintf( prepath, MAXPATHLEN, "%s", path) > MAXPATHLEN ) {
	    fprintf( stderr, "line %d: path too long\n", linenum );
	    exit( 2 );
	}

	if ((( *targv[ 0 ] != 'f' )  && ( *targv[ 0 ] != 'a' )) || ( remove )) {
	    if ( updatetran ) {
		fprintf( ufs, "%s", line );
	    }
	    goto done;
	}

	if ( tac != 8 ) {
	    fprintf( stderr, "line %d: %d arguments should be 8\n",
		    linenum, tac );
	    exit( 2 );
	}

	/* check to see if file against prefix */
	if ( prefix != NULL ) {
	    if ( strncmp( decode( targv[ 1 ] ), prefix, strlen( prefix ))
		    != 0 ) {
		if ( updatetran ) {
		    fprintf( ufs, "%s", line );
		}
		goto done;
	    }
	    prefixfound = 1;
	}

	if ( snprintf( path, MAXPATHLEN, "%s/../file/%s/%s", tpath, transcript,
		decode( targv[ 1 ] )) > MAXPATHLEN - 1 ) {
	    fprintf( stderr, "%s/../file/%s/%s: path too long\n", tpath,
		transcript, decode( targv[ 1 ] ));
	    exit( 2 );
	}

	/*
	 * Since this tool is run on the server, all files can be treated
	 * as regular files.
	 *
	 * HFS+ files saved onto the server are converted to applesingle files.
	 *
	 * fsdiff uses do_achskum( ) to calculate the cksum of HFS+ files.
	 *
	 * do_acksum( ) creates a cksum for the associated applesingle file.
	 */

	/* check size */
	if ( stat( path, &st) != 0 ) {
	    perror( path );
	    exit( 2 );
	}
	if ( st.st_size != strtoofft( targv[ 6 ], NULL, 10 )) {
	    if ( verbose && !updatetran ) printf( "%s: size wrong\n",
		    decode( targv[ 1 ] ));
	    ucount++;
	    if ( updatetran ) {
		if ( verbose && updatetran ) printf( "%s: size updated\n",
			decode( targv[ 1 ] ));
	    }
	    updateline = 1;
	}

	if (( cksumsize = do_cksum( path, lcksum )) < 0 ) {
	    perror( path );
	    exit( 2 );
	}
	if ( cksumsize != st.st_size ) {
	    fprintf( stderr, "line %d: checksum wrong in transcript\n",
		linenum );
	    exit( 2 );
	}

	/* check cksum */
	if ( strcmp( lcksum, targv[ 7 ] ) != 0 ) {
	    if ( verbose && !updatetran ) printf( "%s: cksum wrong\n",
		    decode( targv[ 1 ] ));
	    ucount++;
	    if ( updatetran ) {
		if ( verbose && updatetran ) printf( "%s: cksum updated\n",
		    decode( targv[ 1 ] )); 
	    }
	    updateline = 1;
	}

	if ( updatetran ) {
	    if ( updateline ) {
		/* Line incorrect */
		/* Check to see if checksum is listed in transcript */
		if ( strcmp( targv[ 7 ], "-" ) != 0) {
		    /* use mtime from server */
		    fprintf( ufs, "%s %-37s %4s %5s %5s %9ld "
			    "%7" PRIofft "d %s\n",
			targv[ 0 ], targv[ 1 ], targv[ 2 ], targv[ 3 ],
			targv[ 4 ], st.st_mtime, st.st_size, lcksum );
		} else {
		    /* use mtime from transcript */
		    fprintf( ufs, "%s %-37s %4s %5s %5s %9s "
			    "%7" PRIofft "d %s\n",
			targv[ 0 ], targv[ 1 ], targv[ 2 ], targv[ 3 ],
			targv[ 4 ], targv[ 5 ], st.st_size, lcksum );
		    }
	    } else {
		/* Line correct */
		fprintf( ufs, "%s", line );
	    }
	}
done:
	if ( verbose == 2 && ( tac > 0 && *line != '#' )) {
	    pct = ((( float )linenum / ( float )lcount ) * 100.0 );
	    if (( int )pct != lastpct ) {
		printf( "%%%.2d %s\n", ( int )pct, decode( targv[ 1 ] ));
	    }

	    lastpct = ( int )pct;
	}

	free( line );
    }

    if ( !prefixfound && prefix != NULL ) {
	if ( verbose ) printf( "warning: prefix \"%s\" not found\n", prefix );
    }

    if ( updatetran ) {

	if ( ucount ) {
	    /* reconstruct full transcript path */
	    if ( *tpath != '.' ) {
		*(transcript - 1) = '/';
	    } else {
		tpath = transcript;
	    }

	    if ( rename( upath, tpath ) != 0 ) {
		fprintf( stderr, "rename %s to %s failed: %s\n", upath, tpath,
		    strerror( errno ));
		exit( 2 );
	    }
	    if ( verbose ) printf( "%s: updated\n", transcript );
	    exit( 1 );
	} else {
	    if ( unlink( upath ) != 0 ) {
		perror( upath );
		exit( 2 );
	    }
	    if ( verbose ) printf( "%s: verified\n", transcript );
	    exit( 0 );
	}
    } else {
	if ( ucount ) {
	    if ( verbose ) printf( "%s: incorrect\n", transcript );
	    exit( 1 );
	} else {
	    if ( verbose ) printf( "%s: verified\n", transcript );
	    exit( 0 );
	}
    }
    exit( 2 );
}
