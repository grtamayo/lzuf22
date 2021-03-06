/*
	---- A Lempel-Ziv Unary (LZUF) Coding Implementation ----

	Filename:      lzuf22.c  (decompressor:  lzufd22.c)
	Written by:    Gerald Tamayo, Oct. 22, 2008 (2/24/2022)
	
	Description:   Traditional LZ77/LZSS with unary "folded" codes of succeeding bytes 
	               from minimum match.

	Encoding:

		literal byte:         2 bits + 8 bits
		match == MIN_LEN  :   2 bits + position
		match  > MIN_LEN  :   1 bit  + length + position

	NOTES:
		This method extends traditional implementation of LZ77/LZSS coding
		via unary "folded" codes (UFC), which has arisen from my implementation 
		of LZT coding, where the minimum match length is implied.

		The "LZUF" method refers to the technique of unary encoding the LZ77 match
		length codes, and shortening the unary codes via 1D folding. Decoder performs
		no searching, as in traditional LZ77.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utypes.h"
#include "gtbitio.c"
#include "lzhash.c"
#include "mtf.c"
#include "ucodes.c"

/* the decompressor's must also equal these values. */
#define LTCB              17                /* 12..21 tested working */
#ifdef  LTCB
	#define NUM_POS_BITS   LTCB
#else
	#define NUM_POS_BITS   15
#endif

#define WIN_BUFSIZE    (1<<NUM_POS_BITS)
#define WIN_MASK       (WIN_BUFSIZE-1)
#define HASH_SHIFT     (NUM_POS_BITS-8)

#define PAT_BUFSIZE    (WIN_BUFSIZE>>1)  /* must be a power of 2. */
#define PAT_MASK       ((PAT_BUFSIZE)-1)
#define MIN_LEN         4                /* minimum string size >= 2 */

#define NMATCH        196
#define FAR_LIST_BITS  12
#define FAR_LIST       (1<<(FAR_LIST_BITS))

#define HASH_BYTES_N    4
/* 4-byte hash */
#define hash(buf,pos,mask1,mask2) \
	((buf[ (pos)&(mask1)]<<HASH_SHIFT) \
	^(buf[((pos)+1)&(mask1)]<<1) \
	^(buf[((pos)+2)&(mask1)]<<4) \
	^(buf[((pos)+3)&(mask1)]<<7)&(mask2))
	
typedef struct {
	char algorithm[4];
	unsigned long file_size;
} file_stamp;

typedef struct {
	unsigned int pos, len;
} dpos_t;

dpos_t dpos;
unsigned char win_buf[ WIN_BUFSIZE ];      /* the "sliding" window buffer. */
unsigned char pattern[ PAT_BUFSIZE ];      /* the "look-ahead" buffer. */
int win_cnt = 0, pat_cnt = 0, buf_cnt = 0;  /* some counters. */
unsigned long in_file_len = 0, out_file_len = 0;

int len_CODE = 0;  /* the transmitted length code. */
int large_text;

void copyright( void );
dpos_t search ( uchar w[], uchar p[] );
void put_codes( dpos_t *dpos );

