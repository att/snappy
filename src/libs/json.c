/* ==========================================================================
 * json.c - Path Autovivifying JSON C Library
 * --------------------------------------------------------------------------
 * Copyright (c) 2012, 2013  William Ahern
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 * NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 * ==========================================================================
 */
#include <limits.h>	/* INT_MAX */
#include <stddef.h>	/* size_t offsetof */
#include <stdarg.h>	/* va_list va_start va_end va_arg */
#include <stdint.h>     /* SIZE_MAX uintptr_t */
#include <stdlib.h>	/* malloc(3) realloc(3) free(3) strtod(3) */
#include <stdio.h>	/* EOF snprintf(3) fopen(3) fclose(3) ferror(3) clearerr(3) */

#include <string.h>	/* memset(3) strncmp(3) strlen(3) strlcpy(3) stpncpy(3) */

#include <math.h>	/* HUGE_VAL modf(3) isnormal(3) */

#include <errno.h>	/* errno ERANGE EOVERFLOW EINVAL */

#include <sys/queue.h>

#include "llrb.h"
#include "json.h"


/*
 * M I S C E L L A N E O U S  R O U T I N E S
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#define JSON_PASTE(x, y) x ## y
#define JSON_XPASTE(x, y) JSON_PASTE(x, y)

#define JSON_MIN(a, b) (((a) < (b))? (a) : (b))
#define JSON_CMP(a, b) (((a) < (b))? -1 : ((a) > (b))? 1 : 0)

#define json_countof(a) (sizeof (a) / sizeof *(a))
#define json_endof(a) (&(a)[json_countof(a)])

#if __alignas_is_defined
#define JSON_ALIGNAS(n) _Alignas(n)
#else
#define JSON_ALIGNAS(n) __attribute__((aligned(n)))
#endif

#undef SAY_
#define SAY_(file, func, line, fmt, ...) \
	fprintf(stderr, "%s:%d: " fmt "%s", __func__, __LINE__, __VA_ARGS__)
#undef SAY
#define SAY(...) SAY_(__FILE__, __func__, __LINE__, __VA_ARGS__, "\n")
#undef HAI
#define HAI SAY("hai")

#ifdef __GNUC__
#define JSON_NOTUSED __attribute__((unused))
#else
#define JSON_NOTUSED
#endif


#if __linux
static inline size_t json_strlcpy(char *dst, const char *src, size_t len) {
	char *end = stpncpy(dst, src, len);

	if (len)
		dst[len - 1] = '\0';

	return end - dst;
} /* json_strlcpy() */
#else
#define json_strlcpy(...) strlcpy(__VA_ARGS__)
#endif


static inline size_t json_strlen(const char *src) {
	return (src)? strlen(src) : 0;
} /* json_strlen() */


static inline void *json_make(size_t size, int *error) {
	void *p;

	if (!(p = malloc(size)))
		*error = errno;

	return p;
} /* json_make() */


static void *json_make0(size_t size, int *error) {
	void *p;

	if ((p = json_make(size, error)))
		memset(p, 0, size);

	return p;
} /* json_make0() */


#define json_isctype(map, ch) \
	!!((map)[((ch) & 0xff) / 64] & (1ULL << ((ch) & 0xff)))

static inline _Bool json_isdigit(unsigned char ch) {
	unsigned long long digit[4] = { 0x3ff000000000000ULL, 0, 0, 0 };

	return json_isctype(digit, ch);
} /* json_isdigit() */


static inline _Bool json_isgraph(unsigned char ch) {
	unsigned long long graph[4] = { 0xfffffffe00000000ULL, 0x7fffffffffffffffULL, 0, 0 };

	return json_isctype(graph, ch);
} /* json_isgraph() */


static inline _Bool json_isascii(unsigned char ch) {
	return !(0x80 & ch);
} /* json_isascii() */


static inline _Bool json_isnumber(unsigned char ch) {
	/* + - . 0-9 E e */
	unsigned long long number[4] = { 0x3ff680000000000ULL, 0x2000000020ULL, 0, 0 };

	return json_isctype(number, ch);
} /* json_isnumber() */


static inline int json_safeadd(size_t *r, size_t a, size_t b) {
	if (~a < b)
		return ENOMEM;

	*r = a + b;

	return 0;
} /* json_safeadd() */


JSON_PUBLIC const char *json_strtype(enum json_type type) {
	switch (type) {
	default:
		/* FALL THROUGH */
	case JSON_T_NULL:
		return "null";
	case JSON_T_BOOLEAN:
		return "boolean";
	case JSON_T_NUMBER:
		return "number";
	case JSON_T_STRING:
		return "string";
	case JSON_T_ARRAY:
		return "array";
	case JSON_T_OBJECT:
		return "object";
	} /* switch() */
} /* json_strtype() */


JSON_PUBLIC enum json_type json_itype(const char *type) {
	static const char name[][8] =  {
		[JSON_T_NULL]    = "null",
		[JSON_T_BOOLEAN] = "boolean",
		[JSON_T_NUMBER]  = "number",
		[JSON_T_STRING]  = "string",
		[JSON_T_ARRAY]   = "array",
		[JSON_T_OBJECT]  = "object",
	};
	size_t i;

	if (type && *type) {
		for (i = 0; i < json_countof(name); i++) {
			if (!strcmp(type, name[i]))
				return (enum json_type)i;
		}
	}

	return JSON_T_NULL;
} /* json_itype() */


JSON_PUBLIC const char *json_strerror(int error) {
	static const char *descr[] = {
		[JSON_EASSERT-JSON_EBASE]    = "JSON assertion",
		[JSON_ELEXICAL-JSON_EBASE]   = "JSON lexical error",
		[JSON_ESYNTAX-JSON_EBASE]    = "JSON syntax error",
		[JSON_ETRUNCATED-JSON_EBASE] = "JSON truncated input",
		[JSON_ENOMORE-JSON_EBASE]    = "JSON no more input needed",
		[JSON_ETYPING-JSON_EBASE]    = "JSON illegal operation on type",
		[JSON_EBADPATH-JSON_EBASE]   = "JSON malformed path",
		[JSON_EBIGPATH-JSON_EBASE]   = "JSON path too long",
	};

	if (error >= JSON_EBASE && error < JSON_ELAST)
		return descr[error - JSON_EBASE];
	else
		return strerror(error);
} /* json_strerror() */


/*
 * V E R S I O N  R O U T I N E S
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

JSON_PUBLIC const char *json_vendor(void) {
	return JSON_VENDOR;
} /* json_vendor() */


JSON_PUBLIC int json_version(void) {
	return JSON_VERSION;
} /* json_version() */


JSON_PUBLIC int json_v_rel(void) {
	return JSON_V_REL;
} /* json_v_rel() */


JSON_PUBLIC int json_v_abi(void) {
	return JSON_V_ABI;
} /* json_v_abi() */


JSON_PUBLIC int json_v_api(void) {
	return JSON_V_API;
} /* json_v_api() */


/*
 * O B S T A C K  R O U T I N E S
 *
 * Like the GNU thing, but simpler and easier. The principle purpose of
 * using a special-purpose allocator is deallocation efficiency, not to
 * outdo the system allocator.
 *
 * NOTES:
 *
 * 	o `Block' refers to the memory region allocated by the system.
 *
 * 	o `Page' refers to the data structure and associated memory region
 * 	  used for chunking allocation requests.
 *
 * 	o `Page size' refers to the size of alloctable region of the Page
 * 	  used to satisfy allocation requests.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#define JSON_MINALIGN 16
#define JSON_MINFUDGE (16UL) /* system allocator overhead */
#define JSON_MINBLOCK (4096UL - JSON_MINFUDGE)
#define JSON_MAXBLOCK (SIZE_MAX >> 2UL) /* to simplify overflow detection */


struct json_obspage {
	struct json_obspage *next;
	size_t size;
	unsigned char JSON_ALIGNAS(JSON_MINALIGN) data[1];
}; /* struct json_obspage */


struct json_obstack {
	int refs;
	struct json_obspage *page;
	unsigned char *tp, *p, *pe;
}; /* struct json_obstack */


static inline size_t json_power2(size_t n) {
#if defined SIZE_MAX
	--n;

	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
#if SIZE_MAX > 0xffffULL
	n |= n >> 16;
#if SIZE_MAX > 0xffffffffULL
	n |= n >> 32;
#if SIZE_MAX > 0xffffffffffffffffULL
#error SIZE_MAX too big
#endif
#endif
#endif
	return ++n;
#else
#error SIZE_MAX not defined
#endif
} /* json_power2() */


static inline int json_minblock(size_t *size, size_t n) {
	if (n > JSON_MAXBLOCK)
		return ENOMEM;
	else if (n <= JSON_MINBLOCK)
		return *size = JSON_MINBLOCK;
	else
		*size = json_power2(n + JSON_MINFUDGE) - JSON_MINFUDGE;

	return 0;
} /* json_minblock() */


static struct json_obstack *json_obsopen(int *error) {
	struct json_obspage *page;
	struct json_obstack *obs;
	size_t size;

	if ((*error = json_safeadd(&size, offsetof(struct json_obspage, data), sizeof *obs)))
		return NULL;

	if ((*error = json_minblock(&size, size)))
		return NULL;

	if (!(page = malloc(size))) {
		*error = errno;
		return NULL;
	}

	page->next = NULL;
	page->size = size - offsetof(struct json_obspage, data);

	obs = (struct json_obstack *)page->data;
	obs->refs = 1;
	obs->page = page;
	obs->tp = &page->data[sizeof *obs];
	obs->p = obs->tp;
	obs->pe = &page->data[page->size];

	return obs;
} /* json_obsopen() */


static void json_obsclose(struct json_obstack *obs) {
	struct json_obspage *page, *next;

	if (!obs || --obs->refs > 0)
		return;

	for (page = obs->page; page; page = next) {
		next = page->next;
		free(page);
	}
} /* json_obsclose() */


static void json_obsacquire(struct json_obstack *obs) {
	++obs->refs;
} /* json_obsacquire() */


static void json_obsalign(struct json_obstack *obs) {
	uintptr_t off, diff;

	if ((off = (uintptr_t)obs->p & JSON_MINALIGN)) {
		diff = JSON_MINALIGN - off;

		if (diff < (uintptr_t)(obs->pe - obs->p))
			obs->p += diff;
		else
			obs->p = obs->pe;
	}

	obs->tp = obs->p;
} /* json_obsalign() */


static int json_obsgrow(struct json_obstack *obs, size_t n) {
	struct json_obspage *page;
	size_t size, p;
	int error;

	if ((size_t)(obs->pe - obs->p) >= n)
		return 0;

	if ((error = json_safeadd(&size, offsetof(struct json_obspage, data), obs->p - obs->page->data)))
		return error;

	if ((error = json_safeadd(&size, size, n)))
		return error;

	if ((error = json_minblock(&size, size)))
		return error;

	p = (size_t)(obs->p - obs->tp);

	/*
	 * resize the page if no other objects were allocated
	 */
	if (obs->tp == obs->page->data) {
		if (!(page = realloc(obs->page, size)))
			return errno;

		page->size = size - offsetof(struct json_obspage, data);
	} else {
		if (!(page = malloc(size)))
			return errno;

		page->next = obs->page;
		page->size = size - offsetof(struct json_obspage, data);
		memcpy(page->data, obs->tp, obs->p - obs->tp);
	}

	obs->page = page;
	obs->tp = page->data;
	obs->p = obs->tp + p;
	obs->pe = &page->data[page->size];

	return 0;
} /* json_obsgrow() */


static int json_obsnew(struct json_obstack *obs, size_t n) {
	json_obsalign(obs);

	return json_obsgrow(obs, n);
} /* json_obsnew() */


static void json_obsundo(struct json_obstack *obs) {
	obs->p = obs->tp;
} /* json_obsundo() */


static void *json_obstop(struct json_obstack *obs) {
	return obs->tp;
} /* json_obstop() */


static size_t json_obslen(struct json_obstack *obs) {
	return obs->p - obs->tp;
} /* json_obslen() */


