#ifndef __JB_H__
#define __JB_H__

//
// Summary
//	- Item is used to describe each json name/value, object, array and array element
//	- Input:
//		a json text file (already in memory, utf-8 or ansi assumed) and its size
//	- Return:
//		Pointer to an array of JBItem
//
// Usage
//	- Call JSONBin with a text json file in memory
//	- Check parsing stats with optional JBRet structure
//	- Iterate over an array of fixed size items to process data (tree structure intact)
//	- Single call to free(return address) to clean up.
//	- JBItem member functions
//		- getType(): Get item type (JB_OBJECT, JB_STRING, etc. See JBType enum)
//		- getHash(): Get the hashed value of the item name (user defined or fnv1a)
//		- getName(): Get the name as a string (if included, otherwise a string of the hash)
//		- getNameLen(): Get the number of chars/wchars for the name
//		- getStr(): Get the string value of the item (requires type JB_STRING)
//		- getStrLen(): Get the length of the string value of the item
//		- getInt(): Get the integer value of the item (requires type JB_INT or JB_FLOAT)
//		- getFloat(): Get the float value of the item (requires type JB_INT or JB_FLOAT)
//		- getBool(): Get the bool value of the item (requires type JB_BOOL)
//		- getChild(): Get the first child item (requires type JB_OBJECT or JB_ARRAY)
//		- getSibling(): Get the next item at this level (returns 0 after last)
//		- getChildCount(): Get the number of child items (requires type JB_OBJECT or JB_ARRAY)
//		- size(): same function as getChildCount()
//		- end(): returns terminating JBIterator (NULL pointer)
//		- begin(): returns a JBIterator of getChild()
//		- findByHash(hash): returns a child item of this item with a name that is hashed to this value
//
//	- JBIterator usage (optional method of parsing, more STL-like)
//		++JBIterator: step to next item at current hierarchy level
//		successor(): return next item at current hierarchy level
//		has_successor(): check if there is a successor
//		*/->: return pointer to JBItem referenced by JBIterator
//		==/!=: compare if two iterators point to same JBItem
//		bool(): check if JBIterator has a valid reference to a JBItem
//
// Detail of operation
//
// Read in a text JSON file and return a binary interpretation with the following traits:
//	- single allocation (clean up all memory with a single call to free with the
//		returned pointer)
//	- in order to speed up string de-duplication, a string cache is allocated
//		temporarily and potentially a lookup table for converted strings.
//	- if parsing returned data depth first always traverse memory linearly
//	- unique strings similarly traverse memory linearly, duplicate strings are
//		shared.
//	- if relocating memory no fixup required (binary can be saved to disk and
//		reloaded without re-processing or fixup).
//	- it should be possible to re-generate a valid same ordered JSON text file from
//		binary data unless string keys were not stored. Floating point values
//		will only retain as much precision as the system allows. Also all text
//		codes will be converted to utf-8.
//
// Notes:
//	- result data size may be larger than text data size
//	- parser is fairly strict about input and will exit at the first issue but is
//		not explicitly strict
//	- string locale can conflict with JSON style so parsing is strictly utf-8 JSON
//	- editing code locally to fit specific needs is encouraged rather than
//		globally supporting every possible condition.
//
// Optional compiled traits: (comment in/out defines or specify as precompiled flags)
//	- direct pointers to strings (JB_INLINE_STRINGS): Easier debugging but breaks
//		ability to relocate returned data or save/reload binary data.
//	- hashed keys (JB_KEY_HASH): Store keys as hashed values. Define is also hash
//		function. Adds a findByHash method to JBItem.
//	- both hashed keys and string keys (JB_KEY_HASH_AND_VALUE): Keep both the key
//		string AND key hash.
//	- 64 bit values (JB_64BIT_VALUES): Converted numbers are stored as 64 bit,
//		recommended for cases where values are read and then saved even if 32 bits
//		are only necessary.
//	- wchar_t support (JB_WCHAR16): Store strings in memory as wchar_t and assume
//		that this means utf-16 (Windows makes this assumption). Makes it easier
//		to work with Windows but a range of codes are not valid to store.
//	- utf-16 pairs (JB_UTF16_SURROGATE_PAIRS): Non-standard but not really a good
//		case for not supporting. Allows adding pairs of utf-16 characters using
//		\ud800+high 10 bits, \udc00+low 10 bits for higher value utf-16 characters.
//	- bom (JB_HANDLE_UTF8_BOM): JSON files are not defined as needing a bom header
//		but it is convenient to handle.
//	- array on root (JB_ALLOW_ROOT_ARRAY): If a JSON file begins with '[' instead
//		of '{', handle it and change the root node to type JB_ARRAY instead of
//		JB_ROOT. This is not proper JSON but is used in some files.
//
// License
//	Public Domain; no warranty implied; use at your own risk; attribution appreciated.
//	In no event will the authors be held liable for any damages arising from the use of this software.
//	Created by Carl-Henrik Skårstedt (#Sakrac)
//	Version 1.0
//