int main( int argc, char *argv[] )
{
	float ratio = 0.0;
	int i;
	file_stamp fstamp;

	if ( argc != 3 ) {
		fprintf(stderr, "\n Usage: lzuf22 infile outfile");
		copyright();
		return 0;
	}
	init_buffer_sizes( (1<<15) );
	
	if ( (gIN = fopen( argv[1], "rb" )) == NULL ) {
		fprintf(stderr, "\nError opening input file.");
		return 0;
	}
	if ( (pOUT = fopen( argv[2], "wb" )) == NULL ) {
		fprintf(stderr, "\nError opening output file.");
		return 0;
	}
	init_put_buffer();

	fprintf(stderr, "\n--[ A Lempel-Ziv Unary (LZUF) Coding Implementation ]--\n");
	fprintf(stderr, "\nWindow Buffer size used  = %15lu bytes", (ulong) WIN_BUFSIZE );
	fprintf(stderr, "\nLook-Ahead Buffer size   = %15lu bytes", (ulong) PAT_BUFSIZE );

	fprintf(stderr, "\n\nName of input file : %s", argv[1] );

	/* display file length. */
	fseek( gIN, 0, SEEK_END );
	in_file_len = ftell( gIN );
	fprintf(stderr, "\nLength of input file     = %15lu bytes", in_file_len );

	/* Write the FILE STAMP. */
	rewind( pOUT );
	strcpy( fstamp.algorithm, "LZU" );
	if ( in_file_len < 1048576 ) large_text = 0;
	else large_text = 1;
	fstamp.algorithm[3] = large_text;
	fstamp.file_size = in_file_len;
	fwrite( &fstamp, sizeof(file_stamp), 1, pOUT );

	/* start Compressing to output file. */
	fprintf(stderr, "\n\nCompressing...");

	/* initialize the table of pointers. */
	if ( !alloc_lzhash(WIN_BUFSIZE) ) goto halt_prog;

	/* set the sliding-window to all zero (0) values. */
	memset( win_buf, 0, WIN_BUFSIZE );

	/* initialize the search list. */
	for ( i = 0; i < WIN_BUFSIZE; i++ ) {
		lznext[i] = LZ_NULL;
		lzprev[i] = LZ_NULL;
		insert_lznode( hash(win_buf,i,WIN_MASK, WIN_MASK), i );
	}

	/* make sure to rewind the input file */
	rewind(gIN);

 	/* fill the pattern buffer. */
	buf_cnt = fread( pattern, 1, PAT_BUFSIZE, gIN );

	/* initialize the input buffer. */
	init_get_buffer();
	
	/* initialize MTF list. */
	alloc_mtf(256);
	
	/* compress */
	while ( buf_cnt > 0 ) {  /* look-ahead buffer not empty? */
		dpos = search( win_buf, pattern );

		/* encode prefix bits. */
		if ( dpos.len > MIN_LEN ) { /* more than MIN_LEN match? */
			put_ONE();            /* yes, send a 1 bit. */
		}
		else if ( dpos.len == MIN_LEN ) { /* exactly MIN_LEN matching characters? */
			put_ZERO();          /* yes, send a 0 bit. */
			put_ONE();           /* and a 1 bit. */
		}
		else {                  /* less than MIN_LEN matching characters. */
			put_ZERO();          /* send a 0 bit. */
			put_ZERO();          /* one more 0 bit to indicate a no match. */
		}

		/* encode window position or len codes. */
		put_codes( &dpos );
	}
	flush_put_buffer();
	fprintf(stderr, "complete.");

	/* get outfile's size and get compression ratio. */
	out_file_len = ftell( pOUT );

	fprintf(stderr, "\n\nName of output file: %s", argv[2] );
	fprintf(stderr, "\nLength of input file     = %15lu bytes", in_file_len );
	fprintf(stderr, "\nLength of output file    = %15lu bytes", out_file_len );

	ratio = (((float) in_file_len - (float) out_file_len) /
		(float) in_file_len ) * (float) 100;
	fprintf(stderr, "\nCompression ratio:         %15.2f %%", ratio );

	copyright();

	halt_prog:

	free_put_buffer();
	free_get_buffer();
	free_lzhash();
	free_mtf_table();
	if ( gIN ) fclose( gIN );
	if ( pOUT ) fclose( pOUT );

	return 0;
}

void copyright( void )
{
	fprintf(stderr, "\n\n Written by: Gerald Tamayo, 2008/2022\n");
}

/*
This function searches the sliding window buffer for the largest
"string" stored in the pattern buffer.

The function uses an "array of pointers" to singly-linked
lists, which contain the various occurrences or "positions" of a
particular character in the sliding-window.

Note:

	We output 2 bits for a string of size MIN_LEN, so in terms of 
	the transmitted length code, MINIMUM_MATCH_LENGTH is actually 
	prev_LEN = (MIN_LEN+1) here, not MIN_LEN.
*/
dpos_t search( uchar w[], uchar p[] )
{
	register int i, j, k, m = 0, n = 0;
	dpos_t dpos = { 0, 0 };

	/* point to start of lzhash[ index ] */
	i = lzhash[ hash(p,pat_cnt,PAT_MASK,WIN_MASK) ];
	
	if ( buf_cnt > 1 ) while ( i != LZ_NULL ) {
		/* ---- FAST LZ77 SEARCH ----
		
		This implements P. Gutmann's fast string-matching algorithm...
		(see "Differential Ziv-Lempel Text compression,"
		P. Fenwick, Journal of Universal Computer Science, Vol. 1, No. 8, 
		1995, pp.591-602).
		
		First, match the "context" string (the current longest string or
		the partial match) plus 1 "suffix" byte (ie., the first byte tested
		for a match) from right to left...

		The context length (i.e., the current match, dpos.len) is a
		"skip count" as in Boyer-Moore search; thus our approach here
		does not need to prepare a "skip table" for the symbols.

		dpos.len points to the first suffix symbol; if it is a mismatch,
		the search can end immediately.  We verify the *context* string
		first since the *suffix* string may match completely but the context
		string may not, and hence would be a mismatch for the whole string.
		*/
		j = (pat_cnt+dpos.len) & PAT_MASK;
		k = dpos.len;
		do {
			if ( p[j] != w[ (i+k) & WIN_MASK ] ) {
				goto skip_search;  /* allows fast search. */
			}
			if ( j-- == 0 ) j=PAT_BUFSIZE-1;
		} while ( (--k) >= 0 );

		/* then match the rest of the "suffix" string from left to right. */
		j = (pat_cnt+dpos.len+1) & PAT_MASK;
		k = dpos.len+1;
		if ( k < buf_cnt )
			while ( p[ j++ & PAT_MASK ] == w[ (i+k) & WIN_MASK ]
				&& (++k) < buf_cnt ) ;

		/* greater than previous length, record it. */
		dpos.pos = i;
		dpos.len = k;

		/* maximum match, end the search. */
		if ( k == buf_cnt || ++n == NMATCH ) break;
		
		skip_search:
		
		if ( ++m == FAR_LIST ) break;

		/* point to next occurrence of this hash index. */
		i = lznext[i];
	}

	return dpos;
}

