// sample of structuralizing data
// this example does not use jbin::JBIterator
// there is no guarantee that this sample really does what it claims

//
// This sample is intended to be used with JB_KEY_HASH and JB_KEY_HASH_AND_NAME
//
// Requires only JSONBin
//

//
//=========================================================================
// Coding style is not representative of a real product, it is kept simple
//      to improve readability and avoid confusing dependencies.
//	 Error checking is unforgiving and silent to keep things simple.
//=========================================================================
//
// The setup of this sample is that a tool allows designers to create
//	behavior trees, which are a basic tree structure of tests and operations
//	which are used to descripe how characters should act in certain games.
//
// A fictional programmer creates "node types" in a JSON file (sample_behavior_types.json)
//	that are accessible to a fictional designer in a fictional tool created
//	specifically for the purpose. When a node type is added the game code
//	is also updated to recognize the new type.
//
// By keeping both the types (basically a header file) and the tool code separate,
//	the programmer can add new nodes and change property fields of nodes without
//	recompiling the tool or game for the designer. There are many behaviors that
//	reference a single type file.
//
// Before the game can deal with the behaviors they need to be rearranged into
//	classes. This is an in-between build process that reads in the types
//	and a behavior tree file and creates a representation with consistant data
//	in order to reduce processing when loading the behavior into the game.
//
// This sample generates a binary version of the node type data (number of
//	types, sorted array of type hashes followed by an array of file offsets
//	to each type, for each type the number of members followed by a sorted
//	array of member name hashes followed by an array of member offsets and
//	type) and also a binary version of the behavior tree itself which the
//	game can find the data using the type information.
//
//
// Result Usage
//	This sample generates two files, one type file which is shared by all
//	node files, and one node file that has labels matching the ones in the
//	type file.
//
//	The format of the type file is: 1 int containing the number of structs,
//	followed by a sorted array of the hashed names of the structs so
//	binary search can be applied. After that follows an array of
//	file offsets for each structure and the size of each structure in the
//	node file.
//	After this array follows all the structs member declarations.
//	Each struct begins with a count of its members, followed by a sorted
//	array of the hashed member names, followed by a matching array of
//	data member offset into each structure and the type of each member
//	(int, hash, float, text, enum, etc. defined in the enum typeType)
//
//	The node file format starts with the btNodeData struct immediately
//	followed by member values immediately followed by its children or
//	its sibling. If the sibling high bit is set this node has children
//	if the lower 31 bits of sibling are 0 then this is the last node
//	at this level otherwise the lower 31 bits is the memory offset
//	to the sibling data. String typed members are switched to be
//	relative pointers (strings are stored after all nodes)
//

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "../jsonbin/jsonbin.h"

#define _FNV1A_type 0x5127f14d			// "type"
#define _FNV1A_min 0xc98f4557			// "min"
#define _FNV1A_max 0xd7a2e319			// "max"
#define _FNV1A_default 0x933b5bde		// "default"
#define _FNV1A_enum 0x816cb000			// "enum"
#define _FNV1A_nodes 0x514e905a			// "nodes"
#define _FNV1A_types 0xffe0c49a			// "types"
#define _FNV1A_children 0x67a9c9d2		// "children"
#define _FNV1A_behaviortree 0x126764eb  // "behaviortree"

#ifdef WIN32
#define no_warn_stricmp _stricmp
#else
#define no_warn_stricmp strcasecmp
#endif

//
// DEFINITIONS FOR DYNAMIC DATA STRUCTURES
//

union typeUnion {
	int i;
	unsigned int h;
	unsigned int o;
	float f;
};

enum typeType {
	TYP_INT,		// type.i
	TYP_HASH,		// type.h
	TYP_FLOAT,		// type.f
	TYP_TEXT,		// type.o offset into text table, or -1 if missing
	TYP_ENUM,		// type.i is the index into the enum or -1 if not identified
	TYP_STRUCT		// type.o is a relative pointer
};

struct typeMember {
	unsigned int id;		// hash of member name
	unsigned int offs;		// offset into structure binary
	typeType	type;		// member type
	unsigned int enumID;	// id of enum array
	typeUnion	minVal;
	typeUnion	maxVal;
	typeUnion	defVal;
};

