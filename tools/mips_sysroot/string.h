/* Minimal <string.h> for the layout checker's 32-bit MIPS pass (tools/layout_check.py); declarations only. */
#ifndef LAYOUT_CHECK_STRING_H
#define LAYOUT_CHECK_STRING_H
typedef unsigned int size_t;
void* memcpy(void*, const void*, size_t);
void* memmove(void*, const void*, size_t);
void* memset(void*, int, size_t);
int memcmp(const void*, const void*, size_t);
size_t strlen(const char*);
char* strcpy(char*, const char*);
char* strncpy(char*, const char*, size_t);
int strcmp(const char*, const char*);
int strncmp(const char*, const char*, size_t);
char* strcat(char*, const char*);
char* strchr(const char*, int);
#endif
