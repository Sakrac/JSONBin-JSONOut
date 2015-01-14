//
// JSONBin
//
// Details in jsonbin.h
//

#include <stdlib.h>	// malloc/free
#include <stdio.h> // snprintf
#include <string.h>	// memset
#include <ctype.h>	// tolower
#include <math.h>	// pow
#include <float.h> // FLT_MAX
#include "jsonbin.h"

namespace jbin {

// Context states during parsing (internal)
enum eJSONCtx {
	JSON_ROOT,
	JSON_GET_TAG,
	JSON_COLON,
	JSON_VALUE,
	JSON_OBJECT_OPEN,
	JSON_OBJECT,
	JSON_OBJECT_CLOSE,
	JSON_ARRAY_OPEN,
	JSON_ARRAY,
	JSON_ARRAY_CLOSE,
	JSON_NULL_TAG,
	JSON_STRING_VALUE,
	JSON_NUMERIC_VALUE,	// integer or floating pont value
	JSON_TRUE_VALUE,
	JSON_FALSE_VALUE,
	JSON_NULL_VALUE,
	JSON_COMMENT,		// non-standard extension to allow cases where users sprinkled C style comments into JSON files.
};

// unsigned typedefs
typedef unsigned char u8;
typedef unsigned int uint;
typedef unsigned long long ull;

// used for traversing a JSON file
struct JBParse {
	enum {
		PARENT,
		ELDER,
		HIER_COUNT
	};

	int ctx_stack;			// context stack index (context of parsing differs from actual hierarchy depth)
	int level;				// hierarchy level
	int items;				// number of items so far
	JBItem *pItem;			// fill in items in the array after item count and text size has been determined
	eJSONCtx context[JSON_MAX_CONTEXT];			// context of parsing
	JBItem *aHier[JSON_MAX_DEPTH][HIER_COUNT];	// hierarchy (parent/elder sibling for current level in hierarchy)