namespace jbin {

// Returned type
struct JBItem;
struct JBRet;

JBItem* JSONBin(const char *json, unsigned int size, JBRet *info = 0);

#define JB_FNV1A_PRIME 16777619	// as a default, FNV-1A is used for hash
#define JB_FNV1A_SEED 2166136261

#define JB_KEY_HASH_PRIME JB_FNV1A_SEED // this is the seed value for hashing strings if JB_KEY_HASH is defined

// COMPILED TRAITS
//#define JB_INLINE_STRINGS	// replace offset pointers to strings with raw pointers for debugging, but data is no longer relocatable and there are some structure gaps in JBItem in 64 bit.
#define JB_KEY_HASH(hash, x) (((x) ^ hash) * JB_FNV1A_PRIME) // use key hash value instead of key strings, set value to function name that returns a 32 bit hash from a const char*, size_t length
#define JB_KEY_HASH_AND_NAME // if JB_KEY_HASH is defined but also include the name string
//#define JB_64BIT_VALUES // keep 64 bit values instead 32 bit values. uses a little more memory but keeps more precision
//#define JB_STRLEN // keep track of lengths of strings. this is necessary if you allow \u0000 in JSON strings (not supported with JB_INLINE_STRINGS)
//#define JB_WCHAR16 // use wchar_t instead of char in-memory. still expecting proper utf-8 JSON data as input.
#define JB_UTF16_SURROGATE_PAIRS // if \ud800 - \udfff is encountered, check if it is actually a valid utf-16 double character (I know this is a crazy thing to support when only reading utf-8)
#define JB_HASH_COUNT_DIV 4 // number of strings per hash table entry (larger=>less temp memory, smaller=>faster)
#define JB_HANDLE_UTF8_BOM // if utf8 marker is detected, deal with it
#define JB_ALLOW_ROOT_ARRAY // If a JSON file begins with '[' instead of '{', handle it and change the root node to type JB_ARRAY instead of JB_ROOT.

// ITEM TYPES
enum JBType {
	JB_ROOT,		// root object
	JB_OBJECT,		// object, this is an item with object children
	JB_ARRAY,		// array of objects or arrays or values
	JB_STRING,		// string value
	JB_INT,			// int value
	JB_FLOAT,		// float value
	JB_BOOL,		// bool value
	JB_NULL,		// null tag (null)
	JB_NULL_VALUE	// null value ("name" : null)
};

// ERROR CODES (return from JSONBin)
enum JBError {
	JBERR_NONE = 0,						// No error, all went fine, must be 0
	JBERR_UNEXPECTED_BRACE,				// encountered '{' out of place
	JBERR_UNEXPECTED_CLOSE_BRACE,		// encountered '}' out of place
	JBERR_UNEXPECTED_BRACKET,			// encountered '[' out of place
	JBERR_UNEXPECTED_CLOSE_BRACKET,		// encountered ']' out of place
	JBERR_UNTERMINATED_QUOTE,			// encountered '"' starting a quoted string but no ending '"'
	JBERR_UNEXPECTED_QUOTE,				// encountered a quoted string out of place
	JBERR_UNEXPECTED_COLON,				// encountered ':' out of place
	JBERR_UNEXPECTED_COMMA,				// encountered ',' out of place
	JBERR_UNEXPECTED_NULL,				// encountered 'null' out of place
	JBERR_UNEXPECTED_CHARACTER,			// encountered a character that couldn't be handled
	JBERR_EXCEED_MAX_DEPTH,				// exceeded max parsing hierarchy, update JSON_MAX_LEVEL and JSON_MAX_CONTEXT
	JBERR_UNEXPECTED_STRCOUNT,			// this indicates an internal error (bug)
	JBERR_INTERNAL_MISS_STR,			// this indiactes an internal missing string (bug)
	JBERR_UNREPRESENTABLE,				// value can not be represented
	JBERR_OUT_OF_MEMORY,				// failed to allocate a buffer for processing
};

// Assumption of max hierarchical depth in a JSON file
enum { JSON_MAX_DEPTH = 256 };
enum { JSON_MAX_CONTEXT = 256 };

// evaluates to true if JBItem should contain named items (otherwise hashed value of name only)
#if !defined(JB_KEY_HASH) || defined(JB_KEY_HASH_AND_NAME)
#define JB_KEY_STRING 
#endif

// jchar is either char (standard, utf-8) or wchar_t (assumed utf-16, easier with Win32, can't handle all codes)
#ifdef JB_WCHAR16
typedef wchar_t jchar;
#else
typedef char jchar;
#endif

#ifdef JB_STRLEN
#ifdef JB_INLINE_STRINGS
typedef struct { const jchar *p; unsigned int l; } JBKey, JBStr; // inline string pointer, string length
#else
typedef struct { unsigned int o, l; } JBKey, JBStr;	// offset pointer, string length
#endif
#elif defined(JB_INLINE_STRINGS)
typedef struct { const jchar *p; } JBKey, JBStr;	// inline string pointer
#else
typedef struct { unsigned int o; } JBKey, JBStr;	// offset pointer
#endif

#ifdef JB_64BIT_VALUES
typedef long long jbint;
typedef double jbfloat;
#else
typedef int jbint;
typedef float jbfloat;
#endif

// Stats returned from building a binary JSON
struct JBRet {
	unsigned int bin_size;		// array of JBItem + all strings. Not required for parsing.
	unsigned int bytes_read;	// how far into the original file the parsing occured
	unsigned int num_items;		// number of JSON items returned
	unsigned int text_size;		// bytes of text data (included in bin_size)
	unsigned int text_orig;		// size of text in original file (with duplication)
	unsigned int strings_count;	// number of shared strings
	unsigned int strings_orig;	// total number of strings
	JBError error_code;			// Look up error in JBError enum
	int err_line;				// if error this is the line number where stopped
	int err_column;				// if error this is the column (tabs counts as 1) where stopped
};

// JBItem JBIterator (forward only)
// This is an optional way to iterate over items that is more stl-style
struct JBIterator {
	const JBItem *ptr;

