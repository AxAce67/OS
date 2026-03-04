#pragma once

class CommandHistory {
public:
    static constexpr int kMaxEntries = 16;
    static constexpr int kMaxCommandLength = 128;

    CommandHistory();

    void Add(const char* command);
    void Clear();
    void ResetNavigation();

    bool BrowseUp(const char* current_line, char* out, int out_len);
    bool BrowseDown(char* out, int out_len);

    int Count() const;
    const char* Entry(int index) const;

private:
    char entries_[kMaxEntries][kMaxCommandLength];
    int count_;
    int nav_;
    char draft_[kMaxCommandLength];
};

