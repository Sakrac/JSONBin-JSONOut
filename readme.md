JSONBin / JSONOut
=================

Just another parser/writer for JSON. I made a quick and dirty JSON parser that I wasn't very happy with, this is a completely different implementation to address the prior shortcomings. As far as I know this implementation satisfies https://tools.ietf.org/html/rfc7159 and the description at http://json.org. Note that JSONBin and JSONOut are completely independent and if you're only reading JSON files you can skip JSONOut and vice versa skip JSONBin if you're only writing JSON files. Detailed instructions are included in jsonbin.h and jsonout.h.

License
-------

Public Domain; no warranty implied; use at your own risk; may contain flaws; attribution appreciated but not requested.
In no event will the authors be held liable for any damages arising from the use of this software.
Created by Carl-Henrik Skårstedt (#Sakrac).

Achievements with this implementation
-------------------------------------

- Parsing produces a single array of JSON values, linear if accessed depth first.
- Parsing minimizes the number of allocations (1 returned block, 1-2 temporary work blocks).
- Writing JSON values performs no allocations.
- Parsed data is completely relocatable and can be saved and loaded to a different location.
- Parsing runs reasonably fast (depends on data and env, about 2.5-3s for a ~190 MB test file).
- Each parsed JSON name/value can be as small as 12 bytes depending on compiled traits.
- Supports in-memory representation of utf-16 wchar_t strings (optional compiled trait).
- Clean up parsed data with a single free call, no per item destructor.
- Writing floating point numbers is faster than trivial implementation.
- Duplicate strings for names and string values are shared.
- Parsed data can be customized depending on application usage with compiled traits.
- Detailed data error reporting (line, column and context).
- Minimal depencies on separate code libraries (no stl, etc.)

###Limitations

- No implementation or plans for 16 or 32 bit json files. Other tools can convert existing files to utf-8.
- Writer only supports indentation formatting, not as-compact-as-possible.
- Numeric values must fit within the range of the data type (int/long long/float/double). A parsing error is returned if an out of bounds value is encountered.
- Numeric precision of floating point values depends on the floating point representation.
- Numeric value precision depends on whether a number is floating point or integer. The parser makes a choice based on detecting fraction or exponent and the generator will always include a fraction or exponent if passed a floating point value so that values always are represented the same on save and load.
- Does not parse XML files.

###Drawbacks

- Parser accesses text data three times (count strings, then count items, then fill it out).
- Implementation is done in C++.
- Parser requires the entire original file in memory and will allocate work memory for speeding up string comparison.

###Improvements

- Looking in to various ways to parse json files in parts
- If names of values are not included (JB_KEY_HASH) they still count towards the potential maximum number of strings.
- A C version should be fairly trivial
- For smaller files a string cache may not make a significant difference so possibly an option to disable that

Samples
-------
Samples are located in the samples folder.

- sample_behaviortree.cpp converts a behavior tree JSON with a node type JSON into binary standalone data
- sample_scenegraph.cpp creates a random tree of structures that can be saved and loaded
- sample_numbers.cpp is a numeric test to check that ranges of numbers save correctly
- sample_resave.cpp loads a JSON file and then saves it again

Reasoning
---------

http://utf8everywhere.org/
http://www.joshbarczak.com/blog/?p=580

The overarching goal of this implementation is to remove layers of abstraction, remove dependencies outside of clib, and minimize the number of allocations. It isn't necessary to keep the writing and parsing parts of a format compatible as long as both are easy to use, which means parsed data don't need to deal with inserting values in the middle and writing data can store off data as necessary without keeping a complete representation in-memory. A secondary goal is performance which became apparent after attempting to iterate with test files in the range of 100-200 MB. To reach acceptable performance a simple hash table approach was implemented to compare strings, and custom functions for reading and writing floating point values.

The depth-first ordering of the JSON values should in theory also bring a cache usage improvement to the application side of reading the parsed data.

All this amounts to improving something that wasn't causing any noticable performance or memory issues in the first place given that the tool that inspired this work doesn't generate very large or very complicated JSON files. This is a project dedicated to the iteration of itself, but it may have an application elsewhere.

###Reasoning (with numbers)

To illustrate the gains of reducing allocations in a parser I use a large example file (https://github.com/zeMirco/sf-city-lots-json) with the following properties:

Number of JSON Items: 13805884
Strings in original: 5109655
Bytes of strings in original: 41600031

Assuming that an allocator keeps a 16 byte block header and a minimum alignment of 16 bytes, the JSON Item structure that may only use 12 bytes actually requires 32 bytes meaning just the items use over 421 MB, compared to 158 MB in an array of items.
That does not include the allocations for the strings which adds 78 MB of allocation headers to the 40 MB of actual string data reaching a total of nearly 540 MB.

In addition to the memory cost, 18915539 allocations may have some impact on the allocation performance. All those allocation issues can be mitigated by adding a small alloc for the strings and making a pool allocator for the JSON items but that adds additional burden on the application when destroying the parsed data.

Total size using an array of items plus sharing duplicate strings: 167442700, or 160 MB of which 1.7 MB are shared strings (In fairness, add another 16 bytes for the allocation header for that block).

Note that the parser needs the entire file in memory up front and will allocate work memory for speeding up string comparison which in this case adds up to about 100 MB which is released before returning the parsed data, and that the 12 bytes per JSON item is the minimum size. The large file size and pattern of data in it may not be typical, but it is representative of real data. There seem to be real-world cases of large JSON files: http://stackoverflow.com/questions/15932492/ipad-parsing-an-extremely-huge-json-file-between-50-and-100-mb

It is entirely possible to stream in JSON files in small chunks and build your data, without the upfront cost of allocating the entire file, but that requires a bit more awareness and care. That is not the purpose of this implementation (search for a callback style JSON parser unless you want to make the changes locally).

Customization
-------------

Local code changes are highly encouraged to fit the application purpose. The goal with this implementation is not to support every possible variation but to provide a functional memory friendly implementation with reasonable performance. If a good enough estimate can be made for the number of items and size of strings the processing can be made in a single pass with a few changes, or the parsing can be changed to a streaming model with some added state tracking and adding callbacks. Less intrusive customization can be achieved with changing some defines and is already implemented.