struct typeStruct {
	typeStruct *next;
	unsigned int id;		// hash of struct name
	short numMembers;
	short sizeMembers;		// sum of size of each member
	typeMember members[1];	// extended by allocation
};

//
// TEXT BUFFER
//

struct textTable {
	char *pStart;
	char *pCursor;
	int	size;
	int left;
};

//
// ENUM TYPES
//

struct enumArray {
	enumArray *next;
	unsigned int id;		// hash of enum name
	int numEnums;
	unsigned int aEnums[1];	// extended by allocation
};

//
// BEHAVIOR TREE NODE BINARY DATA
//

struct btNodeData {
	unsigned int type;
	unsigned int sibling;	// 0=no sibling otherwise relative offset, children follow directly after members
	// member data follows
};

//
// TYPE COLLECTION
//

struct typeData {
	typeStruct *pStructs;
	enumArray *pEnums;
	textTable text;
	unsigned int **aTextFixup;
	int nTextFixup;
	btNodeData *pNodeDataLimit;
};

//
// STRING HASHING FUNCTION
//

static unsigned int fnv1A(const char *s, unsigned int l)
{
	unsigned const char *r = (unsigned const char*)s;
	unsigned int hash = JB_FNV1A_SEED;
	while (l--)
		hash = (*r++ ^ hash) * JB_FNV1A_PRIME;
	return hash;
}

//
// PARSE ENUM TYPES
//

static enumArray* ParseEnum(const jbin::JBItem *pEnum)
{
	if (pEnum && pEnum->getType() == jbin::JB_ARRAY) {
		int num = pEnum->getChildCount();
		if (enumArray *pArray = (enumArray*)malloc(sizeof(enumArray) + sizeof(unsigned int) * (num - 1))) {
			pArray->id = pEnum->getHash();
			pArray->numEnums = num;
			int n = 0;
			for (const jbin::JBItem *pValue = pEnum->getChild(); pValue; pValue = pValue->getSibling())
				pArray->aEnums[n++] = fnv1A(pValue->getStr(), pValue->getStrLen());
			return pArray;
		}
	}
	return NULL;
}

static enumArray* ParseEnums(const jbin::JBItem *pEnums)
{
	enumArray *pRet = NULL;
	if (pEnums->getType() == jbin::JB_OBJECT) {
		enumArray **pInsert = &pRet;
		for (const jbin::JBItem *pEnum = pEnums->getChild(); pEnum; pEnum = pEnum->getSibling()) {
			if (enumArray *pArray = ParseEnum(pEnum)) {
				pArray->next = *pInsert;
				*pInsert = pArray;
				pInsert = &pArray->next;
			}
		}
	}
	return pRet;
}

//
// READ A SINGLE VALUE FROM TYPE MIN/MAX/DEFAULT OR NODE MEMBER VALUE
//

static bool ParseValue(const jbin::JBItem *pItem, typeType type, unsigned int enumID, typeUnion *value, textTable *text, enumArray *pEnums)
{
	if (pItem) {
		switch (type) {
			case TYP_INT:
				if (pItem->getType() == jbin::JB_INT) {
					value->i = pItem->getInt();
					return true;
				}
				break;
			case TYP_HASH:
				if (pItem->getType() == jbin::JB_STRING) {
					value->h = fnv1A(pItem->getStr(), pItem->getStrLen());
					return true;
				}
				break;
			case TYP_FLOAT:
				if (pItem->getType() == jbin::JB_FLOAT || pItem->getType() == jbin::JB_INT) {
					value->f = pItem->getFloat();
					return true;
				}
				break;
			case TYP_TEXT:
				value->i = -1;
				if (pItem->getType() == jbin::JB_STRING) {
					int strUse = pItem->getStrLen() + 1;
					if (text->left >= strUse) {
						value->o = (unsigned int)(text->pCursor - text->pStart);
						memcpy(text->pCursor, pItem->getStr(), strUse);
						text->pCursor += strUse;
						text->left -= strUse;
						return true;
					}
				}
				break;
			case TYP_ENUM:
				value->i = -1;
				if (pItem->getType() == jbin::JB_STRING) {
					for (enumArray *pEnum = pEnums; pEnum; pEnum = pEnum->next) {
						if (pEnum->id == enumID) {
							unsigned int hash = fnv1A(pItem->getStr(), pItem->getStrLen());
							for (int e = 0; e < pEnum->numEnums; e++) {
								if (hash == pEnum->aEnums[e]) {
									value->i = e;
									return true;
								}
							}
						}
					}
				}
				break;
            default:
                break;
		}
	}
	return false;
}

