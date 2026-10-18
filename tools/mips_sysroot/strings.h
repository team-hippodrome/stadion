/* Minimal <strings.h> for the layout checker's 32-bit MIPS pass; declarations only. */
#ifndef LAYOUT_CHECK_STRINGS_H
#define LAYOUT_CHECK_STRINGS_H
#include <string.h>
void bzero(void*, size_t);
void bcopy(const void*, void*, size_t);
#endif
