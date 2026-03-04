#include "input/history.hpp"

#include "shell/text.hpp"

CommandHistory::CommandHistory() : count_(0), nav_(-1) {
    draft_[0] = '\0';
}

void CommandHistory::Add(const char* command) {
    if (command == nullptr || command[0] == '\0') {
        return;
    }
    if (count_ < kMaxEntries) {
        CopyString(entries_[count_], command, kMaxCommandLength);
        ++count_;
        return;
    }
    for (int i = 1; i < kMaxEntries; ++i) {
        CopyString(entries_[i - 1], entries_[i], kMaxCommandLength);
    }
    CopyString(entries_[kMaxEntries - 1], command, kMaxCommandLength);
}

void CommandHistory::Clear() {
    count_ = 0;
    nav_ = -1;
    draft_[0] = '\0';
}

void CommandHistory::ResetNavigation() {
    nav_ = -1;
    draft_[0] = '\0';
}

bool CommandHistory::BrowseUp(const char* current_line, char* out, int out_len) {
    if (count_ <= 0 || out == nullptr || out_len <= 0) {
        return false;
    }
    if (nav_ == -1) {
        if (current_line != nullptr) {
            CopyString(draft_, current_line, kMaxCommandLength);
        } else {
            draft_[0] = '\0';
        }
        nav_ = count_ - 1;
    } else if (nav_ > 0) {
        --nav_;
    }
    CopyString(out, entries_[nav_], out_len);
    return true;
}

bool CommandHistory::BrowseDown(char* out, int out_len) {
    if (nav_ < 0 || out == nullptr || out_len <= 0) {
        return false;
    }
    if (nav_ < count_ - 1) {
        ++nav_;
        CopyString(out, entries_[nav_], out_len);
        return true;
    }
    nav_ = -1;
    CopyString(out, draft_, out_len);
    return true;
}

int CommandHistory::Count() const {
    return count_;
}

const char* CommandHistory::Entry(int index) const {
    if (index < 0 || index >= count_) {
        return nullptr;
    }
    return entries_[index];
}

