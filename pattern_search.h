#ifndef _PATTERN_SEARCH_H
#define _PATTERN_SEARCH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

#ifndef eprintf
#define eprintf(str, ...)                                                        \
    {                                                                            \
        fprintf(stderr, "%s, %d: " str "\n", __func__, __LINE__, ##__VA_ARGS__); \
        fflush(stderr);                                                          \
    }
#endif

/**
 * @brief Match pattern in b2 to location in b1
 * 
 * @param b1 Search location
 * @param len1 Length of search array
 * @param b2 Key location
 * @param len2 Length of key array
 * @return void* Pointer to first hit of key in search array
 */
static void *find_match(void *b1, ssize_t len1, void *b2, ssize_t len2)
{
    ssize_t i = 0;
    ssize_t idx = 0;
    void *retval = NULL;
    ssize_t maxlen = len1;
    ssize_t cmplen = len2;
    char *buf1 = (char *)b1;
    char *buf2 = (char *)b2;
    if (len1 < len2)
    {
        eprintf("Length of array 1 (search array) has to be larger than array 2 (template array)");
        goto exit;
    }
    for (; i < maxlen - cmplen + 1;)
    {
        if (buf1[i] == buf2[0])
        {
            for (idx = 1; idx < cmplen; idx++)
            {
                if (buf1[i + idx] != buf2[idx])
                    break;
            }
            if (idx == cmplen) // we have hit the string
            {
                retval = (void *)(buf1 + i);
            }
            i += idx;
        }
        else
            i++;
    }
exit:
    return retval;
}

#ifdef __cplusplus
}
#endif
#endif // _PATTERN_SEARCH_H