	eJSONCtx get_context() const { return context[ctx_stack]; } // get current context
	void push_context(eJSONCtx ctx) { context[++ctx_stack] = ctx; } // increment context stack and set context
	void set_context(eJSONCtx ctx) { context[ctx_stack] = ctx; } // change current context to this
	void set_or_push_context(eJSONCtx ctx, bool push) { if (push) ctx_stack++; context[ctx_stack] = ctx; }
	void step_value(JBType type);				// set values in an item and step to the next one
};

// using macros to keep data cursor and remaining size in sync
#define text_step(text, left) text++; left--
#define text_back(text, left) text--; left++
#define text_pop(text, left) *text++; left--
#define text_skip(text, left, skip) { uint _s=(uint)(skip); text += _s; left -= _s; }

// A simple hash table (separate chaining)
struct sHashLink {			// always accessed as a pair and same size
	uint hash;
	uint next;		// 0 = last, otherwise next index is (next-1)
};

struct sStrOffs {
	uint offs;
#ifdef JB_STRLEN
	uint length;
#endif
};

struct sStrCache {
	int hashTableSize; // number of entries in the hash table
	int numStr; // number of unique strings so far
	int numStrMax; // total number of anticpated strings (this is evaluated first)
	uint *aHashTable; // hashTable where index=0 => no value; otherwise refers to element index-1 in aHashLinks
	sHashLink *aHashLinks; // storage for hashes and links (indexing next element in the same hash key)
	uint *aStrLen; // length of string for each index
	const char **apStrings; // pointer to string for each index
	const jchar *pRetStrBase;
	sStrOffs *apRetStr;
	int findHash(uint hash); // find a string with a hash that matches the given
	bool checkStr(const char *str, uint len, int index); // compare strings when hashes match to make sure
	int getStringIndex(const char *str, uint len); // get the index for a given string or -1 if not found
	void addString(const char *str, uint len); // add a string if it wasn't already added
};

// global strings for json keywords
const char* _true("true");
const char* _false("false");
const char* _null("null");

// Some accessors for JBItem members with more complicated traits
#ifndef JB_KEY_STRING
const char *JBItem::getName() const
{
	static char buf[16];
	buf[0] = '0';
	buf[1] = 'x';
	uint h = hash;
	for (int o = 9; o>=2; --o) {
		buf[o] = (h&0xf)<=9?((h&0xf)+'0'):((h&0xf)+'a'-10);
		h >>= 4;
	}
	buf[10] = 0;
	return buf;
}
#endif

#ifndef JB_STRLEN
unsigned int JBItem::getStrLen() const
{
#if defined(JB_WCHAR16)
	return getStr() ? (unsigned int)wcslen(getStr()) : 0;
#else
	return getStr() ? (unsigned int)strlen(getStr()) : 0;
#endif
}
#ifdef JB_KEY_STRING
unsigned int JBItem::getNameLen() const
{
#if defined(JB_WCHAR16)
	return getName() ? (unsigned int)wcslen(getName()) : 0;
#else
	return getName() ? (unsigned int)strlen(getName()) : 0;
#endif
}
#endif
#endif

// parse string data following \u
static unsigned short getUCode(const char *ptr)
{
	unsigned short c = 0;
	for (int n = 0; n < 4; n++) {
		int d = (u8)*ptr++ - '0';
		if (d > 9) {
			d -= 'A' - '0' - 10;	// uppercase or lowercase a-f
			if (d > 0xf)
				d -= 'a' - 'A';
		}
		if (d < 0 || d > 0xf)
			d = 0;
		c = (c << 4) | (d & 0xf);
	}
	return c;
}

// parse custom JSON string code (initated with '\')
static uint solidusCode(const char *ptr, int left, int &code_len)
{
	uint c = (u8)tolower(*ptr++);
	switch (c) {
		case 'b':  code_len = 2; return '\b';	// backspace
		case 'n':  code_len = 2; return '\n';	// newline
		case 'r':  code_len = 2; return '\r';	// carriage return
		case 'f':  code_len = 2; return '\f';	// formfeed
		case 't':  code_len = 2; return '\t';	// tab
		case '\\': code_len = 2; return '\\';	// reverse solidus
		case '/':  code_len = 2; return '/';	// solidus
		case '"':  code_len = 2; return '"';	// quotation mark
		case 'u':								// 4 hex digits
			if (left >= 5) {
				uint c = getUCode(ptr);
				code_len = 6;
#ifdef JB_UTF16_SURROGATE_PAIRS
				if (c >= 0xd800 && c < 0xe000) {	// invalid utf-16 character, check if utf-16
					if (left >= 11 && ptr[5] == '\\' && tolower(ptr[6]) == 'u') {
						uint d = getUCode(ptr + 7);
						if (d>0xdc00 && d < 0xe000) {
							code_len = 12;
							return (((c & 0x3ff) << 10) | (d & 0x3ff)) + 0x10000;
						}
					}
				}
#endif
				return c;
			}
			break;
	}
	return '\\';	// no valid code found, treat char as raw ascii
}

// returns a single code from the string (single char or escape code)
static uint getCode(const char *ptr, int left, int &code_len)
{
	if (left) {
		uint c = (u8)*ptr++;
		code_len = 1;
		left--;
		if (left && c == '\\')
			return solidusCode(ptr, left, code_len);
		return c;
	}
	return 0;
}

// returns a full utf-8 character, decoded from chars or JSON code (if invalid, just return original code)
static uint getChar(const char *ptr, int left, int &char_len)
{
	uint c = getCode(ptr, left, char_len);
	if (c >= 0xc0 && c<0x100 && left>char_len) {	// utf-8 character check
		int code_len;
		int total = char_len;
		text_skip(ptr, left, total);
		uint c8 = c;
		c8 &= 0x7f;
		for (uint m = 0x40; (m & c8) && left; m <<= 5) {
			uint n = getCode(ptr, left, code_len);
			if (n < 0x80 || n >= 0xc0)
				return c; // unexpected utf-8 code, terminate and return original character (probably ANSI anyway)
			total += code_len;
			text_skip(ptr, left, code_len);
			c8 = ((c8 & ~m) << 6) | (n & 0x3f);
		}
		char_len = total;
		return c8;
	}
	return c;
}

// get utf-8 length from a JSON string
static uint getStrLen(const char* ptr, int left)
{
	uint len = 0;
	int skip;
	while (left > 0) {
		uint c = getChar(ptr, left, skip);
		text_skip(ptr, left, skip);
#ifdef JB_WCHAR16
		len += (c>=0xd800&&c<0xe000) ? 0 : (c>=0x10000 ? 2 : 1);	// unicode length in unicode chars - chars > 0x10000 uses 2 wchars, invalid characters removed
#else
		len += 1 + int(c >= 0x80) + int(c >= 0x800) + int(c >= 0x10000);	// utf-8 length
#endif
	}
	return len;
}

// store one utf-8 or wchar character
static int asEncoding(uint c, jchar *out)
{
#ifdef JB_WCHAR16
	if (c >= 0xd800 && c < 0xe000)
		return 0;	// invalid utf-16 - skip
	if (c < 0x10000) {
		*out++ = (jchar)c;
		return 1;
	}
	c -= 0x10000;
	*out++ = 0xd800 | ((c >> 10) & 0x3ff);
	*out++ = 0xdc00 | (c & 0x3ff);
	return 2;	// counting wchars, not bytes
#else
	if (c < 0x80) {
		*out++ = c;
		return 1;
	} else if (c < 0x800) {
		*out++ = 0xc0 | (c >> 6);
		*out++ = 0x80 | (c & 0x3f);
		return 2;
	} else if (c < 0x10000) {
		*out++ = 0xe0 | (c >> 12);
		*out++ = 0x80 | ((c >> 6) & 0x3f);
		*out++ = 0x80 | (c & 0x3f);
		return 3;
	}
	*out++ = 0xf0 | ((c >> 18) & 7);
	*out++ = 0x80 | ((c >> 12) & 0x3f);
	*out++ = 0x80 | ((c >> 6) & 0x3f);
	*out++ = 0x80 | (c & 0x3f);
	return 4;
#endif
}

// store one utf-8 or wchar string by length
static int toEncoding(const char* ptr, int left, jchar *out)
{
	jchar *orig = out;
	int skip;
	while (left) {
		out += asEncoding(getChar(ptr, left, skip), out);
		text_skip(ptr, left, skip);
	}
	*out = 0;
	return int(out - orig);
}

static uint getWhiteSpaceSize(const char *text, uint left)
{
	uint orig = left;
	while (left && *text <= ' ') {
		text++;
		left--;
	}
	return orig - left;
}

#ifdef JB_64BIT_VALUES
#define FP_MAXEXP_EXP DBL_MAX_10_EXP
#define FP_MAXEXP_INT 1
#define FP_MAXEXP_FRC .7976931348623158
#define INT_MAX_INT ((1ULL<<63))
#else
#define FP_MAXEXP_EXP FLT_MAX_10_EXP
#define FP_MAXEXP_INT 3
#define FP_MAXEXP_FRC .402823467
#define INT_MAX_INT ((1UL<<31))
#endif

// note: strtof/strtod is slow and this version does floating point and integer at once
static jbfloat getNumStr(const char *ptr, int left, jbint &intnum, int &num_len, bool &real, bool &representable)
{
	static const uint max_exp = (~0U) / 20;
	static const ull max_int_add_digit = (~0ULL) / 10;
	ull n_int = 0;
	ull n_frac = 0;
	int n_frac_size = 0;
	int n_exp = 0;
	bool neg = false;
	bool neg_exp = false;
	bool int_over = false;
	real = false;
	int orig_len = left;

	// skip initial spaces
	while (left && *ptr <= ' ') { left--; ptr++; }

	if (left) {
		if (*ptr == '-') { neg = true; left--; ptr++; } // +/-?
		else if (*ptr == '+') { left--; ptr++; }
		// count numbers before any control character
		while (left && *ptr >= '0' && *ptr <= '9') {
			if (n_int < max_int_add_digit)	// for floating point values ignoring overflow digits doesn't matter
				n_int = n_int * 10 + (*ptr - '0');
			else							// for integer values ignoring digits does matter
				int_over = true;
			text_step(ptr, left);
		}
		if (left && *ptr == '.') {	// fraction?
			text_step(ptr, left);
			real = true;
			// count fractional numbers
			while (left && *ptr >= '0' && *ptr <= '9') {
				if (n_frac < ((~0ULL) / 10)) {	// stop reading more fraction numbers when no more room in word
					n_frac = n_frac * 10 + (*ptr - '0');
					n_frac_size++;
				}
				text_step(ptr, left);
			}
		}
		// check for exponent
		if (left && (*ptr == 'e' || *ptr == 'E')) {
			text_step(ptr, left);
			real = true;
			if (*ptr == '-') { neg_exp = true; left--; ptr++; }	// check for +/- sign
			else if (*ptr == '+') { left--; ptr++; }
			while (left && *ptr >= '0' && *ptr <= '9') { // read numbers
				if ((uint)n_exp < max_exp)	// exp is signed so divide by 2 and 10
					n_exp = n_exp * 10 + (*ptr - '0');
				text_step(ptr, left);
			}
			if (neg_exp)
				n_exp = -n_exp; // negate exponent if needed
		}
	}
	num_len = orig_len - left;

	if (real) {
		double frac = double(n_frac) / pow(10.0, n_frac_size);
		representable = n_exp < FP_MAXEXP_EXP || (n_exp == FP_MAXEXP_EXP && (n_int < FP_MAXEXP_INT || (n_int == FP_MAXEXP_INT && frac <= FP_MAXEXP_FRC)));
		if (!representable)
			return jbfloat(0.0);

		jbfloat ret = jbfloat((double(n_int) + frac) * pow(10.0, n_exp));
		if (neg)
			ret = -ret;
		intnum = jbint(ret); // some shifting may have occured - convert back to integer value
		return ret;
	}
	// no fractional or exponential parts encountered, treat as integer and check range
	representable = !int_over;
#ifndef JB_64BIT_VALUES		
	representable = representable && n_int <= INT_MAX_INT;
#endif
	if (representable) {
		intnum = neg ? -jbint(n_int) : jbint(n_int);
		return jbfloat(intnum);
	}
	intnum = 0;
	return jbfloat(0.0);
}

// raw fnv1a hash (pre-utf-8)
static uint fnv1A(const char *s, uint l)
{
	u8 *r = (u8*)s;
	uint hash = JB_FNV1A_SEED;
	while (l--)
		hash = (*r++ ^ hash) * JB_FNV1A_PRIME;
	return hash;
}

#ifndef JB_KEY_HASH
unsigned int JBItem::getHash() const
{
	return fnv1A(getName(), getNameLen());
}
#endif

// find a character in a string of given length
static const char *findChar(const char *str, uint left, char c)
{
	while (left && *str != c) {
		text_step(str, left);
	}
	return left ? str : NULL;
}

#ifdef JB_ALLOW_C_COMMENTS
static const char *findChar(const char *str, uint left, char c1, char c2)
{
	while (left && *str != c1 && *str != c2) {
		text_step(str, left);
	}
	return left ? str : NULL;
}

static uint endOfLine(const char *start, uint left)
{
	if (const char *nextLine = findChar(start, left, '\n'))
		return (uint)(nextLine-start);	// characters to skip to get to next line
	return left;	// end of line not found
}

static uint commentSize(const char *comment, uint left)
{
	if (left<2 || *comment!='/')
		return left;	// error - end processing by going to end of file
	const char *body = comment+2;
	uint body_left = left-2;
	switch (comment[1]) {
		case '/':
			return endOfLine(comment, left);
		case '*':
			while (const char *body_end = findChar(body, body_left, '*')) {
				uint end_left = left - (uint)(body_end-body);
				if (end_left<2)
					return left;
				else if (body_end[1]=='/')
					return uint(body_end+2-comment);
			}
			break;
	}
	return left;
}

#endif

// count how many instances of a given character precedes the current character
static int countBack(const char *str, uint left, char c)
{
	int ret = 0;
	while (left && *--str == c) {
		left--;
		ret++;
	}
	return ret;
}

// given a quoted string, return the address of the terminating quote
static const char* quoteEnd(const char *str, uint left)
{
	uint skip;
	text_step(str, left);
	while (const char *_end = findChar(str, left, '"')) {	// find  end of string
		skip = (uint)(_end - str);
		str = _end + 1;
		left -= skip + 1;
		if (!(countBack(_end, skip, '\\') & 1)) // ignore \" and any number of \\ immediately preceding it
			return _end;
	}
	return NULL;
}

// returns length of word if found or 0 if not. first char is passed in as the first character
bool sameWord(const char *wordA, const char *wordB, int lenA)
{
	while (lenA-- && *wordB && tolower(*wordA++) == *wordB++);
	return !*wordB;
}

#ifdef JB_KEY_HASH
// It is necessary to do the hashing byte by byte because the input string is a raw JSON string
static uint hashJSONStr(const char *s, int l)
{
	int skip;
	uint hash = JB_KEY_HASH_PRIME;
	while (l) {
		uint c = getChar(s, l, skip);
		s += skip;
		l -= skip;
		if (c < 0x80)
			hash = JB_KEY_HASH(hash, c);
		else if (c < 0x800) {
			hash = JB_KEY_HASH(hash, (u8)(0xc0 | (c >> 6)));
			hash = JB_KEY_HASH(hash, (u8)(0x80 | (c & 0x3f)));
		} else if (c < 0x10000) {
			hash = JB_KEY_HASH(hash, (u8)(0xc0 | (c >> 12)));
			hash = JB_KEY_HASH(hash, (u8)(0x80 | ((c >> 6) & 0x3f)));
			hash = JB_KEY_HASH(hash, (u8)(0x80 | (c & 0x3f)));
		} else {
			hash = JB_KEY_HASH(hash, (u8)(0xc0 | ((c >> 18) & 7)));
			hash = JB_KEY_HASH(hash, (u8)(0x80 | ((c >> 12) & 0x3f)));
			hash = JB_KEY_HASH(hash, (u8)(0x80 | ((c >> 6) & 0x3f)));
			hash = JB_KEY_HASH(hash, (u8)(0x80 | (c & 0x3f)));
		}
	}
	return hash;
}

#endif

const JBItem* JBItem::findByHash(unsigned int hash) const
{
	if ((type == JB_OBJECT || type == JB_ROOT) && data.i) {
		for (const JBItem *i = getChild(); i; i = i->getSibling())
			if (i->getHash() == hash)
				return i;
	}
	return NULL;
}

//
// String Cache Operations
//

// find a hash value in a hash table
int sStrCache::findHash(uint hash)
{
	uint element = aHashTable[(hash ^ (hash >> 16 | hash << 16)) % hashTableSize];	// get first element from seed index
	while (element) {
		if (aHashLinks[element - 1].hash == hash)
			return element;
		element = aHashLinks[element - 1].next;
	}
	return 0;
}

// compare strings to make sure the same even if hashes match
bool sStrCache::checkStr(const char *str, uint len, int index)
{
	if (index < 0 || len != aStrLen[index])
		return false;
	const char *cmp = apStrings[index];
	if (cmp == str)
		return true;
	while (len && *cmp++ == *str++)
		len--;
	return !len;
}

// get the string cache index for a given string
int sStrCache::getStringIndex(const char *str, uint len)
{
	uint hash = fnv1A(str, len);
	if (int element = findHash(hash)) {
		while (element) {
			if (checkStr(str, len, element - 1))
				return element - 1;
			do { element = aHashLinks[element - 1].next; } while (element && aHashLinks[element - 1].hash != hash);
		}
	}
	return -1;
}

// add a string if it wasn't already to the string cache
void sStrCache::addString(const char *str, uint len)
{
	uint hash = fnv1A(str, len);
	int element = 0;
	if ((element = findHash(hash))) {
		while (element) {
			if (checkStr(str, len, element - 1))
				break;
			do { element = aHashLinks[element - 1].next; } while (element && aHashLinks[element - 1].hash != hash);
		}
	}
	if (!element) {
		int slot = (hash ^ (hash >> 16 | hash << 16)) % hashTableSize;
		int insert = numStr++;
		aHashLinks[insert].hash = hash;
		aHashLinks[insert].next = aHashTable[slot];
		aHashTable[slot] = insert + 1;
		apStrings[insert] = str;
		aStrLen[insert] = len;
	}
}

//
// Finalize an item and step to the next item
//

void JBParse::step_value(JBType type) {
	items++;							// a value was added
	ctx_stack--;						// pop the context stack
	if (pItem) {						// fill in the binary item if memory was allocated
		aHier[level][PARENT]->data.i++;	// increment parent item # children
		if (aHier[level][ELDER])		// update elder sibling item offset
			aHier[level][ELDER]->sibling = (int)(pItem - aHier[level][ELDER]);
		aHier[level][ELDER] = pItem;	// set this as the elder for the next sibling
		pItem->type = type;				// set the type
		pItem->sibling = 0;				// terminate this sibling link in case it is the last
		if (type == JB_ARRAY || type == JB_OBJECT) {
			pItem->data.i = 0;			// arrays and objects keep track of number of children in the data field
			level++;					// arrays and objects add a hierarchy level
			aHier[level][JBParse::PARENT] = pItem;
			aHier[level][JBParse::ELDER] = NULL;
		}
		pItem++;						// this item is completed, step to next
	}
}

#ifdef JB_ALLOW_C_COMMENTS
#define JB_QUOTE_FIND '"', '/'
#else
#define JB_QUOTE_FIND '"'
#endif

// convert a text based json file to a binary representation
JBItem* JSONBin(const char *json, uint size, JBRet *info)
{
	JBParse read = { 0 };	// clear all members of parsing struct
	JBItem *pRet = NULL;	// return data pointer
	JBError error = JBERR_NONE;

#ifdef JB_HANDLE_UTF8_BOM
	if (size >= 3 && (u8)json[0] == 0xef && (u8)json[1] == 0xbb && (u8)json[2] == 0xbf) {
		json += 3;
		size -= 3;
	}
#endif

	// Building a sorted hash array for the strings. first count the number of potential strings to allocate the hash array and string lookup
	struct sStrCache strCache = { 0 };	// clear all members
	{	// using scope to identify string counting section
		int numStrMax = 0;
		const char *quote_str = json;
		uint quote_left = size;
		uint string_size_orig = 0;
		while (const char *quote_next = findChar(quote_str, quote_left, JB_QUOTE_FIND)) {	// find start of a quote
#ifdef JB_ALLOW_C_COMMENTS
			if (*quote_next=='/' && (quote_next[1]=='/' || quote_next[1]=='*')) {
				text_skip(quote_str, quote_left, commentSize(quote_next, quote_left) + (quote_next-quote_str));
			} else
#endif
			{
				quote_left -= (uint)(quote_next - quote_str);
				quote_str = quote_next;
				if (const char *quote_end = quoteEnd(quote_str, quote_left)) {
					numStrMax++;
					quote_end++;
					uint quote_len = (uint)(quote_end - quote_str);
					string_size_orig += quote_len;
					quote_left -= quote_len;
					quote_str = quote_end;
				}
			}
		}
		strCache.numStrMax = numStrMax;
		strCache.hashTableSize = (numStrMax / JB_HASH_COUNT_DIV) < 1024 ? 1024 : (numStrMax / JB_HASH_COUNT_DIV);
		if (info) {
			info->text_orig = string_size_orig;
			info->strings_orig = numStrMax;
		}

		// get work memory (single allocation)
		if ((strCache.apStrings = (const char**)malloc(sizeof(const char**) * numStrMax +
			sizeof(uint) * numStrMax +
			sizeof(struct sHashLink) * numStrMax +
			sizeof(uint) * strCache.hashTableSize))) {
			strCache.aStrLen = (uint*)&strCache.apStrings[numStrMax];
			strCache.aHashLinks = (struct sHashLink*)&strCache.aStrLen[numStrMax];
			strCache.aHashTable = (uint*)&strCache.aHashLinks[numStrMax];
			memset(strCache.aHashTable, 0, sizeof(uint) * strCache.hashTableSize);
		} else
			error = JBERR_OUT_OF_MEMORY;
	}

	const char *cursor = json;

	// first pass determines the number of things, the second builds them
	for (int pass = 0; pass < 2 && error == JBERR_NONE; pass++) {

		// reset hierarchy
		read.aHier[0][JBParse::PARENT] = read.pItem;
		read.aHier[0][JBParse::ELDER] = NULL;
		read.items = 1;	// account for root item
		read.level = 0;

		// initialize root node (all items are 0'd)
		if (read.pItem) {
			read.pItem->type = JB_ROOT;
			read.pItem++;
		}

		// go through entire file
		cursor = json;
		uint left = size;
		while (left && error == JBERR_NONE) {
			text_skip(cursor, left, getWhiteSpaceSize(cursor, left));
			eJSONCtx ctx = read.get_context();
			char c = text_pop(cursor, left);
			switch (c) {	// handle next JSON character
				case '{':
					if (!read.ctx_stack)
						read.push_context(JSON_OBJECT);
					else if (ctx == JSON_VALUE || ctx == JSON_ARRAY)
						read.push_context(JSON_OBJECT_OPEN);
					else
						error = JBERR_UNEXPECTED_BRACE;
					break;

				case '}':
					if (ctx == JSON_OBJECT)
						read.set_context(JSON_OBJECT_CLOSE);
					else
						error = JBERR_UNEXPECTED_CLOSE_BRACE;
					break;

				case '[':
#ifdef JB_ALLOW_ROOT_ARRAY
					if (ctx == JSON_ROOT) {
						read.push_context(JSON_ARRAY);
						if (pRet)
							pRet->type = JB_ARRAY;
					} else
#endif
					if (ctx == JSON_VALUE || ctx == JSON_ARRAY)	// allow array of arrays
						read.push_context(JSON_ARRAY_OPEN);
					else
						error = JBERR_UNEXPECTED_BRACKET;
					break;

				case ']':
					if (ctx == JSON_ARRAY)
						read.set_context(JSON_ARRAY_CLOSE);
					else
						error = JBERR_UNEXPECTED_CLOSE_BRACKET;
					break;

				case '"':
					switch (ctx) {
						case JSON_OBJECT: read.push_context(JSON_GET_TAG); break;
						case JSON_ARRAY: read.push_context(JSON_STRING_VALUE); break;
						case JSON_VALUE: read.set_context(JSON_STRING_VALUE); break;
						default: error = JBERR_UNEXPECTED_QUOTE; break;
					}
					break;

				case ':':
					if (ctx == JSON_COLON)
						read.set_context(JSON_VALUE);
					else
						error = JBERR_UNEXPECTED_COLON;
					break;

				case ',':
					if (ctx != JSON_OBJECT && ctx != JSON_ARRAY)
						error = JBERR_UNEXPECTED_COMMA;
					break;
				case '/':
#ifdef JB_ALLOW_C_COMMENTS
					if (left && (*cursor=='/' || *cursor=='*')) {
						text_back(cursor, left);
						text_skip(cursor, left, commentSize(cursor, left));
						read.push_context(JSON_COMMENT);
					} else
#endif
						error = JBERR_UNEXPECTED_CHARACTER;
					break;

				default:
					text_back(cursor, left);	// back up to do word compare
					if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+')
						read.set_or_push_context(JSON_NUMERIC_VALUE, ctx == JSON_ARRAY);
					else if ((ctx == JSON_ARRAY || ctx == JSON_OBJECT) && sameWord(cursor, _null, left)) {
						read.push_context(JSON_NULL_TAG);
						text_skip(cursor, left, strlen(_null));
					} else if (ctx != JSON_VALUE && ctx != JSON_ARRAY) {
						error = JBERR_UNEXPECTED_NULL;	// other characters can only be accepted if not expecting a value
					} else if (sameWord(cursor, _true, left)) {
						read.set_or_push_context(JSON_TRUE_VALUE, ctx == JSON_ARRAY);
						text_skip(cursor, left, strlen(_true));
					} else if (sameWord(cursor, _false, left)) {
						read.set_or_push_context(JSON_FALSE_VALUE, ctx == JSON_ARRAY);
						text_skip(cursor, left, strlen(_false));
					} else if (sameWord(cursor, _null, left)) {
						read.set_or_push_context(JSON_NULL_VALUE, ctx == JSON_ARRAY);
						text_skip(cursor, left, strlen(_null));
					} else
						error = JBERR_UNEXPECTED_CHARACTER;
					break;
			}

			// check assumption of complexity
			if (read.ctx_stack >= JSON_MAX_CONTEXT || read.level >= JSON_MAX_DEPTH)
				error = JBERR_EXCEED_MAX_DEPTH;

			// exit if unexpected data occured at cursor
			if (error != JBERR_NONE)
				break;

			// handle the current context
			switch (read.get_context()) {
				// read in a key
				case JSON_GET_TAG:
					text_back(cursor, left);
					if (const char *quote_end = quoteEnd(cursor, left)) {
						const char *quote_start = cursor + 1;
						uint quote_len = (uint)(quote_end - quote_start);
						text_skip(cursor, left, quote_end - cursor + 1);
						if (read.pItem) {
#ifdef JB_KEY_HASH
							read.pItem->hash = (quote_start && quote_len) ? hashJSONStr(quote_start, quote_len) : 0;
#endif
#ifdef JB_KEY_STRING
							if (quote_len) {
								int index = strCache.getStringIndex(quote_start, quote_len);
								if (index < 0)
									error = JBERR_INTERNAL_MISS_STR;
								else {
#ifdef JB_INLINE_STRINGS
									read.pItem->name.p = strCache.pRetStrBase+strCache.apRetStr[index].offs;
#else
									read.pItem->name.o = (uint)((const char*)(strCache.pRetStrBase + strCache.apRetStr[index].offs) - (const char*)&read.pItem->name.o);
#endif
#ifdef JB_STRLEN
									read.pItem->name.l = strCache.apRetStr[index].length;
#endif
								}
							}
#endif
						} else if (quote_len) {
#ifdef JB_KEY_STRING
							if (quote_len) {
								strCache.addString(quote_start, quote_len);
								if (strCache.numStr >= strCache.numStrMax)
									error = JBERR_UNEXPECTED_STRCOUNT;
							}
#endif
						}
						read.set_context(JSON_COLON);
					} else
						error = JBERR_UNTERMINATED_QUOTE;
					break;

					// read in "null"
				case JSON_NULL_TAG:
					if (read.pItem)
						read.pItem->data.i = 0;
					read.step_value(JB_NULL);
					break;

					// separator between key and value/object/array
				case JSON_COLON:
					read.set_context(JSON_VALUE);
					break;

					// start of object ('{')
				case JSON_OBJECT_OPEN:
					read.step_value(JB_OBJECT);
					read.set_or_push_context(JSON_OBJECT, read.get_context() == JSON_ARRAY);
					break;

					// end of object ('}')
				case JSON_OBJECT_CLOSE:
					read.ctx_stack--;
					if (read.pItem)
						read.level--;
					break;

					// start of array ('[')
				case JSON_ARRAY_OPEN:
					read.step_value(JB_ARRAY);
					read.set_or_push_context(JSON_ARRAY, read.get_context() == JSON_ARRAY);
					break;

					// end of array (']')
				case JSON_ARRAY_CLOSE:
					read.ctx_stack--;
					if (read.pItem)
						read.level--;
					break;

					// read in a value that is a string
				case JSON_STRING_VALUE:
					text_back(cursor, left);
					if (const char *quote_end = quoteEnd(cursor, left)) {
						const char *quote_start = cursor + 1;
						uint quote_len = (uint)(quote_end - quote_start);
						text_skip(cursor, left, quote_end - cursor + 1);
						if (read.pItem) {
							if (quote_len) { // 0 length strings will already be set to NULL
								int index = strCache.getStringIndex(quote_start, quote_len);
								if (index < 0)
									error = JBERR_INTERNAL_MISS_STR;
								else {
#ifdef JB_INLINE_STRINGS
									read.pItem->data.s.p = strCache.pRetStrBase + strCache.apRetStr[index].offs;
#else
									read.pItem->data.s.o = (uint)((const char*)(strCache.pRetStrBase + strCache.apRetStr[index].offs) - (const char*)&read.pItem->data.s.o);
#endif
#ifdef JB_STRLEN
									read.pItem->data.s.l = strCache.apRetStr[index].length;
#endif
								}
							}
						} else if (quote_len) {
							strCache.addString(quote_start, quote_len);
							if (strCache.numStr >= strCache.numStrMax)
								error = JBERR_UNEXPECTED_STRCOUNT;
						}
						read.step_value(JB_STRING);
					} else
						error = JBERR_UNTERMINATED_QUOTE;
					break;

					// read in a value that is a number
				case JSON_NUMERIC_VALUE: {
					int skip;
					bool real, representable;
					jbint valInt;
					jbfloat valFloat = getNumStr(cursor, left, valInt, skip, real, representable);
					if (!representable)
						error = JBERR_UNREPRESENTABLE;
					else {
						if (read.pItem) {
							if (!real)
								read.pItem->data.i = valInt;
							else
								read.pItem->data.f = valFloat;
						}
						text_skip(cursor, left, skip); // move to the next character
						read.step_value(!real ? JB_INT : JB_FLOAT);
					}
					break;
				}

											// read in a boolean that is true
				case JSON_TRUE_VALUE:
					if (read.pItem)
						read.pItem->data.b = true;
					read.step_value(JB_BOOL);
					break;


					// read in a boolean that is false
				case JSON_FALSE_VALUE:
					if (read.pItem)
						read.pItem->data.b = false;
					read.step_value(JB_BOOL);
					break;

					// read in a value that is null, as opposed to a key/value that is null
				case JSON_NULL_VALUE:
					if (read.pItem)
						read.pItem->data.i = 0;
					read.step_value(JB_NULL_VALUE);
					break;
#ifdef JB_ALLOW_C_COMMENTS
				case JSON_COMMENT:
					read.ctx_stack--;
					break;
#endif

				default:
					break;
			}

			// check assumption of complexity
			if (read.ctx_stack >= JSON_MAX_CONTEXT || read.level >= JSON_MAX_DEPTH)
				error = JBERR_EXCEED_MAX_DEPTH;

			if (read.ctx_stack == 0)	// parsing is complete
				break;
		}

		// after the first pass allocate memory for the determined number of JBItem and the determined amount of unique strings
		if (!pass) {
			// find total size needed for all strings
			int string_bytes = 0;
			for (int i = 0; i < strCache.numStr; i++) {
				uint string_size = getStrLen(strCache.apStrings[i], strCache.aStrLen[i]);
				string_bytes += sizeof(jchar) * (string_size + 1); // just adding up lengths of strings and terminating zeroes
			}

			// get memory for return data and store unique strings immediately after JBItem array
			if ((pRet = (JBItem*)calloc(sizeof(JBItem) * read.items + string_bytes, 1))) { // calloc may zero memory faster than malloc+memset
				jchar *strings = (jchar*)&pRet[read.items];

				// find total size needed for a references to unique strings and find some memory for that
				size_t strPtrSize = sizeof(sStrOffs) * strCache.numStr;
				if (strPtrSize) { // in case there is a json file with no strings no string buffer is necessary
					if (strPtrSize < size_t((char*)&strCache.apStrings[strCache.numStrMax] - (char*)&strCache.apStrings[strCache.numStr])) {
						strCache.apRetStr = (sStrOffs*)&strCache.apStrings[strCache.numStr]; // can re-use allocated memory for return strings
					} else if (!(strCache.apRetStr = (sStrOffs*)malloc(strPtrSize))) { // otherwise must allocate new memory for return strings
						error = JBERR_OUT_OF_MEMORY;
						break;
					}
				}
				strCache.pRetStrBase = strings;
				if (info) {
					info->bin_size = sizeof(JBItem) * read.items + string_bytes;
					info->text_size = string_bytes;
					info->num_items = read.items;
					info->strings_count = strCache.numStr;
				}

				int string_offset = 0;
				for (int i = 0; i < strCache.numStr; i++) {	 // build the string table destination (also convert to UTF-8)
					uint string_length = toEncoding(strCache.apStrings[i], strCache.aStrLen[i], strings + string_offset);
					strCache.apRetStr[i].offs = string_offset;// (const jchar*)(strings + string_offset);
#ifdef JB_STRLEN
					strCache.apRetStr[i].length = string_length; // store the encoded length of the string, excluding terminator
#endif
					string_offset += string_length + 1; // account for terminator
				}
				read.pItem = pRet;
				read.items = 0;
			} else
				error = JBERR_OUT_OF_MEMORY;
		}
	}

	// processing done - free temp work memory
	if (strCache.apRetStr && (size_t(strCache.apRetStr) >= size_t(&strCache.apStrings[strCache.numStrMax]) || size_t(strCache.apRetStr) < size_t(strCache.apStrings)))
		free(strCache.apRetStr); // don't free this if it reused a memory buffer, see above at point of allocation
	if (strCache.apStrings)	// free string cache
		free(strCache.apStrings);

	// clean up on error
	if (error != JBERR_NONE && pRet) {
		free(pRet);			// free allocated return data if invalid
		pRet = NULL;
	}

	// Return stats setting
	if (info) {
		info->bytes_read = (uint)(cursor - json);
		info->err_line = 0;
		info->err_column = 0;
		info->error_code = error;	// report error to caller
		// ERROR REPORTING (tell caller about which line row/column error was encountered)
		if (error != JBERR_NONE) {
			info->bin_size = 0;
			info->num_items = 0;
			info->text_size = 0;
			info->text_orig = 0;
			info->strings_orig = 0;
			info->strings_count = 0;
			if (error != JBERR_OUT_OF_MEMORY) {	// out of memory does not happen at a specific place in the file
				int line = 1;	// scan to line number of cursor
				const char *seek = json;
				const char *line_start = seek;
				for (size_t i = cursor - json; i; --i) {
					if (*seek++ == '\n') {
						line++;
						line_start = seek;
					}
				}
				info->err_line = line;
				info->err_column = int(cursor - line_start + 1);	// offset from start of line to cursor
			}
		}
	}

	return pRet;
}

} // namespace jsonbin