//
// PARSE STRUCTURE DATA
//

static bool ParseMember(const jbin::JBItem *pMemberItem, typeMember *pMember, unsigned int *structOffs, textTable *text, enumArray *pEnums)
{
	if (pMemberItem->getType() != jbin::JB_OBJECT)
		return false;

	pMember->id = pMemberItem->getHash();
	pMember->enumID = 0;
	if (const jbin::JBItem *pTypeItem = pMemberItem->findByHash(_FNV1A_type)) {
		if (no_warn_stricmp(pTypeItem->getStr(), "int") == 0)
			pMember->type = TYP_INT;
		else if (no_warn_stricmp(pTypeItem->getStr(), "hash") == 0)
			pMember->type = TYP_HASH;
		else if (no_warn_stricmp(pTypeItem->getStr(), "float") == 0)
			pMember->type = TYP_FLOAT;
		else if (no_warn_stricmp(pTypeItem->getStr(), "text") == 0)
			pMember->type = TYP_TEXT;
		else if (no_warn_stricmp(pTypeItem->getStr(), "enum") == 0) {
			pMember->type = TYP_ENUM;
			if (const jbin::JBItem *pEnumItem = pMemberItem->findByHash(_FNV1A_enum))
				pMember->enumID = fnv1A(pEnumItem->getStr(), pEnumItem->getStrLen());
		} else
			return false;	// unknown type

		pMember->offs = *structOffs;
		*structOffs += 4;	// in this case all members are 4 bytes

		pMember->minVal.i = 0;
		pMember->maxVal.i = 0;
		pMember->defVal.i = 0;

		for (const jbin::JBItem *pScan = pMemberItem->getChild(); pScan; pScan = pScan->getSibling()) {
			switch (pScan->getHash()) {
				case _FNV1A_min:
					if (!ParseValue(pScan, pMember->type, pMember->enumID, &pMember->minVal, text, pEnums))
						return false;
					break;
				case _FNV1A_max:
					if (!ParseValue(pScan, pMember->type, pMember->enumID, &pMember->maxVal, text, pEnums))
						return false;
					break;
				case _FNV1A_default:
					if (!ParseValue(pScan, pMember->type, pMember->enumID, &pMember->defVal, text, pEnums))
						return false;
					break;
			}
		}
		return true;
	}

	return false;
}

static typeStruct* ParseStruct(const jbin::JBItem *pStructItem, textTable *text, enumArray *pEnums)
{
	if (!pStructItem || pStructItem->getType() != jbin::JB_OBJECT)
		return NULL;

	int memberCount = pStructItem->getChildCount();
	int structSize = sizeof(typeStruct) + sizeof(typeMember) * (memberCount - 1);

	typeStruct *pStruct = (typeStruct*)malloc(structSize);
	pStruct->next = NULL;
	pStruct->id = pStructItem->getHash();
	
	const jbin::JBItem *pMemberItem = pStructItem->getChild();
	int member = 0;
	unsigned int structOffs = 0;
	while (member < memberCount && pMemberItem) {
		if (ParseMember(pMemberItem, &pStruct->members[member], &structOffs, text, pEnums))
			member++;
		pMemberItem = pMemberItem->getSibling();
	}
	pStruct->numMembers = member;
	pStruct->sizeMembers = structOffs;
	return pStruct;
}

//
// Container for enums, text table and structs
//

