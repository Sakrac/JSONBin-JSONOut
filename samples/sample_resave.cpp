#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../jsonbin/jsonbin.h"
#include "../jsonout/jsonout.h"

//
// Load a JSON file and resave it
//
// Requires both JSONBin and JSONOut
//

//=========================================================================
// Coding style is not representative of a real product, it is kept simple
//      to improve readability and avoid confusing dependencies.
//=========================================================================

static bool ExportJSON(FILE *f, const jbin::JBItem *pJSON)
{
	// avoid recursion by keeping an internal stack on the stack
	jout::JSONOut o(f, pJSON->getType()==jbin::JB_ARRAY);
	const jbin::JBItem* aStack[jbin::JSON_MAX_DEPTH];
	int sp = 0;

	const jbin::JBItem *i = pJSON->getChild();
	while (i || sp) {
		if (!i) {
			i = aStack[--sp];
			o.scope_end();
		} else {
			const jbin::JBItem *children = NULL;

			switch (i->getType()) {
				case jbin::JB_ROOT:
				case jbin::JB_OBJECT:		// object, this is an item with children
					o.push_object(i->getName());
					if (!(children = i->getChild()))
						o.scope_end();
					break;
				case jbin::JB_ARRAY:		// array of objects or arrays or values
					o.push_array(i->getName());
					if (!(children = i->getChild()))
						o.scope_end();
					break;
				case jbin::JB_STRING:		// string value
					o.push(i->getName(), i->getStr(), i->getStrLen());
					break;
				case jbin::JB_INT:			// int value
					o.push(i->getName(), i->getInt());
					break;
				case jbin::JB_FLOAT:		// float value
					o.push(i->getName(), i->getFloat());
					break;
				case jbin::JB_BOOL:		// bool value
					o.push(i->getName(), i->getBool());
					break;
				case jbin::JB_NULL:		// null tag
					o.push_null();
					break;
				case jbin::JB_NULL_VALUE:	// null value
					o.push_null(i->getName());	// either null type or null object
					break;
			}

			if (o.last_error() != jout::JSONOut::ERR_NONE)
				return false;

			i = i->getSibling();
			if (children) {
				aStack[sp++] = i;
				i = children;
			}
		}
	}
	o.finish();
	return true;
}

static void ResaveJSON(const char *input_file, const char *output_file)
{
	FILE *f = NULL;
#ifdef WIN32
	if (!fopen_s(&f, input_file, "rb"))
#else
	if ((f = fopen(input_file, "rb")))
#endif
	{
		fseek(f, 0, SEEK_END);
		size_t size = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (void *data = (void*)malloc(size)) {
			fread(data, size, 1, f);
			fclose(f);
			jbin::JBRet ret = { 0 };
			jbin::JBItem *pJSON = jbin::JSONBin((const char*)data, (unsigned int)size, &ret);
			free(data);

			if (pJSON) {
				const char *saveFile = output_file;
				char buf[256], *dest = buf;
				if (!saveFile) {
					saveFile = buf;
					const char *src = input_file;
					char *last_dot = NULL;
					while (*src) {
						if (*src == '.')
							last_dot = dest;
						*dest++ = *src++;
					}
					if (last_dot)
						dest = last_dot;
					src = ".rsv.json";
					while (*src)
						*dest++ = *src++;
					*dest = 0;
				}
#ifdef WIN32
				if (!fopen_s(&f, saveFile, "w")) {
#else
				if ((f = fopen(saveFile, "w"))) {
#endif
					ExportJSON(f, pJSON);
					fclose(f);
				}
				free(pJSON);
			} else {
				printf("Error at line %d column %d in file %s\n", ret.err_line, ret.err_column, input_file);
			}
		} else
			fclose(f);
	} else {
		printf("Could not open %s\n", input_file);
	}
}



int main(int argc, char **argv)
{
	if (argc > 1)
		ResaveJSON(argv[1], argc > 2 ? argv[2] : 0);
	else
		printf("Usage:\n%s file.json [output.json]\n\nResult:\nLoads a JSON file and resaves it if no errors were detected.\n", argv[0]);

	return 0;
}