	JBIterator(const JBItem *p) : ptr(p) {} // construct an JBIterator
	JBIterator() : ptr(NULL) {} // empty JBIterator
	bool valid() const { return ptr != 0; } // check if this JBIterator is valid
	JBIterator& operator++(); // iterate JBIterator
	JBIterator successor() const; // get successor to this item
	bool has_successor() const; // check if there are successors to this item
	JBIterator child() const; // get child item JBIterator (valid if this is a hierarchical item and it has any children)
	const JBItem* operator*() { return ptr; } // get pointer to item by dereference
	const JBItem* operator->() { return ptr; } // get pointer to item by dereference
	const JBItem* get() { return ptr; } // get pointer to item
	bool operator==(const JBIterator &n) const { return n.ptr == ptr; } // compare equal operator
	bool operator!=(const JBIterator &n) const { return n.ptr != ptr; } // compare inequal operator
	operator bool() const { return ptr != NULL; } // bool operator is validity check for JBIterator
};

struct JBItem {
#ifdef JB_KEY_HASH
	unsigned int hash;
#endif
	JBType type : 8;		// json type
	int	sibling : 24;		// array offset to sibling (value nodes can only be 1 or 0)
#ifdef JB_KEY_STRING
	JBKey name;
#endif
	union {
		jbint	i;		// integer value
		jbfloat f;		// floating point value
		bool b;			// boolean value
		JBStr s;		// string value
	} data;