/*
Transmits a length/position pair of codes according
to the match length received.

When we receive a match length of 0, we quickly set the length
code to 1 (we have to "slide" through the window buffer at least
one character at a time).

Due to the algorithm, we only encode the match length if it is
greater than MIN_LEN. Next, a byte or a "position code" is
transmitted.

Then this function properly performs the "sliding" part by
copying the matched characters to the window buffer; note that
the linked list is also updated.

Finally, it "gets" characters from the input file according
to the number of matching characters.
*/
void put_codes( dpos_t *dpos )
{
	int i, k;
	
	/* the whole string match is encoded completely. (Oct. 19, 2008) */
	if ( dpos->len > MIN_LEN ) { /* encode unary len_CODE only if > MIN_LEN. */
		/* suffix string length. */
		len_CODE = dpos->len - (MIN_LEN+1);
		
		if ( !large_text ) put_vlcode(len_CODE, 1);
		else {
			#define MFOLD 2         /* m = 2 works for this type of encoding */
			
			i = len_CODE >> MFOLD;  /* "fold" the suffix string. (10/21/2008) */
			while ( i-- ) {         /*   encode only a part of the unary codes. */
				put_ONE();
			}
			put_nbits( (len_CODE % (1 << MFOLD)) << 1, MFOLD+1 );
		}
	}
	
	/* encode position for match len >= MIN_LEN. */
	if ( dpos->len >= MIN_LEN ) {
		k = dpos->pos;
		put_nbits( k, NUM_POS_BITS );
	}
	else {
		dpos->len = 1;
		/* emit just the byte. */
		k = (unsigned char) pattern[pat_cnt];
		/* Implemented VL coding for better compression. (1/12/2010) */
		put_vlcode(mtf(k), 3);
	}
	
	/* ---- if its a match, then "slide" the buffer. ---- */
	if ( (k=win_cnt-(HASH_BYTES_N-1)) < 0 ) {
		/* record the left-most string index (k). */
		k = WIN_BUFSIZE+k;
	}
	
	/* remove the strings (i.e., positions) from the hash list. */
	for ( i = 0; i < (dpos->len+(HASH_BYTES_N-1)); i++ ) {
		delete_lznode( hash(win_buf,(k+i),WIN_MASK,WIN_MASK), (k+i) & WIN_MASK );
	}
	
	i = dpos->len;
	while ( i-- ) {
		/* write the character to the window buffer. */
		*(win_buf +((win_cnt+i) & (WIN_MASK)) ) =
			*(pattern + ((pat_cnt+i) & PAT_MASK));
	}

	/* with the new characters, rehash at this position. */
	for ( i = 0; i < (dpos->len+(HASH_BYTES_N-1)); i++ ) {
		insert_lznode( hash(win_buf,(k+i),WIN_MASK,WIN_MASK), (k+i) & WIN_MASK );
	}
	
	/* get dpos.len bytes */
	for ( i = 0; i < dpos->len; i++ ){
		if( (k=gfgetc()) != EOF ) {
			*(pattern + ((pat_cnt+i) & PAT_MASK)) =
				(uchar) k;
		}
		else break;
	}

	/* update counters. */
	buf_cnt -= (dpos->len-i);
	win_cnt = (win_cnt+dpos->len) & WIN_MASK;
	pat_cnt = (pat_cnt+dpos->len) & PAT_MASK;
}
