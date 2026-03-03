#pragma once

bool StrEqual(const char* a, const char* b);
bool StrStartsWith(const char* s, const char* prefix);
int StrCompare(const char* a, const char* b);
bool ContainsChar(const char* s, char ch);
bool StrContains(const char* haystack, const char* needle);
int StrLength(const char* s);
void CopyString(char* dst, const char* src, int max_len);
const char* SkipSpaces(const char* s);
bool NextToken(const char* s, int* pos, char* out, int out_len);
const char* RestOfLine(const char* s, int pos);
