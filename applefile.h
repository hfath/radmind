/*
 * Copyright (c) 2003 Regents of The University of Michigan.
 * All Rights Reserved.  See COPYRIGHT.
 */

/*
 * applesingle format:
 *  header:
 *      -magic number (4 bytes)
 *      -version number (4 bytes)
 *      -filler (16 bytes)
 *      -number of entries (2 bytes)
 *      -x number of entries, with this format:
 *          id (4 bytes)
 *          offset (4 bytes)
 *          length (4 bytes)
 *  data:
 *      -finder info
 *      -rsrc fork
 *      -data fork
 */

#define AS_HEADERLEN	26
#define AS_MAGIC	0x00051600
#define AS_VERSION	0x00020000
#define AS_NENTRIES	3

#define ASEID_DFORK	1
#define ASEID_RFORK	2
#define ASEID_FINFO	9

#define AS_FIE		0	/* for array of ae_entry structs */
#define AS_RFE		1
#define AS_DFE		2	

#define FINFOLEN	32

/* applesingle entry */
struct as_entry {
    uint32_t	ae_id;
    uint32_t	ae_offset;
    uint32_t	ae_length;
};

/* applesingle header */
struct as_header {
    uint32_t	ah_magic;
    uint32_t	ah_version;
    uint8_t	ah_filler[ 16 ];
    uint16_t	ah_num_entries;
};

struct finderinfo {
    uint32_t   	fi_size;
    uint8_t	fi_data[ FINFOLEN ];
};

struct applefileinfo {
#ifdef __APPLE__
    char                rsrc_path[ MAXPATHLEN ];
#endif /* __APPLE__ */
    struct finderinfo	fi;		// finder info
    struct as_entry	as_ents[ 3 ];	// Apple Single entries
					// For Finder info, rcrs and data forks
    off_t		as_size;	// Total apple single file size 
};