static bool ParseTypes(const jbin::JBItem *pTypeFile, typeData *pData)
{
	// look up enums
	pData->pEnums = NULL;
	if (const jbin::JBItem *pEnumsItem = pTypeFile->findByHash(_FNV1A_enum))
		pData->pEnums = ParseEnums(pEnumsItem);

	// create the node types
	if (const jbin::JBItem *pNodes = pTypeFile->findByHash(_FNV1A_nodes)) {
		if (pNodes->getType() == jbin::JB_OBJECT) {
			typeStruct **pInsert = &pData->pStructs;
			for (const jbin::JBItem *pNodeItem = pNodes->getChild(); pNodeItem; pNodeItem = pNodeItem->getSibling()) {
				if (typeStruct *pNode = ParseStruct(pNodeItem, &pData->text, pData->pEnums)) {
					pNode->next = *pInsert;
					*pInsert = pNode;
					pInsert = &pNode->next;
				}
			}
		}
	}
	return true;
}

//
// LOAD A JSON FILE
//

static void* LoadFile(const char *filename, unsigned int *size)
{
	void *data = 0;
	FILE *f = NULL;
#ifdef WIN32
	if (!fopen_s(&f, filename, "rb"))
#else
	if ((f = fopen(filename, "rb")))
#endif
	{
		fseek(f, 0, SEEK_END);
		unsigned int data_size = (unsigned int)ftell(f);
		fseek(f, 0, SEEK_SET);
		if ((data = (void*)malloc(data_size))) {
			fread(data, data_size, 1, f);
			if (size)
				*size = data_size;
		}
		fclose(f);
	}
	return data;
}

//
// LOAD THE NODE TYPES JSON FILE
//

static bool LoadTypes(const char *filename, typeData *pData)
{
	unsigned int size = 0;
	if (void *data = LoadFile(filename, &size)) {
		const unsigned char *buf = (const unsigned char*)data;
		if (*buf == 0xef && buf[1] == 0xbb && buf[2] == 0xbf) { size -= 3; buf += 3; }

		jbin::JBRet ret = { 0 };
		jbin::JBItem *pJSON = jbin::JSONBin((const char*)buf, (unsigned int)size, &ret);
		free(data);

		if (pJSON) {
			bool ret = ParseTypes(pJSON, pData);
			free(pJSON);
			return ret;
		}
	}
	return false;
}

//
// CREATE A BINARY VERSION OF THE NODE TYPES FILE
//

struct sortHashOffs {
	unsigned int hash;
	unsigned int offs;
	unsigned int type;
};

static int sortHashFunc(const void *A, const void *B)
{
	return ((sortHashOffs*)A)->hash < ((sortHashOffs*)B)->hash ? -1 : 1;
}

