// fnv1a.cpp : Defines the entry point for the console application.
//

#ifdef WIN32
#include <tchar.h>
#endif
#include <stdio.h>
#include <string.h>

#define JB_FNV1A_PRIME 16777619	// as a default, FNV-1A is used for hash
#define JB_FNV1A_SEED 2166136261

static unsigned int fnv1A(const char *s, unsigned int l)
{
	unsigned const char *r = (unsigned const char*)s;
	unsigned int hash = JB_FNV1A_SEED;
	while (l--)
		hash = (*r++ ^ hash) * JB_FNV1A_PRIME;
	return hash;
}

#ifdef WIN32

// store one utf-8 or wchar character
static int asUTF8(unsigned int c, char *out)
{
	if (c < 0x80) {
		*out++ = c;
		return 1;
	}
	else if (c < 0x800) {
		*out++ = 0xc0 | (c >> 6);
		*out++ = 0x80 | (c & 0x3f);
		return 2;
	}
	else if (c < 0x10000) {
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
}

static void UTF16toUTF8(const wchar_t *src, char *dest, size_t room)
{
	while (wchar_t c = *src++) {
		if (c >= 0xd800 && c < 0xe000 && *src) {	// invalid utf-16 character, check if utf-16
			wchar_t d = *src++;
			if (d>0xdc00 && d < 0xe000)
				c = (((c & 0x3ff) << 10) | (d & 0x3ff)) + 0x10000;
			else
				c = 0;
		}
		if (room >= 4) {
			int skip = asUTF8(c, dest);
			dest += skip;
			room -= skip;
		}
	}
	if (room)
		*dest++ = 0;
}

int _tmain(int argc, _TCHAR* argv[])
{
	char utf8_buf[4096];

	if (argc > 1) {
		UTF16toUTF8(argv[1], utf8_buf, sizeof(utf8_buf));
		unsigned int hash = fnv1A(utf8_buf, strlen(utf8_buf));

		printf("_FNV1A_%s 0x%08x\t// \"%s\"\n", utf8_buf, hash, utf8_buf);
	} else
		printf("Usage:\nfnv1a <string>");
	return 0;
}

#else

int main(int argc, char **argv)
{
	if (argc > 1) {
		unsigned int hash = fnv1A(argv[1], (unsigned int)strlen(argv[1]));

		printf("_FNV1A_%s 0x%08x\t// \"%s\"\n", argv[1], hash, argv[1]);
	}
	else
		printf("Usage:\nfnv1a <string>");
	return 0;
}

#endif