static int json_obsputc(struct json_obstack *obs, int ch) {
	int error;

	if (!(obs->p < obs->pe) && (error = json_obsgrow(obs, 1)))
		return error;

	*obs->p++ = ch;

	return 0;
} /* json_obsputc() */


static int json_obscat(struct json_obstack *obs, const void *src, size_t len) {
	int error;

	if ((error = json_obsgrow(obs, len)))
		return error;

	memcpy(obs->p, src, len);
	obs->p += len;

	return 0;
} /* json_obscat() */


static void *json_obsget(struct json_obstack *obs, size_t n, int *error) {
	if ((*error = json_obsnew(obs, n)))
		return NULL;

	obs->p += n;

	return obs->tp;
} /* json_obsget() */


static void *json_obsdup(struct json_obstack *obs, const void *src, size_t len, int *error) {
	void *dst;

	if ((dst = json_obsget(obs, len, error)))
		memcpy(dst, src, len);

	return dst;
} /* json_obsdup() */


/*
 * S T R I N G  C O L L E C T I O N  R O U T I N E S
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

struct json_string {
	LLRB_ENTRY(json_string) rbe;
	size_t size;
	char *text;
}; /* struct json_string */


struct json_strings {
	struct json_obstack *obstack;
	LLRB_HEAD(json_cache, json_string) tree;
}; /* struct json_strings */


static inline int json_strcmp(struct json_string *a, struct json_string *b) {
	int cmp;

	if ((cmp = strncmp(a->text, b->text, JSON_MIN(a->size, b->size))))
		return cmp;

	return JSON_CMP(a->size, b->size);
} /* json_strcmp() */

LLRB_GENERATE_STATIC(json_cache, json_string, rbe, json_strcmp)


static struct json_strings *json_strsopen(struct json_obstack *obs, int *error) {
	struct json_strings *strs;

	if (obs) {
		json_obsacquire(obs);
	} else if (!(obs = json_obsopen(error))) {
		goto error;
	}

	if (!(strs = json_obsget(obs, sizeof *strs, error)))
		goto error;

	strs->obstack = obs;

	LLRB_INIT(&strs->tree);

	return strs;
error:
	json_obsclose(obs);

	return NULL;
} /* json_strsopen() */


static void json_strsclose(struct json_strings *strs) {
	json_obsclose(strs->obstack);
} /* json_strsclose() */


static int json_strnew(struct json_strings *strs) {
	int error;

	if (!json_obsget(strs->obstack, sizeof (struct json_string), &error))
		return error;

	return 0;
} /* json_strnew() */


static int json_strcat(struct json_strings *strs, const void *src, size_t len) {
	return json_obscat(strs->obstack, src, len);
} /* json_strcat() */


static int json_strputc(struct json_strings *strs, int ch) {
	return json_obsputc(strs->obstack, ch);
} /* json_strputc() */


static struct json_string *json_strend(struct json_strings *strs, int *error) {
	struct json_string *str, *old;

	if ((*error = json_strputc(strs, '\0')))
		return NULL;

	str = (struct json_string *)json_obstop(strs->obstack);
	str->size = json_obslen(strs->obstack) - sizeof *str - 1;
	str->text = (char *)str + sizeof *str;

	if (!(old = LLRB_INSERT(json_cache, &strs->tree, str))) {
		json_obsundo(strs->obstack);

		return old;
	} else {
		return str;
	}
} /* json_strend() */


/*
 * S T R I N G  R O U T I N E S
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

struct string {
	size_t limit;
	size_t length;
	char *text;
}; /* struct string */

static const struct string nullstring = { 0, 0, "" };

#define string_fake(text, len) (&(struct string){ 0, (len), (char *)(text) })


static void string_init(struct string **S) {
	*S = (struct string *)&nullstring;
} /* string_init() */


static void string_destroy(struct string **S) {
	if (*S != &nullstring) {
		free(*S);
		string_init(S);
	}
} /* string_destroy() */


static void string_reset(struct string **S) {
	if (!*S)
		string_init(S);
	else if (*S != &nullstring)
		(*S)->length = 0;
} /* string_reset() */


static void string_move(struct string **dst, struct string **src) {
	*dst = *src;
	*src = (struct string *)&nullstring;
} /* string_move() */


static int string_grow(struct string **S, size_t len) {
	size_t size, need, limit;
	struct string *tmp;

	if ((*S)->limit - (*S)->length > len)
		return 0;

	if (~len < (*S)->length + 1)
		return ENOMEM;

	limit = (*S)->length + 1 + len;

	if (~sizeof **S < limit)
		return ENOMEM;

	need = sizeof **S + limit;
	size = sizeof **S + (*S)->limit;

	while (size < need) {
		if (~size < size)
			return ENOMEM;
		size *= 2;
	}

	if (*S == &nullstring) {
		if (!(tmp = malloc(size)))
			return errno;
		memset(tmp, 0, size);
	} else if (!(tmp = realloc(*S, size)))
		return errno;

	*S = tmp;
	(*S)->limit = size - sizeof **S;
	(*S)->text = (char *)*S + sizeof **S;
	memset(&(*S)->text[(*S)->length], 0, (*S)->limit - (*S)->length);

	return 0;
} /* string_grow() */


static int string_cats(struct string **S, const void *src, size_t len) {
	int error;

	if ((error = string_grow(S, len)))
		return error;

	memcpy(&(*S)->text[(*S)->length], src, len);
	(*S)->length += len;

	return 0;
} /* string_cats() */


static int string_putc(struct string **S, int ch) {
	int error;

	if ((error = string_grow(S, 1)))
		return error;

	(*S)->text[(*S)->length++] = ch;

	return 0;
} /* string_putc() */


static int string_putw(struct string **S, int ch) {
	char seq[4];
	int len;

	if (ch < 0x80)
		return string_putc(S, ch);

	if (ch < 0x800) {
		seq[0] = 0xc0 | (0x1f & (ch >> 6));
		seq[1] = 0x80 | (0x3f & ch);
		len = 2;
	} else if (ch < 0x10000) {
		seq[0] = 0xe0 | (0x0f & (ch >> 12));
		seq[1] = 0x80 | (0x3f & (ch >> 6));
		seq[2] = 0x80 | (0x3f & ch);
		len = 3;
	} else {
		seq[0] = 0xf0 | (0x07 & (ch >> 18));
		seq[1] = 0x80 | (0x3f & (ch >> 12));
		seq[2] = 0x80 | (0x3f & (ch >> 6));
		seq[3] = 0x80 | (0x3f & ch);
		len = 4;
	}

	return string_cats(S, seq, len);
} /* string_putw() */


/*
 * L E X E R  R O U T I N E S
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

struct token {
	enum tokens {
		T_BEGIN_ARRAY,
		T_END_ARRAY,
		T_BEGIN_OBJECT,
		T_END_OBJECT,
		T_NAME_SEPARATOR,
		T_VALUE_SEPARATOR,
		T_STRING,
		T_NUMBER,
		T_BOOLEAN,
		T_NULL,
	} type;

	CIRCLEQ_ENTRY(token) cqe;

	union {
		struct string *string;
		double number;
		_Bool boolean;
	};
}; /* struct token */


static const enum json_type lex_typemap[] = {
	[T_BEGIN_ARRAY]     = JSON_T_ARRAY,
	[T_END_ARRAY]       = JSON_T_NULL,
	[T_BEGIN_OBJECT]    = JSON_T_OBJECT,
	[T_END_OBJECT]      = JSON_T_NULL,
	[T_NAME_SEPARATOR]  = JSON_T_NULL ,
	[T_VALUE_SEPARATOR] = JSON_T_NULL,
	[T_STRING]          = JSON_T_STRING,
	[T_NUMBER]          = JSON_T_NUMBER,
	[T_BOOLEAN]         = JSON_T_BOOLEAN,
	[T_NULL]            = JSON_T_NULL,
}; /* lex_typemap[] */


JSON_NOTUSED static const char *lex_strtype(enum tokens type) {
	static const char name[][16] = {
		[T_BEGIN_ARRAY]     = "begin-array",
		[T_END_ARRAY]       = "end-array",
		[T_BEGIN_OBJECT]    = "begin-object",
		[T_END_OBJECT]      = "end-object",
		[T_NAME_SEPARATOR]  = "name-separator",
		[T_VALUE_SEPARATOR] = "value-separator",
		[T_STRING]          = "string",
		[T_NUMBER]          = "number",
		[T_BOOLEAN]         = "boolean",
		[T_NULL]            = "null",
	};

	return name[type];
} /* lex_strtype() */


struct lexer {
	void *state;

	struct {
		unsigned pos;
		unsigned row;
		unsigned col;
	} cursor;

	CIRCLEQ_HEAD(, token) tokens;

	struct token *token;

	struct string *string;
	char *sp, *pe;

	int i, code, high;

	char number[64];

	int error;
}; /* struct lexer */


static void lex_init(struct lexer *L) {
	memset(L, 0, sizeof *L);

	L->cursor.row = 1;

	CIRCLEQ_INIT(&L->tokens);

	string_init(&L->string);
} /* lex_init() */


static void tok_free(struct token *T) {
	if (T->type == T_STRING)
		string_destroy(&T->string);

	free(T);	
} /* tok_free() */


static void lex_destroy(struct lexer *L) {
	struct token *T;

	while (!CIRCLEQ_EMPTY(&L->tokens)) {
		T = CIRCLEQ_FIRST(&L->tokens);
		CIRCLEQ_REMOVE(&L->tokens, T, cqe);

		tok_free(T);
	} /* while (tokens) */

	string_destroy(&L->string);
} /* lex_destroy() */


static int lex_push(struct lexer *L, enum tokens type, ...) {
	struct token *T;
	va_list ap;
	int error;

	if (!(T = json_make0(sizeof *T, &error)))
		return error;

	T->type = type;

	switch (type) {
	case T_STRING:
		va_start(ap, type);
		T->string = va_arg(ap, struct string *);
		va_end(ap);
		break;
	case T_NUMBER:
		va_start(ap, type);
		T->number = va_arg(ap, double);
		va_end(ap);
		break;
	case T_BOOLEAN:
		va_start(ap, type);
		T->boolean = va_arg(ap, int);
		va_end(ap);
		break;
	default:
		break;
	} /* switch() */

	CIRCLEQ_INSERT_TAIL(&L->tokens, T, cqe);
	L->token = T;

	return 0;
} /* lex_push() */


static void lex_newnum(struct lexer *L) {
	L->sp = L->number;
	L->pe = &L->number[sizeof L->number - 1];
} /* lex_newnum() */


static inline _Bool lex_isnum(int ch) {
	return json_isnumber(ch);
} /* lex_isnum() */


static int lex_catnum(struct lexer *L, int ch) {
	if (L->sp < L->pe) {
		*L->sp++ = ch;

		return 0;
	} else
		return EOVERFLOW;
} /* lex_catnum() */


static int lex_pushnum(struct lexer *L) {
	double number;
	char *end;

	*L->sp = '\0';

	number = strtod(L->number, &end);

	if (number == 0) {
		if (end == L->number)
			return JSON_ELEXICAL;
		if (errno == ERANGE)
			return ERANGE;
	} else if (number == HUGE_VAL && errno == ERANGE) {
		return ERANGE;
	} else if (*end != '\0')
		return JSON_ELEXICAL;

	return lex_push(L, T_NUMBER, number);
} /* lex_pushnum() */


static inline _Bool lex_ishigh(int code) {
	return (code >= 0xD800 && code <= 0xDBFF);
} /* lex_ishigh() */

static inline _Bool lex_islow(int code) {
	return (code >= 0xDC00 && code <= 0xDFFF);
} /* lex_islow() */

static int lex_frompair(int hi, int lo) {
	return (hi << 10) + lo + (0x10000 - (0xD800 << 10) - 0xDC00);
} /* lex_frompair() */


#define resume() do { \
	if (L->state) \
		goto *L->state; \
} while (0)

#define popchar() do { \
	JSON_XPASTE(L, __LINE__): \
	if (p >= pe) { L->state = &&JSON_XPASTE(L, __LINE__); return 0; } \
	ch = *p++; \
	L->cursor.pos++; \
	L->cursor.col++; \
} while (0)

