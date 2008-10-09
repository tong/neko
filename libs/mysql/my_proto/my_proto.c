/* ************************************************************************ */
/*																			*/
/*  MYSQL 5.0 Protocol Implementation 										*/
/*  Copyright (c)2008 Nicolas Cannasse										*/
/*																			*/
/*  This program is free software; you can redistribute it and/or modify	*/
/*  it under the terms of the GNU General Public License as published by	*/
/*  the Free Software Foundation; either version 2 of the License, or		*/
/*  (at your option) any later version.										*/
/*																			*/
/*  This program is distributed in the hope that it will be useful,			*/
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of			*/
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the			*/
/*  GNU General Public License for more details.							*/
/*																			*/
/*  You should have received a copy of the GNU General Public License		*/
/*  along with this program; if not, write to the Free Software				*/
/*  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA */
/*																			*/
/* ************************************************************************ */
#include <math.h>
#include "my_proto.h"

int my_recv( MYSQL *m, void *buf, int size ) {
	while( size ) {
		int len = psock_recv(m->s,(char*)buf,size);
		if( len < 0 ) return 0;
		buf = ((char*)buf) + len;
		size -= len;
	}
	return 1;
}


int my_send( MYSQL *m, void *buf, int size ) {
	while( size ) {
		int len = psock_send(m->s,(char*)buf,size);
		if( len < 0 ) return 0;
		buf = ((char*)buf) + len;
		size -= len;
	}
	return 1;
}

int my_read( MYSQL_PACKET *p, void *buf, int size ) {
	if( p->size - p->pos < size ) {
		p->error = 1;
		return 0;
	}
	memcpy(buf,p->buf + p->pos,size);
	p->pos += size;
	return 1;
}

unsigned char my_read_byte( MYSQL_PACKET *p ) {
	unsigned char c;
	if( !my_read(p,&c,1) )
		return 0;
	return c;
}

unsigned short my_read_ui16( MYSQL_PACKET *p ) {
	unsigned short i;
	if( !my_read(p,&i,2) )
		return 0;
	return i;
}

int my_read_int( MYSQL_PACKET *p ) {
	int i;
	if( !my_read(p,&i,4) )
		return 0;
	return i;
}

int my_read_bin( MYSQL_PACKET *p ) {
	int c = my_read_byte(p);
	if( c <= 250 )
		return c;
	if( c == 251 )
		return -1; // NULL
	if( c == 252 )
		return my_read_ui16(p);
	if( c == 253 ) {
		c = 0;
		my_read(p,&c,3);
		return c;
	}
	if( c == 254 )
		return my_read_int(p);
	p->error = 1;
	return 0;
}

const char *my_read_string( MYSQL_PACKET *p ) {
	char *str;
	if( p->pos >= p->size ) {
		p->error = 1;
		return "";
	}
	str = p->buf + p->pos;
	p->pos += strlen(str) + 1;
	return str;
}

char *my_read_bin_str( MYSQL_PACKET *p ) {
	int size = my_read_bin(p);
	char *str;
	if( size == -1 )
		return NULL;
	if( p->error || p->pos + size > p->size ) {
		p->error = 1;
		return NULL;
	}
	str = (char*)malloc(size + 1);
	memcpy(str,p->buf + p->pos, size);
	str[size] = 0;
	p->pos += size;
	return str;
}

int my_read_packet( MYSQL *m, MYSQL_PACKET *p ) {
	unsigned int psize;
	p->pos = 0;
	p->error = 0;
	if( !my_recv(m,&psize,4) ) {
		p->id = -1;
		p->error = 1;
		p->size = 0;
		return 0;
	}
	p->id = (psize >> 24);
	psize &= 0xFFFFFF;
	p->size = psize;
	if( p->mem < (int)psize ) {
		free(p->buf);
		p->buf = (char*)malloc(psize + 1);
		p->mem = psize;
	}
	p->buf[psize] = 0;
	if( !my_recv(m,p->buf,psize) ) {
		p->error = 1;
		p->size = 0;
		p->buf[0] = 0;
		return 0;
	}
	return 1;
}

int my_send_packet( MYSQL *m, MYSQL_PACKET *p ) {
	unsigned int header = p->size | (p->id << 24);
	if( !my_send(m,&header,4) ) {
		p->error = 1;
		return 0;
	}
	if( !my_send(m,p->buf,p->size) ) {
		p->error = 1;
		return 0;
	}
	return 1;
}

void my_begin_packet( MYSQL_PACKET *p, int id, int minsize ) {
	if( p->mem < minsize ) {
		free(p->buf);
		p->buf = (char*)malloc(minsize + 1);
		p->mem = minsize;
	}
	p->error = 0;
	p->id = id;
	p->size = 0;
}

void my_write( MYSQL_PACKET *p, const void *data, int size ) {
	if( p->size + size > p->mem ) {
		char *buf2;
		if( p->mem == 0 ) p->mem = 32;
		do {
			p->mem <<= 1;
		} while( p->size + size > p->mem );
		buf2 = (char*)malloc(p->mem);
		memcpy(buf2,p->buf,p->size);
		free(p->buf);
		p->buf = buf2;
	}
	memcpy( p->buf + p->size , data, size );
	p->size += size;
}

void my_write_byte( MYSQL_PACKET *p, int i ) {
	unsigned char c = (unsigned char)i;
	my_write(p,&c,1);
}

void my_write_ui16( MYSQL_PACKET *p, int i ) {
	unsigned short c = (unsigned char)i;
	my_write(p,&c,2);
}

void my_write_int( MYSQL_PACKET *p, int i ) {
	my_write(p,&i,4);
}

