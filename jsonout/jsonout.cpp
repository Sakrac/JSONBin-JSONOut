//
// JSONOut
//
// Details in jsonout.h
//

#include <stdio.h>
#include <string.h>
#include "jsonout.h"
#include "math.h"
#include "float.h"
#include "wchar.h"

namespace jout {

#ifdef WIN32
#define snprintf sprintf_s
#endif

bool JSONOut::write_buf()
{
	if (f) {
		if (buf_cur)
			fwrite(aFileBuf, buf_cur, 1, (FILE*)f);
		buf_cur = 0;
		return true;
	}
	return error(ERR_NO_FILE);
}

JSONOut::JSONOut(bool rootArray) : f(0), hier_depth(0), indent_len(2), buf_cur(0), error_cause(ERR_NONE)
{
	indent[0] = ' '; indent[1] = ' '; indent[2] = 0;
	reset(rootArray);
}
JSONOut::JSONOut(void *FILE_out, bool rootArray) : f(FILE_out), hier_depth(0), indent_len(2), buf_cur(0), error_cause(ERR_NONE)
{
	indent[0] = ' '; indent[1] = ' '; indent[2] = 0;
	reset(rootArray);
}

void JSONOut::reset(bool rootArray)
{
	hier_depth = 1;
	hasValue.clear(0);
	hasValue.clear(1);
	buf_cur = 0;
	if (rootArray) {
#ifdef JO_ALLOW_ROOT_ARRAY
		isArray.clear(0);
		isArray.clear(1);
		add_char('[');
#else
		error(ERR_ROOT_ARRAY);
#endif
	} else {
		isArray.clear(0);
		isArray.clear(1);
		add_char('{');
	}
	prev_type = JO_NONE;
}

bool JSONOut::set_rootArray()
{
#ifdef JO_ALLOW_ROOT_ARRAY
	if (hier_depth==1) {
		isArray.set(0);
		isArray.set(1);
		prev_type = JO_ARRAY;
		buf_cur = 0;
		add_char('[');
		return true;
	}
#endif
	return error(ERR_ROOT_ARRAY);
}

void JSONOut::set_file(void *FILE_out)
{
	f = FILE_out;
}

void JSONOut::set_indent(const char *spacing)
{
	size_t len = strlen(spacing);
	if (len>(sizeof(indent)-1))
		len = sizeof(indent)-1;

	memcpy(indent, spacing, len);
	indent[len] = 0;
}

bool JSONOut::add_char(char c)
{
	if (buf_cur>=JO_FILE_BUFFER_SIZE) {
		if (!write_buf())
			return false;
	}
	aFileBuf[buf_cur++] = c;
	column++;
	return true;

}

bool JSONOut::new_line()
{
	bool ret = add_char('\n');
	column = 0;
	return ret;
}

bool JSONOut::add_indent()
{
	for (int ind=0; ind<hier_depth; ind++) {
		if (buf_cur>=(JO_FILE_BUFFER_SIZE-indent_len)) {
			if (!write_buf())
				return false;
		}
		memcpy(aFileBuf+buf_cur, indent, indent_len);
		buf_cur += indent_len;
	}
	column += hier_depth * indent_len;
	return true;
}

bool JSONOut::next_line()
{
	if (hasValue[hier_depth]) {
		if (!add_char(','))
			return false;
	}
	return new_line();
}

bool JSONOut::next_line_indent()
{
	if (hasValue[hier_depth]) {
		if (!add_char(','))
			return false;
	}
	if (!new_line())
		return false;
	return add_indent();
}

bool JSONOut::next_element()
{
	if (hasValue[hier_depth]) {
		if (!add_char(','))
			return false;
	}
	if (column<MAX_JSONOUT_ARRAY_LINE)
		return add_char(' ');
	return new_line() && add_indent();
}

bool JSONOut::add_string(const char *str, size_t len)
{
	char custom[16];
	column += (int)len;
	while (len) {
		while (len) {
			len--;
			const char *w = str;
			int wl = 1;
			switch (unsigned char c = (unsigned char)*str++) {
				case '\b': w = "\\b";  wl = 2; break;
				case '\t': w = "\\t";  wl = 2; break;
				case '\n': w = "\\n";  wl = 2; break;
				case '\f': w = "\\f";  wl = 2; break;
				case '\r': w = "\\r";  wl = 2; break;
				case '\"': w = "\\\""; wl = 2; break;
				case '\\': w = "\\\\"; wl = 2; break;
				default:
					if (c<' ') {	// JSON is both UTF-8 compliant AND has its own U'coding so simply leave output as UTF-8 for readability
						w = custom;
						wl = snprintf(custom, sizeof(custom), "\\u%04x", c);
					}
					break;
			}
			while (wl--) {
				if (buf_cur >= JO_FILE_BUFFER_SIZE && !write_buf())
					return false;
				aFileBuf[buf_cur++] = *w++;
				column++;
			}
		}
	}
	return true;
}

bool JSONOut::add_quote_str(const char *str, size_t len)
{
	if (!add_char('"'))
		return false;
	if (!add_string(str, len))
		return false;
	return add_char('"');
}

bool JSONOut::add_quote_str(const char *str)
{
	if (!add_char('"'))
		return false;
	if (!add_string(str, str ? (int)strlen(str) : 0))
		return false;
	return add_char('"');
}

bool JSONOut::scope_end()
{
	if (error_cause != ERR_NONE)
		return false;
	if (isArray[hier_depth]) {
		if ((hasValue[hier_depth--] && column > MAX_JSONOUT_ARRAY_LINE) || prev_type == JO_ARRAY_END || prev_type==JO_OBJECT_END) {
			if (!new_line())
				return false;
			if (!add_indent())
				return false;
		}
		else if (!add_char(' '))
			return false;
		prev_type = JO_ARRAY_END;
		return add_char(']');
	}
	if (hasValue[hier_depth--] || prev_type == JO_ARRAY_END || prev_type==JO_OBJECT_END) {
		if (!new_line())
			return false;
		if (!add_indent())
			return false;
	} else if (!add_char(' '))
		return false;
	prev_type = JO_OBJECT_END;
	return add_char('}');
}

#ifdef JO_FAST_FRAC

static const char* double2text(double v, char *buf, size_t buf_size, int precision=17)
{
	if (isnan(v))
		return "0.0";	// json does not have a concept of NaN, probably not a great solution but better than printing out garbage.

	bool neg = v < 0.0;
	if (neg)
		v = -v;

	int exp = fabs(v)>DBL_MIN ? int(log10(fabs(v))) : 0;	// log10(0) is undefined, in this case I only care that it requires 0 digits to represent.
	int frac = precision;
	unsigned long long n = 0;		// integer portion
	double f = 0.0;					// fractional portion
	if (exp > (precision-2) || exp < -2) {	// consider exponent numbers if large or small for easier reading
		double ve = v / pow(10.0, exp); // shift the value to be centered around 0
		n = (long long)ve;			// this will be roughly 1-9 with sign
		f = ve - double(n);			// fraction
		frac = precision;			// number of digits after '.'
	} else {
		frac = precision - 1 - exp;	// between 1 and 18 digits (greatest if 0.00xxxx) will fit into a ULL
		if (frac < 1) frac = 1;
		if (frac > (precision+1)) frac = precision+1;
		exp = 0;					// no exponent
		n = (long long)v;			// up to ~10^15
		f = v - double(n);			// fraction
	}

	if (neg) {	// add - sign if number is negative
		*buf++ = '-';
		buf_size--;
	}

	// integer!
	{
		unsigned int nc = 1;	// number of characters
		unsigned long long ncc = 10; // number of character comparison value
		while (n >= ncc) {
			nc++;
			ncc *= 10;
		}
		if (nc >= buf_size)
			return "";
		for (int o = nc - 1; o >= 0; --o) {
			buf[o] = (n % 10) + '0';
			n /= 10;
		}
		buf += nc;
		buf_size -= nc;
	}

	// fraction?
	if (frac > 0) {
		if (buf_size < (unsigned int)(frac + 1))
			return "";
		*buf++ = '.';	// add '.'
		buf_size--;

		unsigned long long fn = (unsigned long long)(f * pow(10.0, frac) + 0.49);
		for (int o = frac - 1; o >= 0; --o) {
			buf[o] = (fn % 10)+'0';
			fn /= 10;
		}
		buf += frac;
		buf_size -= frac;
	}

	// exponent?
	if (exp && buf_size >= 2) {
		unsigned int ec = 1;	// number of characters
		int ecc = 10;			// number of character comparison value
		*buf++ = 'e';
		buf_size--;
		if (exp < 0) {
			*buf++ = '-';
			buf_size--;
			exp = -exp;
		}
		while (exp >= ecc) {
			ec++;
			ecc *= 10;
		}
		if (buf_size < ec)
			return "";
		for (int o = ec - 1; o >= 0; --o) {
			buf[o] = (exp % 10) + '0';
			exp /= 10;
		}
		buf += ec;
		buf_size -= ec;
	}

	if (buf_size)
		*buf++ = 0;
	return buf;
}
#else
// helper formatting strings to get full precision of a float or double
static const char *dblformstr(double v, char *buf, size_t buf_size)
{
	int exp = fabs(v)>DBL_MIN ? int(log10(fabs(v))) : 0;
	if (exp > 15 || exp < -2)
		snprintf(buf, buf_size, "%%.17le");
	else
		snprintf(buf, buf_size, "%%#.%dlf", 16 - exp);
	return buf;
}

static const char *fltformstr(float v, char *buf, size_t buf_size)
{
	int exp = fabsf(v)>FLT_MIN ? int(log10f(fabsf(v))) : 0;
	if (exp > 8 || exp < -2)
		snprintf(buf, buf_size, "%%.10e");
	else
		snprintf(buf, buf_size, "%%#.%df", 8 - exp);
	return buf;
}
#endif

// remove extraneous 0's at end of floating point strings
static const char *cleanfloatstr(char *buf)
{
	const char* dot = 0;
	const char* e = 0;
	char* last0 = 0;
	char *b = buf;
	while (char c = *b) {
		if (c == '.')
			dot = b;
		else if (c == 'e' || c=='E')
			e = b;
		if (c != '0')
			last0 = 0;
		else if (!last0)
			last0 = b;
		b++;
	}
	if (dot && !e && last0 && last0 > dot) {
		if (last0 == (dot + 1))
			*(last0 + 1) = 0;
		else
			*last0 = 0;
	}
	return buf;
}

bool JSONOut::push(const char *name, const char *value, int length)
{
	if (error_cause != ERR_NONE)
		return false;
	if (inArray()) {
		if (next_element() && add_quote_str(value, length)) {
			hasValue.set(hier_depth);
			prev_type = JO_STRING;
			return true;
		}
	} else {
		if (next_line_indent() && add_quote_str(name) && add_string(" : ", 3) && add_quote_str(value, length)) {
			hasValue.set(hier_depth);
			prev_type = JO_STRING;
			return true;
		}
	}
	return false;
}


bool JSONOut::push(const char *name, const char *value)
{
	return push(name, value, int(strlen(value)));
}

bool JSONOut::push(const char *name, int value)
{
	if (error_cause != ERR_NONE)
		return false;
	char valstr[16];
	snprintf(valstr, sizeof(valstr), "%d", value);

	if (inArray()) {
		if (next_element() && add_string(valstr, strlen(valstr))) {
			hasValue.set(hier_depth);
			prev_type = JO_NUMBER;
			return true;
		}
	} else if (next_line_indent() && add_quote_str(name) && add_string(" : ", 3) && add_string(valstr, strlen(valstr))) {
		hasValue.set(hier_depth);
		prev_type = JO_NUMBER;
		return true;
	}
	return false;
}

bool JSONOut::push(const char *name, long long value)
{
	if (error_cause != ERR_NONE)
		return false;
	char valstr[32];
	snprintf(valstr, sizeof(valstr), "%lld", value);

	if (inArray()) {
		if (next_element() && add_string(valstr, strlen(valstr))) {
			hasValue.set(hier_depth);
			prev_type = JO_NUMBER;
			return true;
		}
	} else if (next_line_indent() && add_quote_str(name) && add_string(" : ", 3) && add_string(valstr, strlen(valstr))) {
		hasValue.set(hier_depth);
		prev_type = JO_NUMBER;
		return true;
	}
	return false;
}

bool JSONOut::push(const char *name, float value)
{
	if (error_cause != ERR_NONE)
		return false;

	char valstr[64];
#ifdef JO_FAST_FRAC
	double2text((double)value, valstr, sizeof(valstr), 10);
#else
	char formstr[16];
	snprintf(valstr, sizeof(valstr), fltformstr(value, formstr, sizeof(formstr)), value);
#endif
	cleanfloatstr(valstr);

	if (inArray()) {
		if (next_element() && add_string(valstr, strlen(valstr))) {
			hasValue.set(hier_depth);
			prev_type = JO_NUMBER;
			return true;
		}
	} else if (next_line_indent() && add_quote_str(name) && add_string(" : ", 3) && add_string(valstr, strlen(valstr))) {
		hasValue.set(hier_depth);
		prev_type = JO_NUMBER;
		return true;
	}
	return false;
}

bool JSONOut::push(const char *name, double value)
{
	if (error_cause != ERR_NONE)
		return false;
	char valstr[64];
#ifdef JO_FAST_FRAC
	double2text((double)value, valstr, sizeof(valstr));
#else
	char formstr[16];
	snprintf(valstr, sizeof(valstr), dblformstr(value, formstr, sizeof(formstr)), value);
#endif
	cleanfloatstr(valstr);

	if (inArray()) {
		if (next_element() && add_string(valstr, strlen(valstr))) {
			hasValue.set(hier_depth);
			prev_type = JO_NUMBER;
			return true;
		}
	} else if(next_line_indent() && add_quote_str(name) && add_string(" : ", 3) && add_string(valstr, strlen(valstr))) {
		hasValue.set(hier_depth);
		prev_type = JO_NUMBER;
		return true;
	}
	return false;
}

bool JSONOut::push(const char *name, bool value)
{
	if (error_cause != ERR_NONE)
		return false;
	if (inArray()) {
		if (next_element() && add_string(value ? "true" : "false", value ? 4 : 5)) {
			hasValue.set(hier_depth);
			prev_type = JO_NUMBER;
			return true;
		}
	} else if (next_line_indent() && add_quote_str(name) && add_string(" : ", 3) && add_string(value ? "true" : "false", value ? 4 : 5)) {
		hasValue.set(hier_depth);
		prev_type = JO_BOOL;
		return true;
	}
	return false;
}

bool JSONOut::push_null(const char *name) // add an object with a null value
{
	if (error_cause != ERR_NONE)
		return false;
	if (inArray()) {
		if (next_element() && add_string("null", 4)) {
			hasValue.set(hier_depth);
			prev_type = JO_NULL;
			return true;
		}
	}
	else if (next_line_indent() && (!name || (add_quote_str(name) && add_string(" : ", 3))) && add_string("null", 4)) {
		hasValue.set(hier_depth);
		prev_type = JO_NULL;
		return true;
	}
	return false;
}

bool JSONOut::push_null() // add an object with a null value
{
	if (error_cause != ERR_NONE)
		return false;
	if (inArray()) {
		if (next_element() && add_string("null", 4)) {
			hasValue.set(hier_depth);
			prev_type = JO_NULL;
			return true;
		}
	}
	else if (next_line_indent() && add_string("null", 4)) {
		hasValue.set(hier_depth);
		prev_type = JO_NULL;
		return true;
	}
	return false;
}

bool JSONOut::push_object(const char *name)
{
	if (error_cause != ERR_NONE)
		return false;
	if (isArray[hier_depth]) {
		if (!next_line_indent())
			return false;
		hasValue.set(hier_depth);
		++hier_depth;
		if (hier_depth >= MAX_JSONOUT_DEPTH)
			return error(ERR_TOO_DEEP);
		isArray.clear(hier_depth);
		hasValue.clear(hier_depth);
		prev_type = JO_OBJECT;
		return add_char('{');
	}
	if (!next_line_indent() || !add_quote_str(name))
		return false;

	hasValue.set(hier_depth);
	++hier_depth;
	if (hier_depth >= MAX_JSONOUT_DEPTH)
		return error(ERR_TOO_DEEP);
	hasValue.clear(hier_depth);
	isArray.clear(hier_depth);
	prev_type = JO_OBJECT;
	return add_string(" : {", 4);
}


bool JSONOut::push_array(const char *name)
{
	if (error_cause != ERR_NONE)
		return false;
	if (isArray[hier_depth]) {
		if (!next_line_indent())
			return false;
		hasValue.set(hier_depth);
		++hier_depth;
		if (hier_depth >= MAX_JSONOUT_DEPTH)
			return error(ERR_TOO_DEEP);
		hasValue.clear(hier_depth);
		isArray.set(hier_depth);
		prev_type = JO_ARRAY;
		return add_char('[');
	}
	if (!next_line_indent() || !add_quote_str(name))
		return false;
	hasValue.set(hier_depth);
	++hier_depth;
	if (hier_depth >= MAX_JSONOUT_DEPTH)
		return error(ERR_TOO_DEEP);
	hasValue.clear(hier_depth);
	isArray.set(hier_depth);
	prev_type = JO_ARRAY;
	return add_string(" : [", 4);
}



// array elements
bool JSONOut::element(const char* value)
{
	if (error_cause != ERR_NONE)
		return false;
	if (!isArray[hier_depth])
		return error(ERR_NOT_ARRAY);

	if (next_element() && add_quote_str(value, strlen(value))) {
		hasValue.set(hier_depth);
		prev_type = JO_STRING;
		return true;
	}
	return false;
}

bool JSONOut::element(const char* value, int length)
{
	if (error_cause != ERR_NONE)
		return false;
	if (!isArray[hier_depth])
		return error(ERR_NOT_ARRAY);

	if (next_element() && add_quote_str(value, length)) {
		hasValue.set(hier_depth);
		prev_type = JO_STRING;
		return true;
	}
	return false;
}

bool JSONOut::element(int value)
{
	if (error_cause != ERR_NONE)
		return false;
	if (!isArray[hier_depth])
		return error(ERR_NOT_ARRAY);

	char valstr[16];
	snprintf(valstr, sizeof(valstr), "%d", value);

	if (next_element() && add_string(valstr, strlen(valstr))) {
		hasValue.set(hier_depth);
		prev_type = JO_NUMBER;
		return true;
	}
	return false;
}

bool JSONOut::element(long long value)
{
	if (error_cause != ERR_NONE)
		return false;
	if (!isArray[hier_depth])
		return error(ERR_NOT_ARRAY);

	char valstr[32];
	snprintf(valstr, sizeof(valstr), "%lld", value);

	if (next_element() && add_string(valstr, strlen(valstr))) {
		hasValue.set(hier_depth);
		prev_type = JO_NUMBER;
		return true;
	}
	return false;
}

bool JSONOut::element(float value)
{
	if (error_cause != ERR_NONE)
		return false;
	if (!isArray[hier_depth])
		return error(ERR_NOT_ARRAY);

	char valstr[64];
#ifdef JO_FAST_FRAC
	double2text((double)value, valstr, sizeof(valstr), 10);
#else
	char formstr[16];
	snprintf(valstr, sizeof(valstr), fltformstr(value, formstr, sizeof(formstr)), value);
#endif
	cleanfloatstr(valstr);

	if (next_element() && add_string(valstr, strlen(valstr))) {
		hasValue.set(hier_depth);
		prev_type = JO_NUMBER;
		return true;
	}
	return false;
}

bool JSONOut::element(double value)
{
	if (error_cause != ERR_NONE)
		return false;
	if (!isArray[hier_depth])
		return error(ERR_NOT_ARRAY);

	char valstr[64];
#ifdef JO_FAST_FRAC
	double2text((double)value, valstr, sizeof(valstr));
#else
	char formstr[16];
	snprintf(valstr, sizeof(valstr), dblformstr(value, formstr, sizeof(formstr)), value);
#endif
	cleanfloatstr(valstr);

	if (next_element() && add_string(valstr, strlen(valstr))) {
		hasValue.set(hier_depth);
		prev_type = JO_NUMBER;
		return true;
	}
	return false;
}

bool JSONOut::element(bool value)
{
	if (error_cause != ERR_NONE)
		return false;
	if (!isArray[hier_depth])
		return error(ERR_NOT_ARRAY);

	if (next_element() && add_string(value ? "true" : "false", value ? 4 : 5)) {
		hasValue.set(hier_depth);
		prev_type = JO_BOOL;
		return true;
	}
	return false;
}

// the null value
bool JSONOut::element_null()
{
	if (error_cause != ERR_NONE)
		return false;
	if (!isArray[hier_depth])
		return error(ERR_NOT_ARRAY);

	if (!next_element())
		return false;

	if (add_string("null", 4)) {
		hasValue.set(hier_depth);
		prev_type = JO_NULL;
		return true;
	}
	return false;
}

bool JSONOut::element_object()
{
	if (error_cause != ERR_NONE)
		return false;
	if (!isArray[hier_depth])
		return error(ERR_NOT_ARRAY); // not currently in an array so can't recover
	if (!next_line_indent())
		return false;
	hasValue.set(hier_depth);
	++hier_depth;
	if (hier_depth >= MAX_JSONOUT_DEPTH)
		return error(ERR_TOO_DEEP);
	isArray.clear(hier_depth);
	hasValue.clear(hier_depth);
	prev_type = JO_OBJECT;
	return add_char('{');
}

bool JSONOut::element_array()
{
	if (error_cause != ERR_NONE)
		return false;
	if (!isArray[hier_depth])
		return error(ERR_NOT_ARRAY);
	if (!next_line_indent())
		return false;
	hasValue.set(hier_depth);
	++hier_depth;
	if (hier_depth >= MAX_JSONOUT_DEPTH)
		return error(ERR_TOO_DEEP);
	hasValue.clear(hier_depth);
	isArray.set(hier_depth);
	prev_type = JO_ARRAY;
	return add_char('[');
}

#ifdef JO_SUPPORT_WCHAR

bool JSONOut::add_string(const wchar_t *str, size_t len)
{
	char custom[16];
	column += (int)len;
	while (len) {
		while (len) {
			len--;
			const char *w = custom;
			int wl = 1;
			switch (unsigned int c = (unsigned int)*str++) {
				case '\b': w = "\\b"; wl = 2; break;
				case '\t': w = "\\t"; wl = 2; break;
				case '\n': w = "\\n"; wl = 2; break;
				case '\f': w = "\\f"; wl = 2; break;
				case '\r': w = "\\r"; wl = 2; break;
				case '\"': w = "\\\""; wl = 2; break;
				case '\\': w = "\\\\"; wl = 2; break;	// note: '/' is valid JSON string as is so no need to complicate it
				default:
					if (c<' ')	// JSON is both UTF-8 compliant AND has its own U'coding so simply leave output as UTF-8 for readability
						wl = snprintf(custom, sizeof(custom) / sizeof(custom[0]), "\\u%04x", c);
					else {	// convert wchar_t assumed to be utf-16 to utf-8
						if (c >= 0xd800 && c < 0xe000 && len && *str>=0xdc00 && *str<0xe000) {
							c = (((c & 0x3ff) << 10) || ((*str++) & 0x3ff)) + 0x10000;	// c >= 0x10000
							len--;
							custom[0] = 0xf0 | ((c >> 18) & 7);
							custom[1] = 0x80 | ((c >> 12) & 0x3f);
							custom[2] = 0x80 | ((c >> 6) & 0x3f);
							custom[3] = 0x80 | (c & 0x3f);
							wl = 4;
						} else if (c < 0x80) { // c < 0x10000
							*custom = (char)c;
							wl = 1;
						} else if (c < 0x800) {
							custom[0] = 0xc0 | (c >> 6);
							custom[1] = 0x80 | (c & 0x3f);
							wl = 2;
						} else {
							custom[0] = 0xe0 | (c >> 12);
							custom[1] = 0x80 | ((c >> 6) & 0x3f);
							custom[2] = 0x80 | (c & 0x3f);
							wl = 3;
						}
					}
					break;
			}
			while (wl--) {
				if (buf_cur >= JO_FILE_BUFFER_SIZE && !write_buf())
					return false;
				aFileBuf[buf_cur++] = *w++;
				column++;
			}
		}
	}
	return true;
}

bool JSONOut::add_quote_str(const wchar_t *str, size_t len)
{
	if (!add_char('"'))
		return false;
	if (!add_string(str, len))
		return false;
	return add_char('"');
}

bool JSONOut::add_quote_str(const wchar_t *str)
{
	if (!add_char('"'))
		return false;
	if (!add_string(str, str ? (int)wcslen(str) : 0))
		return false;
	return add_char('"');
}

bool JSONOut::push(const wchar_t *name, const wchar_t *value, int length)
{
	if (error_cause != ERR_NONE)
		return false;
	if (inArray()) {
		if (next_element() && add_quote_str(value, length)) {
			hasValue.set(hier_depth);
			prev_type = JO_STRING;
			return true;
		}
	}
	else {
		if (next_line_indent() && add_quote_str(name) && add_string(" : ", 3) && add_quote_str(value, length)) {
			hasValue.set(hier_depth);
			prev_type = JO_STRING;
			return true;
		}
	}
	return false;
}


bool JSONOut::push(const wchar_t *name, const wchar_t *value)
{
	return push(name, value, int(wcslen(value)));
}

bool JSONOut::push(const char *name, const wchar_t *value, int length)
{
	if (error_cause != ERR_NONE)
		return false;
	if (inArray()) {
		if (next_element() && add_quote_str(value, length)) {
			hasValue.set(hier_depth);
			prev_type = JO_STRING;
			return true;
		}
	}
	else {
		if (next_line_indent() && add_quote_str(name) && add_string(" : ", 3) && add_quote_str(value, length)) {
			hasValue.set(hier_depth);
			prev_type = JO_STRING;
			return true;
		}
	}
	return false;
}


bool JSONOut::push(const char *name, const wchar_t *value)
{
	return push(name, value, int(wcslen(value)));
}

bool JSONOut::push(const wchar_t *name, const char *value, int length)
{
	if (error_cause != ERR_NONE)
		return false;
	if (inArray()) {
		if (next_element() && add_quote_str(value, length)) {
			hasValue.set(hier_depth);
			prev_type = JO_STRING;
			return true;
		}
	}
	else {
		if (next_line_indent() && add_quote_str(name) && add_string(" : ", 3) && add_quote_str(value, length)) {
			hasValue.set(hier_depth);
			prev_type = JO_STRING;
			return true;
		}
	}
	return false;
}


bool JSONOut::push(const wchar_t *name, const char *value)
{
	return push(name, value, int(strlen(value)));
}

bool JSONOut::push(const wchar_t *name, int value)
{
	if (error_cause != ERR_NONE)
		return false;
	char valstr[16];
	snprintf(valstr, sizeof(valstr), "%d", value);

	if (inArray()) {
		if (next_element() && add_string(valstr, strlen(valstr))) {
			hasValue.set(hier_depth);
			prev_type = JO_NUMBER;
			return true;
		}
	}
	else if (next_line_indent() && add_quote_str(name) && add_string(" : ", 3) && add_string(valstr, strlen(valstr))) {
		hasValue.set(hier_depth);
		prev_type = JO_NUMBER;
		return true;
	}
	return false;
}

bool JSONOut::push(const wchar_t *name, long long value)
{
	if (error_cause != ERR_NONE)
		return false;
	char valstr[32];
	snprintf(valstr, sizeof(valstr), "%lld", value);

	if (inArray()) {
		if (next_element() && add_string(valstr, strlen(valstr))) {
			hasValue.set(hier_depth);
			prev_type = JO_NUMBER;
			return true;
		}
	}
	else if (next_line_indent() && add_quote_str(name) && add_string(" : ", 3) && add_string(valstr, strlen(valstr))) {
		hasValue.set(hier_depth);
		prev_type = JO_NUMBER;
		return true;
	}
	return false;
}

bool JSONOut::push(const wchar_t *name, float value)
{
	if (error_cause != ERR_NONE)
		return false;

	char valstr[64];
#ifdef JO_FAST_FRAC
	double2text((double)value, valstr, sizeof(valstr), 10);
#else
	char formstr[16];
	snprintf(valstr, sizeof(valstr), fltformstr(value, formstr, sizeof(formstr)), value);
#endif
	cleanfloatstr(valstr);

	if (inArray()) {
		if (next_element() && add_string(valstr, strlen(valstr))) {
			hasValue.set(hier_depth);
			prev_type = JO_NUMBER;
			return true;
		}
	}
	else if (next_line_indent() && add_quote_str(name) && add_string(" : ", 3) && add_string(valstr, strlen(valstr))) {
		hasValue.set(hier_depth);
		prev_type = JO_NUMBER;
		return true;
	}
	return false;
}

bool JSONOut::push(const wchar_t *name, double value)
{
	if (error_cause != ERR_NONE)
		return false;
	char valstr[64];
#ifdef JO_FAST_FRAC
	double2text((double)value, valstr, sizeof(valstr));
#else
	char formstr[16];
	snprintf(valstr, sizeof(valstr), dblformstr(value, formstr, sizeof(formstr)), value);
#endif
	cleanfloatstr(valstr);

	if (inArray()) {
		if (next_element() && add_string(valstr, strlen(valstr))) {
			hasValue.set(hier_depth);
			prev_type = JO_NUMBER;
			return true;
		}
	}
	else if (next_line_indent() && add_quote_str(name) && add_string(" : ", 3) && add_string(valstr, strlen(valstr))) {
		hasValue.set(hier_depth);
		prev_type = JO_NUMBER;
		return true;
	}
	return false;
}

bool JSONOut::push(const wchar_t *name, bool value)
{
	if (error_cause != ERR_NONE)
		return false;
	if (inArray()) {
		if (next_element() && add_string(value ? "true" : "false", value ? 4 : 5)) {
			hasValue.set(hier_depth);
			prev_type = JO_NUMBER;
			return true;
		}
	}
	else if (next_line_indent() && add_quote_str(name) && add_string(" : ", 3) && add_string(value ? "true" : "false", value ? 4 : 5)) {
		hasValue.set(hier_depth);
		prev_type = JO_BOOL;
		return true;
	}
	return false;
}

bool JSONOut::push_null(const wchar_t *name) // add an object with a null value
{
	if (error_cause != ERR_NONE)
		return false;
	if (inArray()) {
		if (next_element() && add_string("null", 4)) {
			hasValue.set(hier_depth);
			prev_type = JO_NULL;
			return true;
		}
	}
	else if (next_line_indent() && (!name || (add_quote_str(name) && add_string(" : ", 3))) && add_string("null", 4)) {
		hasValue.set(hier_depth);
		prev_type = JO_NULL;
		return true;
	}
	return false;
}


bool JSONOut::push_object(const wchar_t *name)
{
	if (error_cause != ERR_NONE)
		return false;
	if (isArray[hier_depth]) {
		if (!next_line_indent())
			return false;
		hasValue.set(hier_depth);
		++hier_depth;
		if (hier_depth >= MAX_JSONOUT_DEPTH)
			return error(ERR_TOO_DEEP);
		isArray.clear(hier_depth);
		hasValue.clear(hier_depth);
		prev_type = JO_OBJECT;
		return add_char('{');
	}
	if (!next_line_indent() || !add_quote_str(name))
		return false;

	hasValue.set(hier_depth);
	++hier_depth;
	if (hier_depth >= MAX_JSONOUT_DEPTH)
		return error(ERR_TOO_DEEP);
	hasValue.clear(hier_depth);
	isArray.clear(hier_depth);
	prev_type = JO_OBJECT;
	return add_string(" : {", 4);
}

bool JSONOut::push_array(const wchar_t *name)
{
	if (error_cause != ERR_NONE)
		return false;
	if (isArray[hier_depth]) {
		if (!next_line_indent())
			return false;
		hasValue.set(hier_depth);
		++hier_depth;
		if (hier_depth >= MAX_JSONOUT_DEPTH)
			return error(ERR_TOO_DEEP);
		hasValue.clear(hier_depth);
		isArray.set(hier_depth);
		prev_type = JO_ARRAY;
		return add_char('[');
	}
	if (!next_line_indent() || !add_quote_str(name))
		return false;
	hasValue.set(hier_depth);
	++hier_depth;
	if (hier_depth >= MAX_JSONOUT_DEPTH)
		return error(ERR_TOO_DEEP);
	hasValue.clear(hier_depth);
	isArray.set(hier_depth);
	prev_type = JO_ARRAY;
	return add_string(" : [", 4);
}

bool JSONOut::element(const wchar_t* value)
{
	if (error_cause != ERR_NONE)
		return false;
	if (!isArray[hier_depth])
		return error(ERR_NOT_ARRAY);

	if (next_element() && add_quote_str(value, wcslen(value))) {
		hasValue.set(hier_depth);
		prev_type = JO_STRING;
		return true;
	}
	return false;
}

bool JSONOut::element(const wchar_t* value, int length)
{
	if (error_cause != ERR_NONE)
		return false;
	if (!isArray[hier_depth])
		return error(ERR_NOT_ARRAY);

	if (next_element() && add_quote_str(value, length)) {
		hasValue.set(hier_depth);
		prev_type = JO_STRING;
		return true;
	}
	return false;
}



#endif


bool JSONOut::finish()
{
	if (!f)
		return true;	// didn't start so finish is ok

	if (error_cause != ERR_NONE)
		return false;

	if (hier_depth != 1)
		return error(isArray[hier_depth] ? ERR_OPEN_ARRAY : ERR_OPEN_OBJECT);	// didn't properly close all opened objects/arrays

	if (!new_line())
		return false;
#ifdef JO_ALLOW_ROOT_ARRAY
	if (isArray[0]) {
		if (!add_char(']')) // in case of array json add the final ']'
			return false;
	} else
#endif
	if (!add_char('}')) // add the final }
		return false;
	if (!new_line())
		return false;

	bool ret = write_buf();

	f = NULL;			// mark as uninitialized and clear the FILE pointer so it isn't accicendally reused
	hier_depth = 0;		// no root created
	hasValue.clear(0);
	prev_type = JO_NONE;

	return ret;
}

} // namespace jsonout