#define ungetchar() do { \
	p--; \
	L->cursor.pos--; \
	L->cursor.col--; \
} while (0)

#define pushtoken(...) do { \
	if ((error = lex_push(L, __VA_ARGS__))) \
		goto error; \
} while (0)

#define expect(c) do { popchar(); if (ch != (c)) goto invalid; } while (0)

static int lex_parse(struct lexer *L, const void *src, size_t len) {
	const unsigned char *p = src, *pe = p + len;
	int ch, error;

	resume();
start:
	popchar();

	switch (ch) {
	case ' ': case '\t': case '\r':
		break;
	case '\n':
		L->cursor.row++;
		L->cursor.col = 0;

		break;
	case '[':
		pushtoken(T_BEGIN_ARRAY);
		break;
	case ']':
		pushtoken(T_END_ARRAY);
		break;
	case '{':
		pushtoken(T_BEGIN_OBJECT);
		break;
	case '}':
		pushtoken(T_END_OBJECT);
		break;
	case ':':
		pushtoken(T_NAME_SEPARATOR);
		break;
	case ',':
		pushtoken(T_VALUE_SEPARATOR);
		break;
	case '"':
		goto string;
	case '+': case '-': case '.':
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		ungetchar();

		goto number;
	case 'n':
		goto null;
	case 't':
		goto btrue;
	case 'f':
		goto bfalse;
	default:
		goto invalid;
	} /* switch (ch) */

	goto start;
string:
	string_reset(&L->string);

	for (;;) {
		popchar();

		switch (ch) {
		case '"':
			goto endstr;
		case '\\':
			popchar();

			switch (ch) {
			case '"':
			case '/':
			case '\\':
				goto catstr;
			case 'b':
				ch = '\b';
				goto catstr;
			case 'f':
				ch = '\f';
				goto catstr;
			case 'n':
				ch = '\n';
				goto catstr;
			case 'r':
				ch = '\r';
				goto catstr;
			case 't':
				ch = '\t';
				goto catstr;
			case 'u':
				L->i = 0;
				L->code = 0;

				while (L->i++ < 4) {
					popchar();

					if (json_isdigit(ch)) {
						L->code <<= 4;
						L->code += ch - '0';
					} else if (ch >= 'A' && ch <= 'F') {
						L->code <<= 4;
						L->code += 10 + (ch - 'A');
					} else if (ch >= 'a' && ch <= 'f') {
						L->code <<= 4;
						L->code += 10 + (ch - 'a');
					} else
						goto invalid;
				} /* while() */

				if (lex_ishigh(L->code)) {
					L->high = L->code;

					break;
				} else if (lex_islow(L->code)) {
					L->code = lex_frompair(L->high, L->code);
					L->high = 0;
				}

				if ((error = string_putw(&L->string, L->code)))
					goto error;

				break;
			default:
				goto invalid;
			} /* switch() */

			break;
		default:
catstr:
			if ((error = string_putc(&L->string, ch)))
				goto error;

			break;
		} /* switch() */
	} /* for() */
endstr:
	pushtoken(T_STRING, L->string);
	string_init(&L->string);

	goto start;
number:
	lex_newnum(L);

	popchar();

	while (lex_isnum(ch)) {
		if ((error = lex_catnum(L, ch)))
			goto error;

		popchar();
	}

	ungetchar();

	if ((error = lex_pushnum(L)))
		goto error;

	goto start;
null:
	expect('u');
	expect('l');
	expect('l');

	pushtoken(T_NULL);

	goto start;
btrue:
	expect('r');
	expect('u');
	expect('e');

	pushtoken(T_BOOLEAN, 1);

	goto start;
bfalse:
	expect('a');
	expect('l');
	expect('s');
	expect('e');

	pushtoken(T_BOOLEAN, 0);

	goto start;
invalid:
	if (json_isgraph(ch))
		fprintf(stderr, "invalid char (%c) at line %u, column %u\n", ch, L->cursor.row, L->cursor.col);
	else
		fprintf(stderr, "invalid char (0x%.2x) at line %u, column %u\n", ch, L->cursor.row, L->cursor.col);

	error = JSON_ELEXICAL;

	goto error;
error:
	L->error = error;
	L->state = &&failed;
failed:
	return L->error;
} /* lex_parse() */


/*
 * V A L U E  R O U T I N E S
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

struct node {
	union {
		struct json_value *key;
		json_index_t index;
	};

	struct json_value *value;
	struct json_value *parent;

	union {
		LLRB_ENTRY(node) rbe;
		CIRCLEQ_ENTRY(node) cqe;
	};
}; /* struct node */


struct json_value {
	enum json_type type;

	struct node *node;

	union { /* mutually exclusive usage */ 
		struct json_value *root;
		void *state;
	};

	union {
		struct {
			LLRB_HEAD(array, node) nodes;
			json_index_t count;
		} array;

		struct {
			LLRB_HEAD(object, node) nodes;
			json_index_t count;
		} object;

		struct string *string;

		double number;

		_Bool boolean;
	};
}; /* struct json_value */


static int array_cmp(struct node *a, struct node *b) {
	return JSON_CMP(a->index, b->index);
} /* array_cmp() */

LLRB_GENERATE_STATIC(array, node, rbe, array_cmp)


/* NOTE: All keys must be strings per RFC 4627. */
static int object_cmp(struct node *a, struct node *b) {
	int cmp;

	if ((cmp = strncmp(a->key->string->text, b->key->string->text, JSON_MIN(a->key->string->length, b->key->string->length))))
		return cmp;

	return JSON_CMP(a->key->string->length, b->key->string->length);
} /* object_cmp() */

LLRB_GENERATE_STATIC(object, node, rbe, object_cmp)


static int value_init(struct json_value *V, enum json_type type, struct token *T) {
	memset(V, 0, sizeof *V);

	V->type = type;

	switch (type) {
	case JSON_T_STRING:
		if (T) {
			string_move(&V->string, &T->string);
		} else {
			string_init(&V->string);
		}
		break;
	case JSON_T_NUMBER:
		V->number = (T)? T->number : 0.0;
		break;
	case JSON_T_BOOLEAN:
		V->boolean = (T)? T->boolean : 0;
		break;
	default:
		break;
	} /* switch() */

	return 0;
} /* value_init() */


static struct json_value *value_open(enum json_type type, struct token *T, int *error) {
	struct json_value *V;

	if (!(V = json_make(sizeof *V, error))
	||  (*error = value_init(V, type, T)))
		return 0;

	return V;
} /* value_open() */


static void value_close(struct json_value *, _Bool);

static int array_push(struct json_value *A, struct json_value *V) {
	struct node *N;
	int error;

	if (!(N = json_make(sizeof *N, &error)))
		return error;

	N->index = A->array.count++;
	N->value = V;
	N->parent = A;
	V->node = N;
	V->root = NULL;

	LLRB_INSERT(array, &A->array.nodes, N);

	return 0;
} /* array_push() */


static int array_insert(struct json_value *A, int index, struct json_value *V) {
	struct node *N, *prev;
	int error;

	if (index < 0)
		index = A->array.count - index;

	if (index < 0 || index >= A->array.count)
		return array_push(A, V);

	if (!(N = json_make(sizeof *N, &error)))
		return error;

	N->index = index;
	N->value = V;
	N->parent = A;
	V->node = N;
	V->root = NULL;

	if (!(prev = LLRB_INSERT(array, &A->array.nodes, N)))
		return 0;

	free(N);

	value_close(prev->value, 0);

	prev->value = V;
	V->node = prev;

	return 0;
} /* array_insert() */


static struct json_value *array_index(struct json_value *A, int index) {
	struct node node, *result;

	if (index < 0) {
		index = A->array.count - index;

		if (index < 0)
			return NULL;
	}

	node.index = index;

	if ((result = LLRB_FIND(array, &A->array.nodes, &node)))
		return result->value;

	return NULL;
} /* array_index() */


static int object_insert(struct json_value *O, struct json_value *K, struct json_value *V) {
	struct node *N, *prev;
	int error;

	if (!(N = json_make(sizeof *N, &error)))
		return error;

	N->key = K;
	N->value = V;
	N->parent = O;
	K->node = N;
	V->node = N;
	V->root = NULL;

	O->object.count++;

	if (!(prev = LLRB_INSERT(object, &O->object.nodes, N)))
		return 0;

	free(N);

	value_close(prev->key, 0);
	value_close(prev->value, 0);

	prev->key = K;
	prev->value = V;
	K->node = prev;
	V->node = prev;

	return 0;
} /* object_insert() */


static struct json_value *object_search(struct json_value *O, const void *name, size_t len) {
	struct json_value key;
	struct node node, *result;

	key.type = JSON_T_STRING;
	key.string = string_fake(name, len);
	node.key = &key;

	if ((result = LLRB_FIND(object, &O->object.nodes, &node)))
		return result->value;

	return NULL;
} /* object_search() */


CIRCLEQ_HEAD(orphans, node);

static void array_remove(struct json_value *V, struct node *N, struct orphans *indices) {
	LLRB_REMOVE(array, &V->array.nodes, N);
	N->parent = 0;

	CIRCLEQ_INSERT_TAIL(indices, N, cqe);
} /* array_remove() */


static void array_clear(struct json_value *V, struct orphans *indices) {
	struct node *N, *nxt;

	for (N = LLRB_MIN(array, &V->array.nodes); N; N = nxt) {
		nxt = LLRB_NEXT(array, &V->array.nodes, N);

		array_remove(V, N, indices);
	}
} /* array_clear() */


static void object_remove(struct json_value *V, struct node *N, struct orphans *keys) {
	LLRB_REMOVE(object, &V->object.nodes, N);
	N->parent = 0;

	V->object.count--;

	CIRCLEQ_INSERT_TAIL(keys, N, cqe);
} /* object_remove() */


static void object_clear(struct json_value *V, struct orphans *keys) {
	struct node *N, *nxt;

	for (N = LLRB_MIN(object, &V->object.nodes); N; N = nxt) {
		nxt = LLRB_NEXT(object, &V->array.nodes, N);

		object_remove(V, N, keys);
	}
} /* object_clear() */


static void value_clear(struct json_value *V, struct orphans *indices, struct orphans *keys) {
	switch (V->type) {
	case JSON_T_ARRAY:
		array_clear(V, indices);
		break;
	case JSON_T_OBJECT:
		object_clear(V, keys);
		break;
	case JSON_T_STRING:
		string_reset(&V->string);
		break;
	case JSON_T_BOOLEAN:
		V->boolean = 0;
		break;
	case JSON_T_NUMBER:
		V->number = 0.0;
		break;
	default:
		break;
	} /* switch() */
} /* value_clear() */


static void node_remove(struct node *N, struct orphans *indices, struct orphans *keys) {
	if (N->parent->type == JSON_T_ARRAY)
		array_remove(N->parent, N, indices);
	else
		object_remove(N->parent, N, keys);
} /* node_remove() */


static void value_destroy(struct json_value *V, struct orphans *indices, struct orphans *keys) {
	value_clear(V, indices, keys);

	if (V->type == JSON_T_STRING)
		string_destroy(&V->string);
} /* value_destroy() */


static void orphans_free(struct orphans *indices, struct orphans *keys) {
	struct node *N;

	do {
		while (!CIRCLEQ_EMPTY(indices)) {
			N = CIRCLEQ_FIRST(indices);
			CIRCLEQ_REMOVE(indices, N, cqe);

			value_destroy(N->value, indices, keys);
			free(N->value);
			free(N);
		}

		while (!CIRCLEQ_EMPTY(keys)) {
			N = CIRCLEQ_FIRST(keys);
			CIRCLEQ_REMOVE(keys, N, cqe);

			value_destroy(N->key, indices, keys);
			free(N->key);

			value_destroy(N->value, indices, keys);
			free(N->value);

			free(N);
		}
	} while (!CIRCLEQ_EMPTY(indices) || !CIRCLEQ_EMPTY(keys));
} /* orphans_free() */


static void value_close(struct json_value *V, _Bool node) {
	struct orphans indices, keys;

	if (!V)
		return;

	CIRCLEQ_INIT(&indices);
	CIRCLEQ_INIT(&keys);

	value_destroy(V, &indices, &keys);

	if (V->node && node) {
		node_remove(V->node, &indices, &keys);
	} else {
		free(V);
	}

	orphans_free(&indices, &keys);
} /* value_close() */