void my_write_string( MYSQL_PACKET *p, const char *str ) {
	my_write(p,str,strlen(str) + 1);
}

void my_write_bin( MYSQL_PACKET *p, int size ) {
	if( size <= 250 ) {
		unsigned char l = (unsigned char)size;
		my_write(p,&l,1);
	} else if( size < 0x10000 ) {
		unsigned char c = 252;
		unsigned short l = (unsigned short)size;
		my_write(p,&c,1);
		my_write(p,&l,2);
	} else if( size < 0x1000000 ) {
		unsigned char c = 253;
		unsigned int l = (unsigned short)size;
		my_write(p,&c,1);
		my_write(p,&l,3);
	} else {
		unsigned char c = 254;
		my_write(p,&c,1);
		my_write(p,&size,4);
	}
}

void my_crypt( unsigned char *out, const unsigned char *s1, const unsigned char *s2, unsigned int len ) {
	unsigned int i;
	for(i=0;i<len;i++)
		out[i] = s1[i] ^ s2[i];
}

void my_encrypt_password( const char *pass, const char *seed, SHA1_DIGEST out ) {
	SHA1_CTX ctx;
	SHA1_DIGEST hash_stage1, hash_stage2;
	// stage 1: hash password
	sha1_init(&ctx);
	sha1_update(&ctx,pass,strlen(pass));;
	sha1_final(&ctx,hash_stage1);
	// stage 2: hash stage 1; note that hash_stage2 is stored in the database
	sha1_init(&ctx);
	sha1_update(&ctx, hash_stage1, SHA1_SIZE);
	sha1_final(&ctx, hash_stage2);
	// create crypt string as sha1(message, hash_stage2)
	sha1_init(&ctx);
	sha1_update(&ctx, seed, SHA1_SIZE);
	sha1_update(&ctx, hash_stage2, SHA1_SIZE);
	sha1_final( &ctx, out );
	// xor the result
	my_crypt(out,out,hash_stage1,SHA1_SIZE);
}

typedef struct {
	unsigned long seed1;
	unsigned long seed2;
	unsigned long max_value;
	double max_value_dbl;
} rand_ctx;

static void random_init( rand_ctx *r, unsigned long seed1, unsigned long seed2 ) {
	r->max_value = 0x3FFFFFFFL;
	r->max_value_dbl = (double)r->max_value;
	r->seed1 = seed1 % r->max_value ;
	r->seed2 = seed2 % r->max_value;
}

static double my_rnd( rand_ctx *r ) {
	r->seed1 = (r->seed1 * 3 + r->seed2) % r->max_value;
	r->seed2 = (r->seed1 + r->seed2 + 33) % r->max_value;
	return (((double) r->seed1)/r->max_value_dbl);
}

static void hash_password( unsigned long *result, const char *password, int password_len ) {
	register unsigned long nr = 1345345333L, add = 7, nr2 = 0x12345671L;
	unsigned long tmp;
	const char *password_end = password + password_len;
	for(; password < password_end; password++) {
		if( *password == ' ' || *password == '\t' )
			continue;
		tmp = (unsigned long)(unsigned char)*password;
		nr ^= (((nr & 63)+add)*tmp)+(nr << 8);
		nr2 += (nr2 << 8) ^ nr;
		add += tmp;
	}
	result[0] = nr & (((unsigned long) 1L << 31) -1L);
	result[1] = nr2 & (((unsigned long) 1L << 31) -1L);
}

void my_encrypt_pass_323( const char *password, const char seed[SEED_LENGTH_323], char to[SEED_LENGTH_323] ) {
	rand_ctx r;
	unsigned long hash_pass[2], hash_seed[2];
	char extra, *to_start = to;
	const char *seed_end = seed + SEED_LENGTH_323;
	hash_password(hash_pass,password,(unsigned int)strlen(password));
	hash_password(hash_seed,seed,SEED_LENGTH_323);
	random_init(&r,hash_pass[0] ^ hash_seed[0],hash_pass[1] ^ hash_seed[1]);
	while( seed < seed_end ) {
		*to++ = (char)(floor(my_rnd(&r)*31)+64);
		seed++;
	}
	extra= (char)(floor(my_rnd(&r)*31));
	while( to_start != to )
		*(to_start++) ^= extra;
}

int my_escape_string( int charset, char *sout, const char *sin, int length ) {
	// !! we don't handle UTF-8 chars here !!
	const char *send = sin + length;
	char *sbegin = sout;
	while( sin != send ) {
		char c = *sin++;
		switch( c ) {
		case 0:
			*sout++ = '\\';
			*sout++ = '0';
			break;
		case '\n':
			*sout++ = '\\';
			*sout++ = 'n';
			break;
		case '\r':
			*sout++ = '\\';
			*sout++ = 'r';
			break;
		case '\\':
			*sout++ = '\\';
			*sout++ = '\\';
			break;
		case '\'':
			*sout++ = '\\';
			*sout++ = '\'';
			break;
		case '"':
			*sout++ = '\\';
			*sout++ = '"';
			break;
		case '\032':
			*sout++ = '\\';
			*sout++ = 'Z';
			break;
		default:
			*sout++ = c;
		}
	}
	*sout = 0;
	return sout - sbegin;
}

int my_escape_quotes( int charset, char *sout, const char *sin, int length ) {
	const char *send = sin + length;
	char *sbegin = sout;
	while( sin != send ) {
		char c = *sin++;
		*sout++ = c;
		if( c == '\'' )
			*sout++ = c;
	}
	*sout = 0;
	return sout - sbegin;
}

/* ************************************************************************ */