static void* TypesBinary(typeStruct *pStructs, int *size)
{
	// number of structs
	int numStructs = 0;
	for (typeStruct *pCount = pStructs; pCount; pCount = pCount->next)
		numStructs++;

	int maxSort = numStructs;	// size of work memory for sorting

	// size of count for structs offsets
	unsigned int countSize = sizeof(int);

	// size of struct offsets
	unsigned int structOffsSize = sizeof(unsigned int) * 3 * numStructs;

	// find size for each struct
	unsigned int structSize = 0;
	for (typeStruct *pStructSize = pStructs; pStructSize; pStructSize = pStructSize->next) {
		unsigned int size = sizeof(int) + sizeof(unsigned int) * 3 * pStructSize->numMembers; // member count, hash+offset+type table for members
		structSize += size;

		if (pStructSize->numMembers > maxSort)
			maxSort = pStructSize->numMembers;
	}

	*size = countSize + structOffsSize + structSize;
	void *ret = malloc(countSize + structOffsSize + structSize);
	sortHashOffs *work = (sortHashOffs*)malloc(sizeof(sortHashOffs) * maxSort);

	// sort the structs by ID
	int n = 0;
	unsigned int structOffs = countSize + structOffsSize;
	for (typeStruct *pStructSort = pStructs; pStructSort; pStructSort = pStructSort->next) {
		unsigned int size =
			sizeof(int) + // member count
			sizeof(unsigned int) * 3 * pStructSort->numMembers; // hash+offset+type table for members

		work[n].hash = pStructSort->id;
		work[n].offs = structOffs;
		work[n].type = pStructSort->sizeMembers;

		structOffs += size;
		n++;
	}
	qsort(work, numStructs, sizeof(sortHashOffs), sortHashFunc);

	// fill in the struct count and the sorted struct hashes and the sorted struct offsets
	*(int*)ret = numStructs;
	unsigned int *pHashes = (unsigned int*)((char*)ret + countSize);
	unsigned int *pOffsets = (unsigned int*)((char*)ret + countSize)+numStructs;
	for (int s = 0; s < numStructs; s++) {
		*pHashes++ = work[s].hash;
		*pOffsets++ = work[s].offs;
		*pOffsets++ = work[s].type;	// size
	}

	// fill in the struct members for each struct
	unsigned int *pDest = (unsigned int*)((char*)ret + countSize + structOffsSize);
	for (typeStruct *pStructStore = pStructs; pStructStore; pStructStore = pStructStore->next) {
		*pDest++ = pStructStore->numMembers;
		for (int m = 0; m < pStructStore->numMembers; m++) {
			work[m].hash = pStructStore->members[m].id;
			work[m].offs = pStructStore->members[m].offs;
			work[m].type = pStructStore->members[m].type;
		}
		qsort(work, pStructStore->numMembers, sizeof(sortHashOffs), sortHashFunc);
		pHashes = pDest;
		pOffsets = pHashes + pStructStore->numMembers;
		for (int m = 0; m < pStructStore->numMembers; m++) {
			*pHashes++ = work[m].hash;
			*pOffsets++ = work[m].offs;
			*pOffsets++ = work[m].type;
		}
		pDest = pOffsets;
	}
	free(work);
	return ret;
}

//
// PROCESS THE NODES JSON FILE INTO A BINARY VERSION
//

static btNodeData* LoadNodeRecursive(const jbin::JBItem *pNodes, btNodeData *pDest, typeData *pTypes)
{
	unsigned int *pSibPtr = NULL;
	for (const jbin::JBItem *pNode = pNodes->getChild(); pNode; pNode = pNode->getSibling()) {
		if (pNode->getType() == jbin::JB_OBJECT) {
			unsigned int hash = pNode->getHash();
            for (typeStruct *pStruct=pTypes->pStructs; pStruct; pStruct=pStruct->next) {
                if (pStruct->id == hash) {
					
					if (pDest >= pTypes->pNodeDataLimit)
						return NULL;	// out of memory, bail!

					pDest->type = hash;	// fill in struct id
					pDest->sibling = 0;	// no siblings or children yet

					// Put some values into the members of this struct
					char *pMembers = (char*)(pDest+1);
					const jbin::JBItem *pMemberItem = pNode->findByHash(_FNV1A_nodes);
					for (int m=0; m<pStruct->numMembers; m++) {
						// Get the memory address of this value
						typeUnion *pMember = (typeUnion*)(pMembers+pStruct->members[m].offs);

						// assign a default value
						*pMember = pStruct->members[m].defVal;
						
						// see if this value was specified in the data file
						if (pMemberItem) {
							if (const jbin::JBItem *pValue = pMemberItem->findByHash(pStruct->members[m].id))
								ParseValue(pValue, pStruct->members[m].type, pStruct->members[m].enumID,
										   pMember, &pTypes->text, pTypes->pEnums);
						}

						// if this member is a string and it is assigned some text make a note of it for later fixup
						if (pStruct->members[m].type == TYP_TEXT) {
							if (pMember->i >= 0)
								pTypes->aTextFixup[pTypes->nTextFixup++] = &pMember->o;
							else
								pMember->o = 0;	// relative pointer of missing string should be 0, not -1
						}
					}

					// Update elder sibling
					if (pSibPtr)
						*pSibPtr = *pSibPtr | (unsigned int)((char*)pDest - (char*)pSibPtr);
					pSibPtr = &pDest->sibling;

					// step to the next node
					btNodeData *pParent = pDest;
					pDest = (btNodeData*)((char*)pDest + sizeof(btNodeData) + pStruct->sizeMembers);	// next element
					
					// recurse with child nodes if found
					if (const jbin::JBItem *pChild = pNode->findByHash(_FNV1A_children)) {
						if (!(pDest = LoadNodeRecursive(pChild, pDest, pTypes)))
							return NULL;
						if (pDest!=pParent)
							pParent->sibling |= 0x80000000; // child marker
					}
					break;
                }
			}
		}
	}
	return pDest;
}

