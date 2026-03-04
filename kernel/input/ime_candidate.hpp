#pragma once

struct ImeCandidateEntry {
    const char* key;             // romaji source
    const char* candidates[4];   // display/insert text (single-byte font space)
    int count;
};