static _Bool value_issimple(struct json_value *V) {
	return V->type != JSON_T_ARRAY && V->type != JSON_T_OBJECT;
} /* value_issimple() */


static _Bool value_iskey(struct json_value *V) {
	return (V->node && V->node->parent->type == JSON_T_OBJECT && V->node->key == V);
} /* value_iskey() */


static _Bool value_isvalue(struct json_value *V) {
	return (V->node && V->node->parent->type == JSON_T_OBJECT && V->node->value == V);
} /* value_isvalue() */


static int value_convert(struct json_value *V, enum json_type type) {
	struct orphans indices, keys;
	struct json_value *R;
	struct node *N;
	int error;

	if (V->type == type)
		return 0;

	if (value_iskey(V))
		return JSON_EASSERT;

	R = V->root;
	N = V->node;

	CIRCLEQ_INIT(&indices);
	CIRCLEQ_INIT(&keys);

	value_destroy(V, &indices, &keys);
	orphans_free(&indices, &keys);

	error = value_init(V, type, NULL);

	V->node = N;
	V->root = R;

	return error;
} /* value_convert() */


static double value_number(struct json_value *V) {
	return (V && V->type == JSON_T_NUMBER)? V->number : 0.0;
} /* value_number() */


static const char *value_string(struct json_value *V) {
	return (V && V->type == JSON_T_STRING)? V->string->text : "";
} /* value_string() */


static double value_length(struct json_value *V) {
	return (V && V->type == JSON_T_STRING)? V->string->length : 0;
} /* value_length() */


static double value_count(struct json_value *V) {
	switch ((V)? V->type : JSON_T_NULL) {
	case JSON_T_ARRAY:
		return V->array.count;
	case JSON_T_OBJECT:
		return V->object.count;
	default:
		return 0;
	}
} /* value_count() */


static _Bool value_boolean(struct json_value *V) {
	if (!V)
		return 0;

	if (V->type == JSON_T_BOOLEAN)
		return V->boolean;

	switch (V->type) {
	case JSON_T_NUMBER:
		return isnormal(V->number);
	case JSON_T_ARRAY:
		return !!V->array.count;
	case JSON_T_OBJECT:
		return !!V->object.count;
	default:
		return 0;
	} /* switch() */
} /* value_boolean() */


#if 0
static struct json_value *value_search(struct json_value *J, const void *name, size_t len) {
	struct json_value key;

	switch (J->type) {
	case JSON_V_OBJECT:
		key.type = JSON_V_STRING;
		key.string = string_fake(name, len);

		return 
	} else


	return NULL;
} /* value_search() */
#endif


static struct json_value *value_parent(struct json_value *V) {
	return (V->node)? V->node->parent : NULL;
} /* value_parent() */


static struct json_value *value_root(struct json_value *top) {
	struct json_value *nxt;

	while (top && (nxt = value_parent(top)))
		top = nxt;

	return top;
} /* value_root() */


static struct json_value *value_descend(struct json_value *V) {
	struct node *N;

	if (V->type == JSON_T_ARRAY) {
		if ((N = LLRB_MIN(array, &V->array.nodes)))
			return N->value;
	} else if (V->type == JSON_T_OBJECT) {
		if ((N = LLRB_MIN(object, &V->object.nodes)))
			return N->key;
	}

	return 0;
} /* value_descend() */


static struct node *node_next(struct node *N) {
	if (N->parent->type == JSON_T_ARRAY)
		return LLRB_NEXT(array, &N->parent->array.nodes, N);
	else
		return LLRB_NEXT(object, &N->parent->object.nodes, N);
} /* node_next() */


static struct json_value *value_adjacent(struct json_value *V) {
	struct node *N;

	if (V->node && (N = node_next(V->node))) {
		if (N->parent->type == JSON_T_ARRAY)
			return N->value;
		else
			return N->key;
	}

	return 0;
} /* value_adjacent() */


#define ORDER_PRE  JSON_I_PREORDER
#define ORDER_POST JSON_I_POSTORDER

static struct json_value *value_next(struct json_value *V, int *order, int *depth) {
	struct json_value *nxt;

	if (!*order) {
		*order = ORDER_PRE;
		*depth = 0;

		return V;
	} else if ((*order & ORDER_PRE) && (V->type == JSON_T_ARRAY || V->type == JSON_T_OBJECT)) {
		if ((nxt = value_descend(V))) {
			++*depth;
			return nxt;
		}

		*order = ORDER_POST;

		return V;
	} else if (!V->node) {
		return NULL;
	} else if (value_iskey(V)) {
		*order = ORDER_PRE;

		return V->node->value;
	} else if (*depth > 0 && (nxt = value_adjacent(V))) {
		*order = ORDER_PRE;

		return nxt;
	} else if (*depth > 0) {
		*order = ORDER_POST;
		--*depth;

		return V->node->parent;
	} else {
		return NULL;
	}
} /* value_next() */


/*
 * P R I N T E R  R O U T I N E S
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

struct printer {
	int flags, state, sstate, order, error;
	struct json_value *value;

	int i, depth;

	char literal[64];

	struct {
		char *p, *pe;
	} buffer;
}; /* struct printer */


static struct json_value *print_root(struct json_value *root, int flags) {
	return (flags & JSON_F_PARTIAL)? root : value_root(root);
} /* print_root() */


static void print_init(struct printer *P, struct json_value *V, int flags) {
	memset(P, 0, sizeof *P);
	P->flags = flags;
	P->value = V;
} /* print_init() */


#define RESUME() switch (P->sstate) { case 0: (void)0

#define YIELD() do { \
	P->sstate = __LINE__; \
	return p - (char *)dst; \
	case __LINE__: (void)0; \
} while (0)

#define STOP() do { \
	P->sstate = __LINE__; \
	case __LINE__: return p - (char *)dst; \
} while (0)

#define PUTCHAR(ch) do { \
	while (p >= pe) \
		YIELD(); \
	*p++ = (ch); \
} while (0)

#define END } (void)0

static size_t print_simple(struct printer *P, void *dst, size_t lim, struct json_value *V, int order) {
	char *p = dst, *pe = p + lim;

	RESUME();

	switch (V->type) {
	case JSON_T_ARRAY:
		if (order & ORDER_PRE)
			P->literal[0] = '[';
		else
			P->literal[0] = ']';

		P->buffer.p = P->literal;
		P->buffer.pe = &P->literal[1];

		goto literal;
	case JSON_T_OBJECT:
		if (order & ORDER_PRE)
			P->literal[0] = '{';
		else
			P->literal[0] = '}';

		P->buffer.p = P->literal;
		P->buffer.pe = &P->literal[1];

		goto literal;
	case JSON_T_STRING:
		P->buffer.p = V->string->text;
		P->buffer.pe = &V->string->text[V->string->length];

		goto string;
	case JSON_T_NUMBER: {
		double i;
		int count;

		if (0.0 == modf(V->number, &i))
			count = snprintf(P->literal, sizeof P->literal, "%lld", (long long)i);
		else
			count = snprintf(P->literal, sizeof P->literal, "%f", V->number);

		if (count == -1) {
			P->error = errno;

			goto error;
		} else if ((size_t)count >= sizeof P->literal) {
			P->error = EOVERFLOW;

			goto error;
		}

		P->buffer.p = P->literal;
		P->buffer.pe = &P->literal[count];

		goto literal;
	}
	case JSON_T_BOOLEAN: {
		size_t count = json_strlcpy(P->literal, ((V->boolean)? "true" : "false"), sizeof P->literal);

		P->buffer.p = P->literal;
		P->buffer.pe = &P->literal[count];

		goto literal;
	}
	case JSON_T_NULL: {
		size_t count = json_strlcpy(P->literal, "null", sizeof P->literal);

		P->buffer.p = P->literal;
		P->buffer.pe = &P->literal[count];

		goto literal;
	}
	} /* switch (V->type) */
string:
	PUTCHAR('"');

	while (P->buffer.p < P->buffer.pe) {
		if (json_isgraph(*P->buffer.p)) {
			if (*P->buffer.p == '"' || *P->buffer.p == '/' || *P->buffer.p == '\\')
				PUTCHAR('\\');
			PUTCHAR(*P->buffer.p++);
		} else if (*P->buffer.p == ' ') {
			PUTCHAR(*P->buffer.p++);
		} else if (json_isascii(*P->buffer.p)) {
			PUTCHAR('\\');

			if (*P->buffer.p == '\b')
				PUTCHAR('b');
			else if (*P->buffer.p == '\f')
				PUTCHAR('f');
			else if (*P->buffer.p == '\n')
				PUTCHAR('n');
			else if (*P->buffer.p == '\r')
				PUTCHAR('r');
			else if (*P->buffer.p == '\t')
				PUTCHAR('t');
			else {
				PUTCHAR('u');
				PUTCHAR('0');
				PUTCHAR('0');
				PUTCHAR("0123456789abcdef"[0x0f & (*P->buffer.p >> 4)]);
				PUTCHAR("0123456789abcdef"[0x0f & (*P->buffer.p >> 0)]);
			}

			P->buffer.p++;
		} else {
			PUTCHAR(*P->buffer.p++);
		}
	} /* while() */

	PUTCHAR('"');

	STOP();
literal:
	while (P->buffer.p < P->buffer.pe)
		PUTCHAR(*P->buffer.p++);

	STOP();
error:
	STOP();

	END;

	return 0;
} /* print_simple() */

#undef RESUME
#undef YIELD
#undef PUTCHAR
#undef STOP
#undef END


#define RESUME switch (P->state) { case 0: (void)0

#define YIELD() do { \
	P->state = __LINE__; \
	return p - (char *)dst; \
	case __LINE__: (void)0; \
} while (0)

#define STOP() do { \
	P->state = __LINE__; \
	case __LINE__: return p - (char *)dst; \
} while (0)

#define PUTCHAR_(ch, cond, ...) do { \
	if ((cond)) { \
		while (p >= pe) \
			YIELD(); \
		*p++ = (ch); \
	} \
} while (0)

#define PUTCHAR(...) PUTCHAR_(__VA_ARGS__, 1)

#define END } (void)0

static size_t print(struct printer *P, void *dst, size_t lim) {
	char *p = dst, *pe = p + lim;
	size_t count;

	RESUME;

	while ((P->value = value_next(P->value, &P->order, &P->depth))) {
		if ((!value_isvalue(P->value) || (P->order & ORDER_POST))
		&&  (P->flags & JSON_F_PRETTY)) {
			for (P->i = 0; P->i < P->depth; P->i++)
				PUTCHAR('\t');
		}

		P->sstate = 0;
		count = 0;

		do {
			p += count;

			if (!(p < pe))
				YIELD();
		} while ((count = print_simple(P, p, pe - p, P->value, P->order)));

		if (P->error)
			STOP();

		if (value_iskey(P->value)) {
			PUTCHAR(' ', (P->flags & JSON_F_PRETTY));
			PUTCHAR(':');
			PUTCHAR(' ', (P->flags & JSON_F_PRETTY));
		} else {
			if ((P->order == ORDER_POST || value_issimple(P->value))
			&&  value_adjacent(P->value))
				PUTCHAR(',');
			PUTCHAR('\n', (P->flags & JSON_F_PRETTY));
		}
	}

	STOP();

	END;

	return 0;
} /* print() */

#undef RESUME
#undef YIELD
#undef PUTCHAR
#undef STOP
#undef END


/*
 * P A R S E R  R O U T I N E S
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

struct parser {
	struct lexer lexer;
	CIRCLEQ_HEAD(, token) tokens;
	void *state;
	struct json_value *root;
	struct json_value *key;
	struct json_value *value;
	int error;
}; /* struct parser */


static void parse_init(struct parser *P) {
	memset(P, 0, sizeof *P);
	lex_init(&P->lexer);
	CIRCLEQ_INIT(&P->tokens);
} /* parse_init() */


