//
// JSONOut
//
//
// Summary
//	- JSONOut (struct) is used to maintain an output to a json file
//	- Keeps a small memory buffer and writes that to the file as
//		it fills up.
//	- No internal allocations (except what the file system might do)
//
// Usage
//	- First create a jout::JSONOut structure to pass around;
//		new JSONOut or simply create on stack (a bit over than 4 kb).
//		You'll need to pass in a FILE pointer, either in the constructor or
//		by calling set_file().
//	- Call 'push' or 'element' functions to add values, objects or arrays.
//		push functions take a name and a value, element functions requires
//		an array and only takes values.
//	- 'element' usage can be replaced with 'push', 'element' functions are
//		provided to distinguish code writing arrays and code writing objects
//		and the name argument passed in to push will just be ignored.
//	- To add an object, call push_object(name) or element_object() if creating
//		an object within an array.
//	- To add an array, call push_array(name) or element_array() if creating
//		an array within an array.
//	- To close an object or an array call scope_end(), or close_array()/close_object()
//	- To finish call finish.
//	- If an error occurs push/element function will return false and
//		nothing further will be added to the file. It is safe to check
//		for errors only occasionally or at the end. Call last_error()
//		to get an error code (JSONOutError)
//
// Optional compiled traits: (comment in/out defines or specify as precompiled flags)
//	- JO_SUPPORT_WCHAR: Enable wchar_t as arguments to push/element functions
//	- JO_FAST_FRAC: Since snprintf can be really slow with a massive amount
//		if floating point printing JSONOut uses an internal floating point to
//		string function by default. This can be disabled.
//
// Notes
//	- Users changing code to fit a purpose is encouraged over supplying
//		a solution containing every possible suggestion and lots of bugs.
//	- If direct file IO isn't desired it should be trivial to modify
//		JSONOut::write_buf().
//
// License
//	Public Domain; no warranty implied; use at your own risk; attribution appreciated.
//	In no event will the authors be held liable for any damages arising from the use of this software.
//	Created by Carl-Henrik Skårstedt (#Sakrac)
//	Version 1.0
//
//

#ifndef _jsonout_h
#define _jsonout_h

#define JO_SUPPORT_WCHAR	// enable wchar_t support (saved JSON will still be utf-8, but no conversion needed for function calls)
#define JO_FAST_FRAC		// instead of using snprintf use a faster way to output numbers as text (snprintf is really slow)

namespace jout {

// a simple bitset class for tracking hierarchy context of json output
template<unsigned int S, class T = unsigned char> class JOBitSet {
	static const unsigned int TB = 8 * sizeof(T);	// size of one word of bits
	T b[(S + TB - 1) / TB];							// enough words to keep all bits
public:
	void clear(unsigned int i) { b[i / TB] &= ~(T(1) << i % TB); }
	void set(unsigned int i) { b[i / TB] |= T(1) << i % TB; }
	bool operator[](unsigned int i) const { return !!(b[i / TB] & (T(1) << (i % TB)));; }
};

struct JSONOut {
	enum { MAX_INDENT_LENGTH = 32 };
	enum JSONOutError {
		ERR_NONE,			// all is well
		ERR_NO_FILE,		// trying to push items before setting the file
		ERR_NOT_ARRAY,		// attempting to add an array element without an array
		ERR_OPEN_ARRAY,		// calling finish without closing an array
		ERR_OPEN_OBJECT,	// calling finish without closing an object
		ERR_TOO_DEEP,		// the hierarchy is deeper, edit MAX_JSONOUT_DEPTH for more
	};
		
	enum { 
		MAX_JSONOUT_DEPTH = 256,		// increase if incredibly deep hierarchy
		JO_FILE_BUFFER_SIZE = 4096,		// adjust to best fit IO access / memory usage
		MAX_JSONOUT_ARRAY_LINE = 200,	// approximate column to break long arrays
	};

	enum AddType {
		JO_NONE,
		JO_OBJECT,
		JO_OBJECT_END,
		JO_ARRAY,
		JO_ARRAY_END,
		JO_STRING,
		JO_NUMBER,
		JO_BOOL,
		JO_NULL
	};

	bool write_buf();			// save file buffer to file. adjust this function to change fwrite behavior.
	bool error(JSONOutError err) { error_cause = err; /*assert(true);*/ return false; }	// replace assert if desired

