#pragma once

// C++標準ライブラリを使わずに自作する、固定長のリングバッファ（FIFOキュー）
// 割り込みハンドラのような「超高速で終わらせる必要がある場所」から、
// メインループのような「ゆっくり処理していい場所」へデータを渡すために使う

template <typename T, int Capacity>
class ArrayQueue {
public:
    ArrayQueue() : read_pos_(0), write_pos_(0), count_(0) {}

    // キューにデータを追加する（Push / Enqueue）
    // 成功すればtrue、満杯ならfalseを返す
    bool Push(const T& value) {
        if (count_ == Capacity) {
            return false; // キューが一杯
        }
        data_[write_pos_] = value;
        write_pos_ = (write_pos_ + 1) % Capacity;
        count_++;
        return true;
    }

    // キューからデータを取り出す（Pop / Dequeue）
    // 成功すればtrue、空ならfalseを返す
    bool Pop(T& out_value) {
        if (count_ == 0) {
            return false; // キューが空
        }
        out_value = data_[read_pos_];
        read_pos_ = (read_pos_ + 1) % Capacity;
        count_--;
        return true;
    }

    // キューのデータ数を返す
    int Count() const {
        return count_;
    }

private:
    T data_[Capacity];
    int read_pos_, write_pos_, count_;
};
