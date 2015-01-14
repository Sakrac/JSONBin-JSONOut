//
// test code for numeric ranges
// there is no guarantee that this sample really does what it claims
//
// Requires only JSONOut
//
// NOTE: The resulting file won't successfully parse with JSONBin unless
// JB_64BIT_VALUES is defined as the numbers are out of range for 32 bit
// representation. This sample does not attempt to read back the JSON.
//
//=========================================================================
// Coding style is not representative of a real product, it is kept simple
//      to improve readability and avoid confusing dependencies.
//=========================================================================

#include <stdio.h>
#include <math.h>
#include <float.h>
#include <stdint.h>
#include "../jsonout/jsonout.h"

#ifdef WIN32
#define snprintf sprintf_s
#endif

static void NumberTest(const char *filename)
{
	FILE *f = 0;
#ifdef WIN32
	if (!fopen_s(&f, filename, "w")) {
#else
	if ((f = fopen(filename, "w"))) {
#endif
		jout::JSONOut o(f);

		o.push_object("floats");
		o.push("zero", 0.0f);
		o.push("max", FLT_MAX);
		o.push("min", FLT_MIN);
		for (int exp = FLT_MIN_10_EXP; exp < FLT_MAX_10_EXP; exp++) {
			char buf[32];
			snprintf(buf, sizeof(buf), "exp(%d)", exp);
			o.push(buf, 1.2345678912345f * powf(10.0f, float(exp)));
		}
		o.scope_end();

		o.push_object("doubles");
		o.push("zero", 0.0);
		o.push("max", DBL_MAX);
		o.push("min", DBL_MIN);
		for (int exp = DBL_MIN_10_EXP; exp < DBL_MAX_10_EXP; exp++) {
			char buf[32];
			snprintf(buf, sizeof(buf), "exp(%d)", exp);
			o.push(buf, 1.2345678901234567890123 * pow(10.0, exp));
		}
		o.scope_end();

		o.push_object("ints");
		o.push("zero", 0);
		o.push("max", INT32_MAX);
		o.push("min", INT32_MIN+1);
		for (int shift = 0; shift<32; shift++) {
			char buf[32];
			snprintf(buf, sizeof(buf), "shift(%d)", shift);
			o.push(buf, int(0xffffffff>>(31-shift)));
		}
		o.scope_end();

		o.push_object("longlongs");
		o.push("zero", 0);
		o.push("max", INT64_MAX);
		o.push("min", INT64_MIN+1);
		unsigned long long all_set = (unsigned long long)0 - 1;
		for (int shift = 0; shift<62; shift++) {
			char buf[32];
			snprintf(buf, sizeof(buf), "shift(%d)", shift);
			o.push(buf, (long long)(all_set >> (63 - shift)));
		}
		o.scope_end();
		o.finish();
		fclose(f);
	}
}

int main(int argc, char **argv)
{
	NumberTest("numbers.json");
	return 0;
}