static void parse_destroy(struct parser *P) {
	struct token *T;
	struct json_value *root;

	lex_destroy(&P->lexer);

	while (!CIRCLEQ_EMPTY(&P->tokens)) {
		T = CIRCLEQ_FIRST(&P->tokens);
		CIRCLEQ_REMOVE(&P->tokens, T, cqe);

		tok_free(T);
	} /* while (tokens) */

	if ((root = value_root(P->root)))
		value_close(root, 1);

	P->root = NULL;
} /* parse_destroy() */


static struct json_value *tovalue(struct token *T, int *error) {
	return value_open(lex_typemap[T->type], T, error);
} /* tovalue() */


#define RESUME() do { \
	T = (CIRCLEQ_EMPTY(&P->tokens))? 0 : CIRCLEQ_LAST(&P->tokens); \
	if (P->state) \
		goto *P->state; \
} while (0)

#define YIELD() do { \
	P->state = &&JSON_XPASTE(L, __LINE__); \
	return EAGAIN; \
	JSON_XPASTE(L, __LINE__): (void)0; \
} while (0)

#define STOP(why) do { \
	P->error = (why); \
	P->state = &&JSON_XPASTE(L, __LINE__); \
	JSON_XPASTE(L, __LINE__): return P->error; \
} while (0)

#define POPTOKEN() do { \
	while (CIRCLEQ_EMPTY(&P->lexer.tokens)) \
		YIELD(); \
	T = CIRCLEQ_FIRST(&P->lexer.tokens); \
	CIRCLEQ_REMOVE(&P->lexer.tokens, T, cqe); \
	CIRCLEQ_INSERT_TAIL(&P->tokens, T, cqe); \
} while (0)

#define POPSTACK() do { \
	void *state = P->root->state; \
	if (P->root->node) \
		P->root = P->root->node->parent; \
	goto *state; \
} while (0)

#define PUSHARRAY(V) do { \
	(V)->state = &&JSON_XPASTE(L, __LINE__); \
	P->root = V; \
	goto array; \
	JSON_XPASTE(L, __LINE__): (void)0; \
} while (0)

#define PUSHOBJECT(V) do { \
	(V)->state = &&JSON_XPASTE(L, __LINE__); \
	P->root = V; \
	goto object; \
	JSON_XPASTE(L, __LINE__): (void)0; \
} while (0)

#define TOVALUE(T) do { \
	if (!(P->value = V = tovalue(T, &error))) \
		STOP(error); \
} while (0)

#define TOKEY(T) do { \
	if (!(P->key = V = tovalue(T, &error))) \
		STOP(error); \
} while (0)

#define INSERT2ARRAY() do { \
	if ((error = array_push(P->root, P->value))) \
		STOP(error); \
	P->value = 0; \
} while (0)

#define INSERT2OBJECT() do { \
	if ((error = object_insert(P->root, P->key, P->value))) \
		STOP(error); \
	P->key = 0; \
	P->value = 0; \
} while (0)

#define LOOP for (;;)

static int parse(struct parser *P, const void *src, size_t len) {
	struct token *T;
	struct json_value *V;
	int error;

	if ((error = lex_parse(&P->lexer, src, len)))
		return error;

	RESUME();

	POPTOKEN();

	switch (T->type) {
	case T_BEGIN_ARRAY:
		if (!(P->root = value_open(JSON_T_ARRAY, T, &error)))
			STOP(error);

		P->root->state = &&stop;

		goto array;
	case T_BEGIN_OBJECT:
		if (!(P->root = value_open(JSON_T_OBJECT, T, &error)))
			STOP(error);

		P->root->state = &&stop;

		goto object;
	default:
		STOP(JSON_ESYNTAX);
	} /* switch() */
array:
	LOOP {
		POPTOKEN();

		switch (T->type) {
		case T_BEGIN_ARRAY:
			TOVALUE(T);

			INSERT2ARRAY();

			PUSHARRAY(V);

			break;
		case T_END_ARRAY:
			POPSTACK();
		case T_BEGIN_OBJECT:
			TOVALUE(T);

			INSERT2ARRAY();

			PUSHOBJECT(V);

			break;
		case T_END_OBJECT:
			STOP(JSON_ESYNTAX);
		case T_VALUE_SEPARATOR:
			break;
		case T_NAME_SEPARATOR:
			STOP(JSON_ESYNTAX);
		default:
			TOVALUE(T);

			INSERT2ARRAY();

			break;
		} /* switch() */
	} /* LOOP */
object:
	LOOP {
		POPTOKEN();

		switch (T->type) {
		case T_END_OBJECT:
			POPSTACK();
		case T_VALUE_SEPARATOR:
			continue;
		case T_STRING:
			break;
		default:
			STOP(JSON_ESYNTAX);
		} /* switch (key) */

		TOKEY(T);

		POPTOKEN();

		if (T->type != T_NAME_SEPARATOR)
			STOP(JSON_ESYNTAX);

		POPTOKEN();

		switch (T->type) {
		case T_BEGIN_ARRAY:
			TOVALUE(T);

			INSERT2OBJECT();

			PUSHARRAY(V);

			break;
		case T_END_ARRAY:
			STOP(JSON_ESYNTAX);
		case T_BEGIN_OBJECT:
			TOVALUE(T);

			INSERT2OBJECT();

			PUSHOBJECT(V);

			break;
		case T_END_OBJECT:
			STOP(JSON_ESYNTAX);
		case T_NAME_SEPARATOR:
			STOP(JSON_ESYNTAX);
		default:
			TOVALUE(T);

			INSERT2OBJECT();

			break;
		} /* switch() */
	} /* LOOP */
stop:
	return 0;
} /* parse() */

#undef RESUME
#undef YIELD
#undef STOP
#undef POPTOKEN
#undef POPSTACK
#undef PUSHARRAY
#undef PUSHOBJECT
#undef TOVALUE
#undef TOKEY
#undef INSERT2ARRAY
#undef INSERT2OBJECT
#undef LOOP


/*
 * J S O N  C O R E  R O U T I N E S
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

struct json {
	int flags, mode;
	struct parser parser;
	struct printer printer;
	jmp_buf *trap;
	struct json_value *root;
}; /* struct json */


JSON_PUBLIC struct json *json_open(int flags, int *error) {
	struct json *J;

	if (!(J = json_make0(sizeof *J, error)))
		return NULL;

	J->flags = flags;
	J->mode = ~((flags & (JSON_F_NOAUTOVIV|JSON_F_NOCONVERT)) >> 4)
	        & (JSON_M_AUTOVIV|JSON_M_CONVERT);

	parse_init(&J->parser);

	J->trap = NULL;
	J->root = NULL;

	return J;
} /* json_open() */


JSON_PUBLIC void json_close(struct json *J) {
	struct json_value *root;

	if (!J)
		return;

	parse_destroy(&J->parser);

	if ((root = value_root(J->root)))
		value_close(root, 1);

	free(J);
} /* json_close() */


JSON_PUBLIC jmp_buf *json_setjmp(struct json *J, jmp_buf *trap) {
	jmp_buf *otrap = J->trap;

	J->trap = trap;

	return otrap;
} /* json_setjmp() */


JSON_PUBLIC int json_throw(struct json *J, int error) {
	if (J->trap)
		_longjmp(*J->trap, error);

	return error;
} /* json_throw() */


JSON_PUBLIC int json_ifthrow(struct json *J, int error) {
	return (error)? json_throw(J, error) : 0;
} /* json_ifthrow() */


JSON_PUBLIC int json_parse(struct json *J, const void *src, size_t len) {
	int error;

	if (J->root)
		return json_throw(J, JSON_ENOMORE);

	if ((error = parse(&J->parser, src, len)))
		return error;

	J->root = J->parser.root;
	J->parser.root = NULL;
	J->root->root = NULL;

	parse_destroy(&J->parser);

	return 0;
} /* json_parse() */


JSON_PUBLIC int json_loadbuffer(struct json *J, const void *src, size_t len) {
	int error;

	if (J->root)
		return json_throw(J, JSON_ENOMORE);

	if ((error = json_parse(J, src, len)))
		return json_throw(J, (error == EAGAIN)? JSON_ETRUNCATED : error);

	return 0;
} /* json_loadbuffer() */


JSON_PUBLIC JSON_DEPRECATED int json_loadlstring(struct json *J, const void *src, size_t len) {
	return json_loadbuffer(J, src, len);
} /* json_loadlstring() */


JSON_PUBLIC int json_loadstring(struct json *J, const char *src) {
	return json_loadbuffer(J, src, json_strlen(src));
} /* json_loadstring() */


JSON_PUBLIC int json_loadfile(struct json *J, FILE *fp) {
	char buffer[512];
	size_t count;
	int error;

	if (J->root)
		return json_throw(J, JSON_ENOMORE);

	clearerr(fp);

	while ((count = fread(buffer, 1, sizeof buffer, fp))) {
		if (!(error = json_parse(J, buffer, count)))
			return 0;
		else if (error != EAGAIN)
			return json_throw(J, error);
	}

	if (ferror(fp))
		return json_throw(J, errno);

	return JSON_ETRUNCATED;
} /* json_loadfile() */


JSON_PUBLIC int json_loadpath(struct json *J, const char *path) {
	struct jsonxs xs;
	FILE *fp = NULL;
	int error;

	if (J->root)
		return json_throw(J, JSON_ENOMORE);

	if ((error = json_enter(J, &xs)))
		goto leave;

	if (!(fp = fopen(path, "r")))
		json_throw(J, errno);

	json_loadfile(J, fp);

leave:
	json_leave(J, &xs);

	if (fp)
		fclose(fp);

	return (error)? json_throw(J, error) : 0;
} /* json_loadpath() */


JSON_PUBLIC size_t json_compose(struct json *J, void *dst, size_t lim, int flags, int *error) {
	struct json_value *root;
	size_t count;

	if (!J->printer.state) {
		if (!(root = print_root(J->root, flags|J->flags)))
			return 0;

		print_init(&J->printer, root, flags|J->flags);
	}

	if ((count = print(&J->printer, dst, lim)))
		return count;

	if (J->printer.error) {
		if (error)
			*error = J->printer.error;

		json_throw(J, J->printer.error);
	}

	if (error)
		*error = 0;

	return 0;
} /* json_compose() */


JSON_PUBLIC void json_rewind(struct json *J) {
	print_init(&J->printer, 0, 0);
} /* json_rewind() */


JSON_PUBLIC JSON_DEPRECATED void json_flush(struct json *J) {
	json_rewind(J);
} /* json_flush() */


JSON_PUBLIC int json_getc(struct json *J, int flags, int *error) {
	char c;

	while (json_compose(J, &c, 1, flags, error))
		return (unsigned char)c;

	return EOF;
} /* json_getc() */


JSON_PUBLIC int json_printfile(struct json *J, FILE *fp, int flags) {
	struct printer P;
	struct json_value *root;
	char buffer[512];
	size_t count;
	int error;

	if (!(root = print_root(J->root, flags|J->flags)))
		return 0;

	print_init(&P, root, flags|J->flags);

	while ((count = print(&P, buffer, sizeof buffer))) {
		if (count != fwrite(buffer, 1, count, fp))
			goto syerr;
	}

	if ((error = P.error))
		goto error;
	else if (0 != fflush(fp))
		goto syerr;

	return 0;
syerr:
	error = errno;
error:
	return json_throw(J, error);
} /* json_printfile() */


JSON_PUBLIC size_t json_printstring(struct json *J, void *dst, size_t lim, int flags, int *error) {
	struct printer P;
	struct json_value *root;
	char buffer[512], *p, *pe;
	size_t count, total;

	if (!(root = print_root(J->root, flags|J->flags)))
		goto empty;

	print_init(&P, root, flags|J->flags);

	p = dst;
	pe = p + lim;
	total = 0;

	while ((count = print(&P, buffer, sizeof buffer))) {
		if (p < pe) {
			memcpy(p, buffer, JSON_MIN((size_t)(pe - p), count));
			p += JSON_MIN((size_t)(pe - p), count);
		}

		total += count;
	}

	if (P.error)
		goto error;

	if (lim)
		((char *)dst)[JSON_MIN(lim - 1, total)] = '\0';

	return total;
error:
	if (error)
		*error = P.error;

	json_throw(J, P.error);
empty:
	if (lim)
		*(char *)dst = '\0';

	return 0;
} /* json_printstring() */