//
// LOAD AND PROCESS A SAMPLE BEHAVIOR TREE JSON FILE
//

static bool LoadTree(const char *filename)
{
	typeData types = { 0 };
	void *types_binary = NULL;
	int types_binary_size = 0;
	bool typesLoaded = false;

	// load the file
	unsigned int size = 0;
	if (void *data = LoadFile(filename, &size)) {
		// parse the JSON and discard the file data
		jbin::JBItem *pJSON = jbin::JSONBin((const char*)data, (unsigned int)size);
		free(data);

		// create a text table (betting that 1 MB should suffice)
		types.text.size = 1024 * 1024;
		types.text.left = types.text.size;
		types.text.pStart = (char*)malloc(types.text.size);
		types.text.pCursor = types.text.pStart;
		types.nTextFixup = 0;
		types.aTextFixup = (unsigned int**)malloc(sizeof(types.aTextFixup[0]) * 4096);

		// load the "types" file
		if (const jbin::JBItem *pTypes = pJSON->findByHash(_FNV1A_types))
			typesLoaded = pTypes->getType() == jbin::JB_STRING && LoadTypes(pTypes->getStr(), &types);

		// If type info was loaded ok proceed with the nodes
		if (typesLoaded) {
			// create a binary type file
			types_binary = TypesBinary(types.pStructs, &types_binary_size);

			// create a binary node file
			btNodeData *pNodeData = (btNodeData*)malloc(1024 * 1024);
			unsigned int nodeFileSize = 0;
			types.pNodeDataLimit = (btNodeData*)((char*)pNodeData + 1024 * 1024);
			if (const jbin::JBItem *pNodes = pJSON->findByHash(_FNV1A_behaviortree)) {
				if (btNodeData *pNodeDataEnd = LoadNodeRecursive(pNodes, pNodeData, &types)) {

					// fix up and copy strings after node data
					char *pTextData = (char*)pNodeDataEnd;
					char *pTextCursor = pTextData;
					for (int s = 0; s < types.nTextFixup; s++) {
						const char *orig = types.text.pStart + *types.aTextFixup[s];
						int len = (int)strlen(orig);
						bool found = false;

						// slow string compares but don't expect many strings in each node file
						for (char *pFind = pTextData; pFind < (pTextCursor - len); pFind++) {
							if (strcmp(orig, pFind) == 0) {
								*types.aTextFixup[s] = (unsigned int)(pFind - (const char*)types.aTextFixup[s]);
								found = true;
								break;
							}
						}
						if (!found) {
							*types.aTextFixup[s] = (unsigned int)(pTextCursor - (const char*)types.aTextFixup[s]);
							memcpy(pTextCursor, orig, len + 1);
							pTextCursor += len + 1;
						}
					}

					// update the final data size for the node file
					nodeFileSize = (unsigned int)(pTextCursor - (char*)pNodeData);
				}
			}

			// save the data files
			if (nodeFileSize) {
				// SaveFile("cunningfilename.bin", types_binary, types_binary_size);
				// SaveFile("behaviornodes.bin", pNodeData, nodeFileSize);

			}

			// cleanup
			if (types_binary)
				free(types_binary);

			free(pNodeData);
		}

		// cleanup
		for (enumArray *pEnum = types.pEnums; pEnum; pEnum = pEnum->next)
			free(pEnum);

		for (typeStruct *pStruct = types.pStructs; pStruct; pStruct = pStruct->next)
			free(pStruct);

		free(types.text.pStart);
		return true;
	}
	return false;
}


int main(int argc, char **argv)
{
	LoadTree("samples/sample_behavior_tree.json");
	return 0;
}