	bool add_char(char c);		// add one character
	bool new_line();			// add a newline
	bool next_line();			// add a separator if necessary and a new line
	bool next_line_indent();	// next_line and indent for the next object/value
	bool next_element();		// add an array separator if necessary
	bool add_indent();			// add indent following a newline
	bool add_string(const char *str, size_t len);		// add text without quotes
	bool add_quote_str(const char *str, size_t len);	// add text within quotes
	bool add_quote_str(const char *str);			// add text within quotes (zero terminated)
#ifdef JO_SUPPORT_WCHAR
	bool add_string(const wchar_t *str, size_t len);
	bool add_quote_str(const wchar_t *str, size_t len);
	bool add_quote_str(const wchar_t *str);			// add text within quotes (zero terminated)
#endif

	void *f;					// file to export to, really FILE* in the basic implementation
	int hier_depth;
	int indent_len;				// how many characters in one indentation
	int column;					// which is the current character offset from the previous newline
	int buf_cur;						// amount of characters added to the file buffer
	JSONOutError error_cause;
	AddType prev_type;			// previous JSON type to be added
	char indent[MAX_INDENT_LENGTH];	// user definable indentation
	JOBitSet<MAX_JSONOUT_DEPTH> hasValue;	// tracking whether a value has been added to the current hierarchical depth
	JOBitSet<MAX_JSONOUT_DEPTH> isArray;	// tracking if the current hierarchical depth is an array or an object
	char aFileBuf[JO_FILE_BUFFER_SIZE];	// the filebuffer

	JSONOut();
	JSONOut(void *FILE_out);
	void set_file(void *FILE_out);
	void set_indent(const char *spacing);
	bool finish(); // call this after adding all values to write out remaining buffer to file
	bool inArray() const { return isArray[hier_depth]; }
	int depth() const { return hier_depth; }
	void reset();
	JSONOutError last_error() const { return error_cause; }

	// push values
	bool push(const char *name, const char *value);	// push a string value
	bool push(const char *name, const char *value, int length); // push a string value with a given length
	bool push(const char *name, int value); // push a 32 bit integer value
	bool push(const char *name, long long value); // push a 64 bit integer value
	bool push(const char *name, float value); // push a 32 bit floating point value
	bool push(const char *name, double value); // push a 64 bit floating point value
	bool push(const char *name, bool value); // push a bool value
	bool push_null(const char *name); // push a null or a null value
	bool push_null(); // push a null
	bool push_array(const char *name); // push a new array
	bool push_object(const char *name); // push a new object

	// value array elements
	bool element(const char* value); // add a string element to an array (zero terminated)
	bool element(const char* value, int length); // add a string elelemnt to an array
	bool element(int value); // add an integer element to an array
	bool element(long long value); // add an integer element to an array
	bool element(float value); // add a float element to an array
	bool element(double value); // add a float element to an array
	bool element(bool value); // add a boolean element to an array
	bool element_object(); // add an object as a value to an array (close with scope_end())
	bool element_array(); // add an array to an array (close with scope_end())
	bool element_null(); // add a null element to an array

#ifdef JO_SUPPORT_WCHAR
	bool push(const wchar_t *name, const wchar_t *value);	// push a string value
	bool push(const wchar_t *name, const wchar_t *value, int length); // push a string value with a given length
	bool push(const char *name, const wchar_t *value);	// push a string value
	bool push(const char *name, const wchar_t *value, int length); // push a string value with a given length
	bool push(const wchar_t *name, const char *value);	// push a string value
	bool push(const wchar_t *name, const char *value, int length); // push a string value with a given length
	bool push(const wchar_t *name, int value); // push a 32 bit integer value
	bool push(const wchar_t *name, long long value); // push a 64 bit integer value
	bool push(const wchar_t *name, float value); // push a 32 bit floating point value
	bool push(const wchar_t *name, double value); // push a 64 bit floating point value
	bool push(const wchar_t *name, bool value); // push a bool value
	bool push_null(const wchar_t *name); // push a null or a null value
	bool push_array(const wchar_t *name);
	bool push_object(const wchar_t *name); // push a new object

	bool element(const wchar_t* value); // add a string element to an array (zero terminated)
	bool element(const wchar_t* value, int length); // add a string elelemnt to an array
#endif

	// close a scope (object or array)
	bool scope_end();
	bool close_array() { return scope_end(); }	// close an array
	bool close_object() { return scope_end(); }	// close an object
};

}
#endif