/*
 * J S O N  V A L U E  R O U T I N E S
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

JSON_PUBLIC struct json_value *json_root(struct json *J) {
	return J->root;
} /* json_root() */


JSON_PUBLIC enum json_type json_v_type(struct json *J JSON_NOTUSED, struct json_value *V) {
	return V->type;
} /* json_v_type() */


static int json_v_search_(struct json_value **V, struct json *J JSON_NOTUSED, struct json_value *O, int mode, const void *name, size_t len) {
	struct json_value *K = NULL;
	int error;

	*V = NULL;

	if (O->type != JSON_T_OBJECT) {
		if (!(mode & JSON_M_AUTOVIV) || !(mode & JSON_M_CONVERT))
			return 0;

		if ((error = value_convert(O, JSON_T_OBJECT)))
			goto error;
	}

	if ((*V = object_search(O, name, len)) || !(mode & JSON_M_AUTOVIV))
		return 0;

	if (!(K = value_open(JSON_T_STRING, NULL, &error)))
		goto error;

	if ((error = string_cats(&K->string, name, len)))
		goto error;

	if (!(*V = value_open(JSON_T_NULL, NULL, &error)))
		goto error;

	if ((error = object_insert(O, K, *V)))
		goto error;

	return 0;
error:
	value_close(K, 0);
	value_close(*V, 0);
	*V = NULL;

	return error;
} /* json_v_search_() */


JSON_PUBLIC struct json_value *json_v_search(struct json *J, struct json_value *O, int mode, const void *name, size_t len) {
	struct json_value *V;
	int error;

	if ((error = json_v_search_(&V, J, O, mode, name, len)))
		json_throw(J, error);

	return V;
} /* json_v_search() */


static int json_v_index_(struct json_value **V, struct json *J JSON_NOTUSED, struct json_value *A, int mode, int index) {
	int error;

	*V = NULL;

	if (A->type != JSON_T_ARRAY) {
		if (!(mode & JSON_M_AUTOVIV) || !(mode & JSON_M_CONVERT))
			return 0;

		if ((error = value_convert(A, JSON_T_ARRAY)))
			goto error;
	}

	if ((*V = array_index(A, index)) || !(mode & JSON_M_AUTOVIV))
		return 0;

	if (!(*V = value_open(JSON_T_NULL, NULL, &error)))
		goto error;

	if ((error = array_insert(A, index, *V)))
		goto error;

	return 0;
error:
	value_close(*V, 0);
	*V = NULL;

	return error;
} /* json_v_index_() */


JSON_PUBLIC struct json_value *json_v_index(struct json *J, struct json_value *O, int mode, int index) {
	struct json_value *V;
	int error;

	if ((error = json_v_index_(&V, J, O, mode, index)))
		json_throw(J, error);

	return V;
} /* json_v_index_() */


JSON_PUBLIC int json_v_delete(struct json *J JSON_NOTUSED, struct json_value *V) {
	struct json_value *root;

	for (root = J->root; root; root = root->root) {
		if (root == V) {
			J->root = V->root;
			V->root = NULL;

			break;
		}
	}

	value_close(V, 1);

	return 0;
} /* json_v_delete() */


JSON_PUBLIC int json_v_clear(struct json *J JSON_NOTUSED, struct json_value *V) {
	struct orphans indices, keys;

	CIRCLEQ_INIT(&indices);
	CIRCLEQ_INIT(&keys);

	value_clear(V, &indices, &keys);
	orphans_free(&indices, &keys);

	return 0;
} /* json_v_clear() */


JSON_PUBLIC double json_v_number(struct json *J, struct json_value *V) {
	if (V && V->type != JSON_T_NUMBER && (J->flags & JSON_F_STRONG))
		json_throw(J, JSON_ETYPING);

	return value_number(V);
} /* json_v_number() */


JSON_PUBLIC const char *json_v_string(struct json *J, struct json_value *V) {
	if (V && V->type != JSON_T_STRING && (J->flags & JSON_F_STRONG))
		json_throw(J, JSON_ETYPING);

	return value_string(V);
} /* json_v_string() */


JSON_PUBLIC size_t json_v_length(struct json *J, struct json_value *V) {
	if (V && V->type != JSON_T_STRING && (J->flags & JSON_F_STRONG))
		json_throw(J, JSON_ETYPING);

	return value_length(V);
} /* json_v_length() */


JSON_PUBLIC size_t json_v_count(struct json *J, struct json_value *V) {
	if (V && V->type != JSON_T_ARRAY && V->type != JSON_T_OBJECT && (J->flags & JSON_F_STRONG))
		json_throw(J, JSON_ETYPING);

	return value_count(V);
} /* json_v_count() */


JSON_PUBLIC _Bool json_v_boolean(struct json *J, struct json_value *V) {
	if (V && V->type != JSON_T_BOOLEAN && (J->flags & JSON_F_STRONG))
		json_throw(J, JSON_ETYPING);

	return value_boolean(V);
} /* json_v_boolean() */


JSON_PUBLIC int json_v_setnumber(struct json *J, struct json_value *V, double number) {
	int error;

	if ((error = value_convert(V, JSON_T_NUMBER)))
		return json_throw(J, error);

	V->number = number;

	return 0;
} /* json_v_setnumber() */


JSON_PUBLIC int json_v_setbuffer(struct json *J, struct json_value *V, const void *sp, size_t len) {
	int error;

	if ((error = value_convert(V, JSON_T_STRING)))
		return json_throw(J, error);

	string_reset(&V->string);

	return json_ifthrow(J, string_cats(&V->string, sp, len));
} /* json_v_setbuffer() */


JSON_PUBLIC JSON_DEPRECATED int json_v_setlstring(struct json *J, struct json_value *V, const void *sp, size_t len) {
	return json_v_setbuffer(J, V, sp, len);
} /* json_v_setlstring() */


JSON_PUBLIC int json_v_setstring(struct json *J, struct json_value *V, const void *sp) {
	return json_v_setbuffer(J, V, sp, json_strlen(sp));
} /* json_v_setstring() */


JSON_PUBLIC int json_v_setboolean(struct json *J, struct json_value *V, _Bool boolean) {
	int error;

	if ((error = value_convert(V, JSON_T_BOOLEAN)))
		return json_throw(J, error);

	V->boolean = boolean;

	return 0;
} /* json_v_setboolean() */


JSON_PUBLIC int json_v_setnull(struct json *J, struct json_value *V) {
	return json_ifthrow(J, value_convert(V, JSON_T_NULL));
} /* json_v_setnull() */


JSON_PUBLIC int json_v_setarray(struct json *J, struct json_value *V) {
	return json_ifthrow(J, value_convert(V, JSON_T_ARRAY));
} /* json_v_setarray() */


JSON_PUBLIC int json_v_setobject(struct json *J, struct json_value *V) {
	return json_ifthrow(J, value_convert(V, JSON_T_OBJECT));
} /* json_v_setobject() */


JSON_PUBLIC int json_i_level(struct json *J JSON_NOTUSED, struct json_iterator *I) {
	return I->level + I->_.depth;
} /* json_i_level() */


JSON_PUBLIC int json_i_depth(struct json *J JSON_NOTUSED, struct json_iterator *I) {
	return I->_.depth;
} /* json_i_depth() */


JSON_PUBLIC int json_i_order(struct json *J JSON_NOTUSED, struct json_iterator *I) {
	return I->_.order;
} /* json_i_order() */


JSON_PUBLIC void json_i_skip(struct json *J JSON_NOTUSED, struct json_iterator *I) {
	I->_.order = ORDER_POST;
} /* json_i_skip() */


JSON_PUBLIC void json_v_start(struct json *J JSON_NOTUSED, struct json_iterator *I, struct json_value *V) {
	memset(&I->_, 0, sizeof I->_);
	I->_.value = V;

	if (I->level < 0)
		I->level = 0;
	if (I->depth <= 0)
		I->depth = INT_MAX;
} /* json_v_start() */


JSON_PUBLIC struct json_value *json_v_next(struct json *J JSON_NOTUSED, struct json_iterator *I) {
	struct json_value *V = I->_.value;

	while ((V = value_next(V, &I->_.order, &I->_.depth))) {
		if (value_iskey(V)) {
			continue;
		} else if (I->level > I->_.depth) {
			continue;
		} else if (I->level + I->depth <= I->_.depth) {
			json_i_skip(J, I);

			continue;
		} else if ((I->flags & (JSON_I_POSTORDER|JSON_I_PREORDER))
		       &&  !(I->flags & I->_.order)) {
			continue;
		}

		break;
	}

	return I->_.value = V;
} /* json_v_next() */


JSON_PUBLIC struct json_value *json_v_keyof(struct json *J JSON_NOTUSED, struct json_value *V) {
	return (V->node && V->node->parent->type == JSON_T_OBJECT)? V->node->key : NULL;
} /* json_v_keyof() */


JSON_PUBLIC int json_v_indexof(struct json *J JSON_NOTUSED, struct json_value *V) {
	return (V->node && V->node->parent->type == JSON_T_ARRAY)? V->node->index : -1;
} /* json_v_indexof() */


/*
 * J S O N  P A T H  R O U T I N E S
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

struct json_path {
	int type, index;
	char key[256], *kp;
	size_t len;
	const char *fmt, *fp;
	va_list ap;
	struct json_value *value;
}; /* struct json_path */


#define path_start(path, fmt) do { \
	(path)->fmt = (fmt); \
	(path)->fp = (fmt); \
	va_start((path)->ap, fmt); \
} while (0)

#define path_end(path) \
	va_end((path)->ap)

#define path_load(error, path, J, fmt, mode) do { \
	path_start((path), fmt); \
	*error = path_exec((J), (path), (mode)); \
	path_end((path)); \
} while (0)


static _Bool path_eof(struct json_path *path) {
	return !*path->fp;
} /* path_eof() */


static unsigned path_getc(struct json_path *path) {
	return (*path->fp)? *path->fp++ : 0;
} /* path_getc() */


static int path_popc(struct json_path *path) {
	int ch;

	switch ((ch = path_getc(path))) {
	case '\\':
		return path_getc(path);
	case '[':
	case ']':
	case '.':
	case '#':
	case '$':
		return -ch;
	default:
		return ch;
	} /* switch() */
} /* path_popc() */


static void path_unget(struct json_path *path) {
	/* NOTE: should never be putting back an escaped character. */
	--path->fp;
} /* path_unget() */


static int key_putc(struct json_path *path, int ch) {
	if (path->kp < json_endof(path->key) - 1) {
		*path->kp++ = ch;

		return 0;
	} else
		return JSON_EBIGPATH;
} /* key_putc() */


static int key_puts(struct json_path *path, const char *str) {
	size_t len, lim;
	
	lim = json_endof(path->key) - path->kp;
	len = json_strlcpy(path->kp, str, lim);

	if (len >= lim)
		return JSON_EBIGPATH;

	path->kp += len;

	return 0;
} /* key_puts() */


