#pragma once
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>


template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory&) = delete;

    RawMemory(RawMemory&& other) noexcept {
        Swap(other);
    }

    RawMemory& operator=(RawMemory&& other) noexcept {
        Swap(other);
        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {

        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:

    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    Vector() = default;

    explicit Vector(size_t size)
        : data_(size)
        , size_(size)
    {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    Vector(const Vector& other)
        : data_(other.size_)
        , size_(other.size_)
    {
        std::uninitialized_copy_n(other.data_.GetAddress(), other.size_, data_.GetAddress());
    }

    Vector& operator=(const Vector& other) {
        if (other.Size() > data_.Capacity()) {
            Vector tmp(other);
            Swap(tmp);
            return *this;
        }
        else {
            for (size_t i = 0; i < Size() && i < other.Size(); ++i) {
                data_[i] = other[i];
            }
            if (Size() < other.Size()) {
                std::uninitialized_copy_n(other.data_.GetAddress() + Size(),
                    other.Size() - Size(),
                    data_.GetAddress() + Size());
            }
            else if (Size() > other.Size()) {
                std::destroy_n(data_.GetAddress() + other.Size(),
                    Size() - other.Size());
            }
            size_ = other.Size();
        }
        return *this;
    }

    Vector(Vector&& other) noexcept {
        Swap(other);
    }

    Vector& operator=(Vector&& other) noexcept {
        Swap(other);
        return *this;
    }

    ~Vector() {
        std::destroy_n(data_.GetAddress(), Size());
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {

        size_t result = pos - this->begin();
        if (this->size_ == result) {
            if (this->size_ == this->Capacity()) {
              size_t capacity_tmp = 0;

              this->size_ == 0
                ? capacity_tmp += 1
                : capacity_tmp += this->size_ * 2;

              RawMemory<T> new_data(capacity_tmp);
              new (new_data + this->size_) T(std::forward<Args>(args)...);

              MoveOrCopyElements(data_.GetAddress(), new_data.GetAddress(), size_);

              std::destroy_n(this->data_.GetAddress(), this->size_);
              this->data_.Swap(new_data);
            }
            else
              new (this->data_ + this->size_) T(std::forward<Args>(args)...);
        }
        else if (this->size_ < this->Capacity()) {
            T tmp = T(std::forward<Args>(args)...);
            new (this->data_ + this->size_) T(std::move(this->data_[this->size_ - 1u]));
            std::move_backward(this->data_.GetAddress() + result, this->end() - 1, this->end());
            this->data_[result] = std::move(tmp);
        }
        else {
            RawMemory<T> new_data(this->size_ * 2);
            new(new_data + result) T(std::forward<Args>(args)...);

            MoveOrCopyElements(data_.GetAddress(), new_data.GetAddress(), result);
            MoveOrCopyElements(data_.GetAddress() + result, new_data.GetAddress() + result + 1, size_ - result);
            
            std::destroy_n(this->data_.GetAddress(), this->size_);
            this->data_.Swap(new_data);
        }

        this->size_++;
        return (this->data_.GetAddress() + result);
    }

    iterator Erase(const_iterator pos) /*noexcept(std::is_nothrow_move_assignable_v<T>)*/ {
        iterator pos_it = const_cast<iterator>(pos);
        std::move(pos_it + 1, this->end(), pos_it);
        std::destroy_n(this->data_.GetAddress() + (--this->size_), 1);
        return pos_it;
    }

    iterator Insert(const_iterator pos, const T& value) {
        return this->Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return this->Emplace(pos, std::forward<T>(value));
    }

    void Resize(size_t new_size) {
        if (size_ < new_size) {

            Reserve(new_size);
            std::uninitialized_value_construct_n(data_.GetAddress() + Size(), new_size - Size());

        }
        else if (size_ > new_size) {
            std::destroy_n(data_.GetAddress() + new_size, Size() - new_size);
        }
        size_ = new_size;
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        return *Emplace(end(), std::forward<Args>(args)...);
    }

    template<typename Type>
    void PushBack(Type&& value) {
        EmplaceBack(std::forward<Type>(value));
    }

    void PopBack() {
        std::destroy_at(data_.GetAddress() + size_ - 1);
        --size_;
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }
        RawMemory<T> new_data(new_capacity);

        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
        else {
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
        }

        std::destroy_n(data_.GetAddress(), Size());
        data_.Swap(new_data);
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    iterator begin() noexcept { return this->data_.GetAddress(); }
    iterator end() noexcept { return (this->data_.GetAddress() + this->size_); }

    const_iterator begin() const noexcept { return this->data_.GetAddress(); }
    const_iterator cbegin() const noexcept { return begin(); }

    const_iterator end() const noexcept { return (this->data_.GetAddress() + this->size_); }
    const_iterator cend() const noexcept { return end(); }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

private:
    void MoveOrCopyElements(T* from, T* to, size_t count) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(from, count, to);
        }
        else {
            std::uninitialized_copy_n(from, count, to);
        }
    }

    RawMemory<T> data_;
    size_t size_ = 0;
};