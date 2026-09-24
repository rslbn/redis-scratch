#ifndef ENUMS_H
#define ENUMS_H

enum {
    ERR_UNKNOWN = 1,
    ERR_TO_BIG = 2
};

enum {
    TAG_NIL = 0, // null
    TAG_ERR = 1, // error code +  msg
    TAG_STR = 2, // string
    TAG_INT = 3, // int64
    TAG_DBL = 4, // double
    TAG_ARR = 5 // array
};

/** Binary Format
 * -------------------------------------------------------
 * | nil |   int64   |   str           | array           |
 * -------------------------------------------------------
 * | tag | tag | int | tag | len | ... | tag | len | ... |
 * -------------------------------------------------------
 * | 1b  | 1b  | 8b  | 1b  | 4b  | ... | 1b  | 4b  | ... |
 * -------------------------------------------------------
*/

/**
 * Tag is 1 byte, length is 4 bytes. Strings are arbitrary bytes.
 * Array length is the number of array elements.
*/
 

/**
 * Integers and lengths are encoded in little-endian, which
 * is just `memcpy`ing values on all relevant platforms
 */
#endif