static _Bool path_next(struct json_path *path, int *error) {
	int ch, nf, sign, index, len;
	const char *str;

	path->type = 0;
	path->kp = path->key;

	if (!(ch = path_popc(path))) {
		return 0;
	} else if (ch == -'[')
		goto array;

	if (ch == -'.') {
		ch = path_popc(path);

		if (ch == -'.' || ch == -'[')
			goto syntx; /* back-to-back separators illegal */
	}

	nf = 0;

	while (ch && ch != -'.' && ch != -'[') {
		++nf;

		switch (ch) {
		case -'#':
			index = va_arg(path->ap, int);

			len = snprintf(path->kp, json_endof(path->key) - path->kp, "%d", index);

			if (len >= json_endof(path->key) - path->kp) {
				*error = JSON_EBIGPATH;
				goto error;
			} else if (len < 0)
				goto syerr;

			path->kp += len;

			break;
		case -'$':
			str = va_arg(path->ap, char *);

			if ((*error = key_puts(path, str)))
				goto error;

			break;
		default:
			if ((*error = key_putc(path, ch)))
				goto error;

			break;
		}

		ch = path_popc(path);
	}

	if (ch)	
		path_unget(path);

	path->type = JSON_T_OBJECT;
	*path->kp = '\0';
	path->len = path->kp - path->key;

	/*
	 * NOTE: Allow empty string keys as long as we consumed a valid key
	 * format.
	 */
	return nf > 0;
array:
	sign = 1;
	index = 0;

	ch = path_popc(path);

	if (ch == -'#') {
		index = va_arg(path->ap, int);

		ch = path_popc(path);
	} else if (ch == '-') {
		sign = -1;
		goto index;
	} else if (json_isdigit(ch)) {
index:
		do {
			index *= 10;
			index += ch - '0';
		} while ((ch = path_popc(path)) > 0 && json_isdigit(ch));
	} else {
		goto syntx;
	}

	if (ch != -']') {
		goto syntx;
	}

	path->type = JSON_T_ARRAY;
	path->index = sign * index;

	return 1;
syerr:
	*error = errno;

	goto error;
syntx:
	*error = JSON_EBADPATH;
error:
	return 0;
} /* path_next() */


static int path_exec(struct json *J, struct json_path *path, int mode) {
	int error = 0;

	if (!J->root && (mode & JSON_M_AUTOVIV)) {
		if (!(J->root = value_open(JSON_T_NULL, NULL, &error)))
			return error;
	}

	path->value = J->root;

	while (path->value && path_next(path, &error)) {
		if (path->type == JSON_T_OBJECT)
			error = json_v_search_(&path->value, J, path->value, mode, path->key, path->len);
		else
			error = json_v_index_(&path->value, J, path->value, mode, path->index);
	}

	return error;
} /* path_exec() */


static _Bool path_exists(struct json_path *path) {
	return path->value && path_eof(path);
} /* path_exists() */


JSON_PUBLIC int json_push(struct json *J, const char *fmt, ...) {
	struct json_path path;
	int error;

	path_load(&error, &path, J, fmt, J->mode);

	if (error)
		return json_throw(J, error);

	if (!path_exists(&path)) /* either JSON_F_NOAUTOVIV or JSON_F_NOCONVERT */
		return json_throw(J, JSON_ETYPING);

	path.value->root = J->root;
	J->root = path.value;

	return 0;
} /* json_push() */


JSON_PUBLIC void json_pop(struct json *J) {
	struct json_value *oroot;

	if ((oroot = J->root) && oroot->root) {
		J->root = oroot->root;
		oroot->root = NULL;
	}
} /* json_pop() */


JSON_PUBLIC void json_popall(struct json *J) {
	struct json_value *oroot;

	while ((oroot = J->root) && oroot->root) {
		J->root = oroot->root;
		oroot->root = NULL;
	}
} /* json_popall() */


JSON_PUBLIC struct json_value *json_top(struct json *J) {
	return J->root;
} /* json_top() */


JSON_PUBLIC void json_delete(struct json *J, const char *fmt, ...) {
	struct json_path path;
	int error;

	path_load(&error, &path, J, fmt, 0);

	json_ifthrow(J, error);

	if (path.value)
		json_v_delete(J, path.value);
} /* json_delete() */


JSON_PUBLIC enum json_type json_type(struct json *J, const char *fmt, ...) {
	struct json_path path;
	int error;

	path_load(&error, &path, J, fmt, 0);

	json_ifthrow(J, error);

	return (path.value)? path.value->type : JSON_T_NULL;
} /* json_type() */


JSON_PUBLIC _Bool json_exists(struct json *J, const char *fmt, ...) {
	struct json_path path;
	int error;

	path_load(&error, &path, J, fmt, 0);

	json_ifthrow(J, error);

	return (path.value)? 1 : 0;
} /* json_exists() */


JSON_PUBLIC double json_number(struct json *J, const char *fmt, ...) {
	struct json_path path;
	int error;

	path_load(&error, &path, J, fmt, 0);

	json_ifthrow(J, error);

	return json_v_number(J, path.value);
} /* json_number() */


JSON_PUBLIC const char *json_string(struct json *J, const char *fmt, ...) {
	struct json_path path;
	int error;

	path_load(&error, &path, J, fmt, 0);

	json_ifthrow(J, error);

	return json_v_string(J, path.value);
} /* json_string() */


JSON_PUBLIC size_t json_length(struct json *J, const char *fmt, ...) {
	struct json_path path;
	int error;

	path_load(&error, &path, J, fmt, 0);

	json_ifthrow(J, error);

	return json_v_length(J, path.value);
} /* json_length() */


JSON_PUBLIC size_t json_count(struct json *J, const char *fmt, ...) {
	struct json_path path;
	int error;

	path_load(&error, &path, J, fmt, 0);

	json_ifthrow(J, error);

	return json_v_count(J, path.value);
} /* json_count() */


JSON_PUBLIC _Bool json_boolean(struct json *J, const char *fmt, ...) {
	struct json_path path;
	int error;

	path_load(&error, &path, J, fmt, 0);

	json_ifthrow(J, error);

	return json_v_boolean(J, path.value);
} /* json_boolean() */


JSON_PUBLIC int json_setnumber(struct json *J, double number, const char *fmt, ...) {
	struct json_path path;
	int error;

	path_load(&error, &path, J, fmt, J->mode);

	if (error)
		return json_throw(J, error);

	if (!path_exists(&path))
		return json_throw(J, JSON_ETYPING);

	return json_v_setnumber(J, path.value, number);
} /* json_setnumber() */


JSON_PUBLIC int json_setbuffer(struct json *J, const void *src, size_t len, const char *fmt, ...) {
	struct json_path path;
	int error;

	path_load(&error, &path, J, fmt, J->mode);

	if (error)
		return json_throw(J, error);

	if (!path_exists(&path))
		return json_throw(J, JSON_ETYPING);

	return json_v_setbuffer(J, path.value, src, len);
} /* json_setbuffer() */


JSON_PUBLIC JSON_DEPRECATED int json_setlstring(struct json *J, const void *src, size_t len, const char *fmt, ...) {
	struct json_path path;
	int error;

	path_load(&error, &path, J, fmt, J->mode);

	if (error)
		return json_throw(J, error);

	if (!path_exists(&path))
		return json_throw(J, JSON_ETYPING);

	return json_v_setbuffer(J, path.value, src, len);
} /* json_setlstring() */


JSON_PUBLIC int json_setstring(struct json *J, const void *src, const char *fmt, ...) {
	struct json_path path;
	int error;

	path_load(&error, &path, J, fmt, J->mode);

	if (error)
		return json_throw(J, error);

	if (!path_exists(&path))
		return json_throw(J, JSON_ETYPING);

	return json_v_setstring(J, path.value, src);
} /* json_setstring() */


JSON_PUBLIC int json_setboolean(struct json *J, _Bool boolean, const char *fmt, ...) {
	struct json_path path;
	int error;

	path_load(&error, &path, J, fmt, J->mode);

	if (error)
		return json_throw(J, error);

	if (!path_exists(&path))
		return json_throw(J, JSON_ETYPING);

	return json_v_setboolean(J, path.value, boolean);
} /* json_setboolean() */


JSON_PUBLIC int json_setnull(struct json *J, const char *fmt, ...) {
	struct json_path path;
	int error;

	path_load(&error, &path, J, fmt, J->mode);

	if (error)
		return json_throw(J, error);

	if (!path_exists(&path))
		return json_throw(J, JSON_ETYPING);

	return json_v_setnull(J, path.value);
} /* json_setnull() */


JSON_PUBLIC int json_setarray(struct json *J, const char *fmt, ...) {
	struct json_path path;
	int error;

	path_load(&error, &path, J, fmt, J->mode);

	if (error)
		return json_throw(J, error);

	if (!path_exists(&path))
		return json_throw(J, JSON_ETYPING);

	return json_v_setarray(J, path.value);
} /* json_setarray() */


JSON_PUBLIC int json_setobject(struct json *J, const char *fmt, ...) {
	struct json_path path;
	int error;

	path_load(&error, &path, J, fmt, J->mode);

	if (error)
		return json_throw(J, error);

	if (!path_exists(&path))
		return json_throw(J, JSON_ETYPING);

	return json_v_setobject(J, path.value);
} /* json_setobject() */


#if JSON_MAIN

#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <err.h>

#if defined FFI_H_PATH
#include FFI_H_PATH
#else
#include <ffi/ffi.h>
#endif


struct call {
	void *fun;

	ffi_type *rtype;

	union {
		double d;
		int i;
		char *p;
		unsigned long lu;
		char c;
	} rval;

	int argc;

	union {
		double d;
		int i;
		char *p;
		unsigned long lu;
		char c;
	} arg[16];

	ffi_type *type[16];
}; /* struct call */


static void call_init(struct call *call, ffi_type *rtype, void *fun) {
	call->fun = fun;
	call->rtype = rtype;
	call->argc = 0;
} /* call_init() */


static void call_push(struct call *call, ffi_type *type, ...) {
	va_list ap;

	if (call->argc >= (int)json_countof(call->arg))
		return;

	va_start(ap, type);

	if (type == &ffi_type_pointer)
		call->arg[call->argc].p = va_arg(ap, void *);
	else if (type == &ffi_type_ulong)
		call->arg[call->argc].lu = va_arg(ap, unsigned long);
	else if (type == &ffi_type_double)
		call->arg[call->argc].d = va_arg(ap, double);
	else if (type == &ffi_type_schar)
		call->arg[call->argc].c = va_arg(ap, int);
	else
		call->arg[call->argc].i = va_arg(ap, int);

	call->type[call->argc++] = type;

	va_end(ap);
} /* call_push() */


static void call_path(struct call *call, int *argc, char ***argv) {
	struct json_path path;
	int ch;

	if (*argc) {
		path.fmt = path.fp = **argv;
		--*argc;
		++*argv;
	} else
		path.fmt = path.fp = "";

	call_push(call, &ffi_type_pointer, path.fmt);

	while ((ch = path_popc(&path))) {
		switch (ch) {
		case -'#':
			if (!*argc)
				errx(1, "fewer arguments than format specifiers");

			call_push(call, &ffi_type_sint, atoi(**argv));
			--*argc;
			++*argv;

			break;
		case -'$':
			if (!*argc)
				errx(1, "fewer arguments than format specifiers");

			call_push(call, &ffi_type_pointer, **argv);
			--*argc;
			++*argv;

			break;
		default:
			break;
		} /* switch() */
	} /* while() */
} /* call_path() */


static void call_exec(struct call *fun) {
	void *arg[json_countof(fun->arg)];
	ffi_cif cif;
	int i;

	for (i = 0; i < (int)json_countof(arg); i++)
		arg[i] = &fun->arg[i];

	if (FFI_OK != ffi_prep_cif(&cif, FFI_DEFAULT_ABI, fun->argc, fun->rtype, fun->type))
		errx(1, "FFI call failed");

	ffi_call(&cif, FFI_FN(fun->fun), &fun->rval, arg);
} /* call_exec() */