	// access data
	JBType getType() const { return type; }
#ifdef JB_KEY_HASH
	unsigned int getHash() const { return hash; }
#else
	unsigned int getHash() const;
#endif
#ifndef JB_KEY_STRING
	const char *getName() const;	// returns a printed number representing the hashed value of the string. this is not intended for multi threading, keeps its own static char buffer.
	unsigned int getNameLen() const { return 10; }	// "0x12345678"
#else
#ifdef JB_INLINE_STRINGS
	const jchar *getName() const { return name.p; }
#else
	const jchar *getName() const { return name.o ? (const jchar*)((const char*)&name.o + name.o) : NULL; }
#endif
#ifdef JB_STRLEN
	unsigned int getNameLen() const { return name.l; }
#else
	unsigned int getNameLen() const;
#endif
#endif
#ifdef JB_INLINE_STRINGS
	const jchar *getStr() const { return (type == JB_STRING) ? data.s.p : NULL; } // if value is string, get a null pointer or a zero terminated string pointer
#else
	const jchar *getStr() const { return (type == JB_STRING && data.s.o) ? (const jchar*)((const char*)&data + data.s.o) : NULL; } // if value is string, get a zero terminated string pointer
#endif
#ifdef JB_STRLEN
	unsigned int getStrLen() const { return (type == JB_STRING) ? data.s.l : 0; } // if value is string, get a null pointer or a zero terminated string pointer
#else
	unsigned int getStrLen() const;
#endif
	jbint getInt() const { return type == JB_INT ? data.i : (type == JB_FLOAT ? (jbint)data.f : 0); } // if value is number, get integer value or zero if not
	jbfloat getFloat() const { return type == JB_FLOAT ? data.f : (type == JB_INT ? (jbfloat)data.i : jbfloat(0)); } // if value is number, get floating point value or zero if not
	bool getBool() const { return type == JB_BOOL ? data.b : false; } // if value is bool, get bool value otherwise false
	const JBItem* getChild() const { return (this && data.i && (type == JB_ROOT || type == JB_OBJECT || type == JB_ARRAY)) ? this + 1 : 0;  }
	const JBItem* getSibling() const { return sibling ? (this + sibling) : NULL; }
	jbint getChildCount() const { return (this && (type == JB_ROOT || type == JB_OBJECT || type == JB_ARRAY)) ? data.i : 0; }

	// counts
	jbint size() const { return (type == JB_ARRAY || type == JB_ROOT || type == JB_OBJECT) ? data.i : 0; } // if this is an array or object or root, get number of (child) elements

	static JBIterator end() { return JBIterator(NULL); } // end JBIterator is NULL pointer
	JBIterator begin() const { return ((type==JB_ARRAY || type==JB_OBJECT || type==JB_ROOT) && data.i) ? JBIterator(this+1) : JBIterator(); } // if this is the root, an object or an array, return first child as an JBIterator

	const JBItem* findByHash(unsigned int hash) const;	// get a child item by hashed name (NULL if not found)
};

// inlined JBIterator member functions
inline JBIterator& JBIterator::operator++() { ptr = ptr->sibling ? ptr + ptr->sibling : NULL; return *this; }
inline JBIterator JBIterator::successor() const { return (ptr&&ptr->sibling) ? JBIterator(ptr + ptr->sibling) : JBIterator(); }
inline bool JBIterator::has_successor() const { return ptr->sibling != 0; }
inline JBIterator JBIterator::child() const { return JBIterator(ptr->getChild()); }

}	// namespace jsonbin

#endif