#define USAGE \
	"%s [-pPf:Vh] [CMD [ARG ...] ...]\n" \
	"  -p       pretty print\n" \
	"  -P       print partial subtree\n" \
	"  -A       disable autovivification\n" \
	"  -C       disable conversion\n" \
	"  -s       enable strong typing\n" \
	"  -f PATH  file to parse\n" \
	"  -V       print version\n" \
	"  -h       print usage\n" \
	"\n" \
	"COMMANDS\n" \
	"  print                 print document to stdout using json_printfile\n" \
	"  puts                  print document to stdout using json_printstring\n" \
	"  rewind                rewind printer to beginning\n" \
	"  delete PATH           delete node\n" \
	"  remove N              delete the Nth node in the path stack\n" \
	"  type PATH             print node type\n" \
	"  exists PATH           print whether node exists--yes or no\n" \
	"  number PATH           print number value\n" \
	"  string PATH           print string value\n" \
	"  length PATH           print string length\n" \
	"  count PATH            print object or array entry count\n" \
	"  boolean PATH          print boolean value--true or false\n" \
	"  push PATH             push node onto top of path stack\n" \
	"  pop                   pop a node from path stack\n" \
	"  popall                pop all nodes except real root\n" \
	"  setnumber NUM PATH    set node to number\n" \
	"  setstring TXT PATH    set node to string\n" \
	"  setboolean BOOL PATH  set node to boolean\n" \
	"  setnull PATH          set node to null\n" \
	"  setarray PATH         convert node to array\n" \
	"  setobject PATH        convert node to object\n" \
	"\n" \
	"PATH FORMAT\n" \
	"  A path consists of object and array entry indices, each of which may contain\n" \
	"  one or more format specifiers. For each format specifier the respective\n" \
	"  number or string should be passed as an additional path argument in its\n" \
	"  respective argument position.\n" \
	"\n" \
	"  Object key names should be preceded by a period, and array indices enclosed\n" \
	"  in square brackets. The # format specifier takes a numeric argument, while\n" \
	"  the $ format specifier takes a string as a replacement value. Example:\n" \
	"\n" \
	"  	foo[#].b$ 0 ar\n" \
	"\n" \
	"  This path first indexes the root node as an object with a key of \"foo\".\n" \
	"  \"foo\" is then indexed as an array with a value at position 0. The 0 node is\n" \
	"  in turn treated as an object with a key of \"bar\". These commands\n" \
	"\n" \
	"  	setnumber 47 foo[#].b$ 0 ar\n" \
	"  	number foo[0].bar\n" \
	"\n" \
	"  will print the number 47.0 to stdout.\n" \
	"\n" \
	"Report bugs to <william@25thandClement.com>\n"

static void usage(const char *arg0, FILE *fp) {
	fprintf(fp, USAGE, arg0);
} /* usage() */

static void version(const char *arg0, FILE *fp) {
	fprintf(fp, "%s (json.c) %.8X\n", arg0, json_version());
	fprintf(fp, "built   %s %s\n", __DATE__, __TIME__);
	fprintf(fp, "vendor  %s\n", json_vendor());
	fprintf(fp, "release %.8X\n", json_v_rel());
	fprintf(fp, "abi     %.8X\n", json_v_abi());
	fprintf(fp, "api     %.8X\n", json_v_api());
} /* version() */

static int lex_main(const char *);

int main(int argc, char **argv) {
	extern int optind;
	char *arg0 = (argc)? argv[0] : "json";
	struct json *J;
	int opt, error;
	int volatile flags = 0;
	const char *volatile file = NULL, *volatile cmd;
	struct call fun;
	struct jsonxs trap;

	while (-1 != (opt = getopt(argc, argv, "pPACsf:Vh"))) {
		switch (opt) {
		case 'p':
			flags |= JSON_F_PRETTY;

			break;
		case 'P':
			flags |= JSON_F_PARTIAL;

			break;
		case 'A':
			flags |= JSON_F_NOAUTOVIV;

			break;
		case 'C':
			flags |= JSON_F_NOCONVERT;

			break;
		case 's':
			flags |= JSON_F_STRONG;

			break;
		case 'f':
			file = optarg;

			break;
		case 'V':
			version(basename(arg0), stdout);

			return 0;
		case 'h':
			usage(basename(arg0), stdout);

			return 0;
		default:
			usage(basename(arg0), stderr);

			return 1;
		} /* switch() */
	} /* switch() */

	argc -= optind;
	argv += optind;

	if (argc) {
		cmd = *argv;
		argc--;
		argv++;
	} else {
		cmd = "print";
	}

	if (!strcmp(cmd, "lex"))
		return lex_main(file);

	J = json_open(flags, &error);

	if (file) {
		if (!strcmp(file, "-"))
			error = json_loadfile(J, stdin);
		else
			error = json_loadpath(J, file);

		if (error)
			errx(1, "%s: %s", file, json_strerror(error));
	}

	if ((error = json_enter(J, &trap)))
		errx(1, "%s: %s", file, json_strerror(error));

	do {
		if (!strcmp(cmd, "print")) {
			if ((error = json_printfile(J, stdout, flags)))
				errx(1, "stdout: %s", json_strerror(error));
			if (!(flags & JSON_F_PRETTY))
				fputc('\n', stdout);
		} else if (!strcmp(cmd, "puts")) {
			char *buf;
			size_t len;

			len = json_printstring(J, NULL, 0, flags, NULL);

			if (!(buf = malloc(len + 1)))
				err(1, "stdout");

			json_printstring(J, buf, len + 1, flags, NULL);

			fputs(buf, stdout);
			free(buf);

			if (!(flags & JSON_F_PRETTY))
				fputc('\n', stdout);
		} else if (!strcmp(cmd, "rewind")) {
			json_rewind(J);
		} else if (!strcmp(cmd, "delete")) {
			call_init(&fun, &ffi_type_void, (void *)&json_delete);
			call_push(&fun, &ffi_type_pointer, J);
			call_path(&fun, &argc, &argv);
			call_exec(&fun);
		} else if (!strcmp(cmd, "type")) {
			call_init(&fun, &ffi_type_sint, (void *)&json_type);
			call_push(&fun, &ffi_type_pointer, J);
			call_path(&fun, &argc, &argv);
			call_exec(&fun);
			puts(json_strtype(fun.rval.i));
		} else if (!strcmp(cmd, "exists")) {
			call_init(&fun, &ffi_type_schar, (void *)&json_exists);
			call_push(&fun, &ffi_type_pointer, J);
			call_path(&fun, &argc, &argv);
			call_exec(&fun);
			printf("%s\n", (fun.rval.c)? "yes" : "no");
		} else if (!strcmp(cmd, "number")) {
			call_init(&fun, &ffi_type_double, (void *)&json_number);
			call_push(&fun, &ffi_type_pointer, J);
			call_path(&fun, &argc, &argv);
			call_exec(&fun);
			printf("%f\n", fun.rval.d);
		} else if (!strcmp(cmd, "string")) {
			call_init(&fun, &ffi_type_pointer, (void *)&json_string);
			call_push(&fun, &ffi_type_pointer, J);
			call_path(&fun, &argc, &argv);
			call_exec(&fun);
			printf("%s\n", fun.rval.p);
		} else if (!strcmp(cmd, "length")) {
			call_init(&fun, &ffi_type_ulong, (void *)&json_length);
			call_push(&fun, &ffi_type_pointer, J);
			call_path(&fun, &argc, &argv);
			call_exec(&fun);
			printf("%lu\n", fun.rval.lu);
		} else if (!strcmp(cmd, "count")) {
			call_init(&fun, &ffi_type_ulong, (void *)&json_count);
			call_push(&fun, &ffi_type_pointer, J);
			call_path(&fun, &argc, &argv);
			call_exec(&fun);
			printf("%lu\n", fun.rval.lu);
		} else if (!strcmp(cmd, "boolean")) {
			call_init(&fun, &ffi_type_schar, (void *)&json_boolean);
			call_push(&fun, &ffi_type_pointer, J);
			call_path(&fun, &argc, &argv);
			call_exec(&fun);
			printf("%s\n", (fun.rval.c)? "true" : "false");
		} else if (!strcmp(cmd, "push")) {
			call_init(&fun, &ffi_type_sint, (void *)&json_push);
			call_push(&fun, &ffi_type_pointer, J);
			call_path(&fun, &argc, &argv);
			call_exec(&fun);
		} else if (!strcmp(cmd, "pop")) {
			call_init(&fun, &ffi_type_void, (void *)&json_pop);
			call_push(&fun, &ffi_type_pointer, J);
			call_exec(&fun);
		} else if (!strcmp(cmd, "popall")) {
			call_init(&fun, &ffi_type_void, (void *)&json_popall);
			call_push(&fun, &ffi_type_pointer, J);
			call_exec(&fun);
		} else if (!strcmp(cmd, "delete")) {
			call_init(&fun, &ffi_type_void, (void *)&json_delete);
			call_push(&fun, &ffi_type_pointer, J);
			call_path(&fun, &argc, &argv);
			call_exec(&fun);
		} else if (!strcmp(cmd, "setnumber")) {
			call_init(&fun, &ffi_type_sint, (void *)&json_setnumber);
			call_push(&fun, &ffi_type_pointer, J);
			if (!argc)
				errx(1, "setnumber: missing argument");
			call_push(&fun, &ffi_type_double, atof(*argv));
			argc--; argv++;
			call_path(&fun, &argc, &argv);
			call_exec(&fun);
		} else if (!strcmp(cmd, "setstring")) {
			call_init(&fun, &ffi_type_sint, (void *)&json_setbuffer);
			call_push(&fun, &ffi_type_pointer, J);
			if (!argc)
				errx(1, "setstring: missing argument");
			call_push(&fun, &ffi_type_pointer, *argv);
			call_push(&fun, &ffi_type_ulong, strlen(*argv));
			argc--; argv++;
			call_path(&fun, &argc, &argv);
			call_exec(&fun);
		} else if (!strcmp(cmd, "setboolean")) {
			call_init(&fun, &ffi_type_sint, (void *)&json_setboolean);
			call_push(&fun, &ffi_type_pointer, J);
			if (!argc)
				errx(1, "setboolean: missing argument");
			call_push(&fun, &ffi_type_schar, (**argv == 't' || **argv == '1'));
			argc--; argv++;
			call_path(&fun, &argc, &argv);
			call_exec(&fun);
		} else if (!strcmp(cmd, "setnull")) {
			call_init(&fun, &ffi_type_sint, (void *)&json_setnull);
			call_push(&fun, &ffi_type_pointer, J);
			call_path(&fun, &argc, &argv);
			call_exec(&fun);
		} else if (!strcmp(cmd, "setarray")) {
			call_init(&fun, &ffi_type_sint, (void *)&json_setarray);
			call_push(&fun, &ffi_type_pointer, J);
			call_path(&fun, &argc, &argv);
			call_exec(&fun);
		} else if (!strcmp(cmd, "setobject")) {
			call_init(&fun, &ffi_type_sint, (void *)&json_setobject);
			call_push(&fun, &ffi_type_pointer, J);
			call_path(&fun, &argc, &argv);
			call_exec(&fun);
		} else if (!strcmp(cmd, "remove")) {
			struct json_value *stack[16], *root;
			int i, n, error;

			if (!argc)
				errx(1, "remove: missing argument");

			if ((i = atoi(*argv)) < 0 || i >= (int)json_countof(stack))
				errx(1, "remove: %d: illegal argument", i);

			argc--; argv++;

			n = 0;

			for (root = J->root; root && n < (int)json_countof(stack); root = root->root) {
				stack[n++] = root;
			}

			if (n > 0) {
				i = JSON_MIN(i, n - 1);

				if ((error = json_v_delete(J, stack[n - (i + 1)])))
					errx(1, "remove: %d: %s", i, json_strerror(error));
			}
		} else {
			errx(1, "%s: invalid command", cmd);
		}

		if (argc) {
			cmd = *argv;
			argc--;
			argv++;
		} else
			cmd = NULL;
	} while (cmd);

	json_close(J);

	return 0;
} /* main() */

static int lex_main(const char *file) {
	FILE *fp = stdin;
	struct lexer L;
	char ibuf[1];
	size_t count;
	struct token *T;
	int error;

	if (strcmp(file, "-") && !(fp = fopen(file, "r")))
		err(1, "%s", file);

	lex_init(&L);

	while ((count = fread(ibuf, 1, sizeof ibuf, fp))) {
		if ((error = lex_parse(&L, ibuf, count)))
			errx(1, "parse: %s", strerror(error));
	}

	if (fp != stdin)
		fclose(fp);

	CIRCLEQ_FOREACH(T, &L.tokens, cqe) {
		switch (T->type) {
		case T_STRING:
			fprintf(stdout, "%s: %.*s\n", lex_strtype(T->type), (int)T->string->length, T->string->text);
			break;
		case T_NUMBER:
			fprintf(stdout, "%s: %f\n", lex_strtype(T->type), T->number);
			break;
		default:
			fprintf(stdout, "%s\n", lex_strtype(T->type));
		}
	}

	lex_destroy(&L);

	return 0;
} /* lex_main() */

#endif /* JSON_MAIN */


/*
 * Sanitize macro namespace.
 */
#undef SAY
#undef HAI

