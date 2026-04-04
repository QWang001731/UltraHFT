#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace ultrahft::core {

template <typename T, std::size_t Capacity>
class MemoryPool {
public:
	static_assert(Capacity > 0, "MemoryPool capacity must be greater than zero");

	MemoryPool() : free_top_(Capacity), live_count_(0),
		storage_(new std::byte[sizeof(T) * Capacity]),
		free_list_(new std::size_t[Capacity]),
		occupied_(Capacity) {
		for (std::size_t i = 0; i < Capacity; ++i) {
			free_list_.get()[i] = Capacity - 1 - i;
			occupied_.reset(i);
		}
	}

	~MemoryPool() {
		for (std::size_t i = 0; i < Capacity; ++i) {
			if (occupied_.test(i)) {
				std::destroy_at(ptr_at(i));
				occupied_.reset(i);
			}
		}
	}

	MemoryPool(const MemoryPool&) = delete;
	MemoryPool& operator=(const MemoryPool&) = delete;
	MemoryPool(MemoryPool&&) = delete;
	MemoryPool& operator=(MemoryPool&&) = delete;

	template <typename... Args>
	T* allocate(Args&&... args) {
		if (free_top_ == 0) {
			return nullptr;
		}

		const std::size_t slot = free_list_.get()[--free_top_];
		T* const p = std::construct_at(ptr_at(slot), std::forward<Args>(args)...);
		occupied_.set(slot);
		++live_count_;
		return p;
	}

	bool deallocate(T* p) {
		const auto idx = index_of(p);
		if (!idx.has_value()) {
			return false;
		}

		const std::size_t slot = *idx;
		if (!occupied_.test(slot)) {
			return false;
		}

		std::destroy_at(ptr_at(slot));
		occupied_.reset(slot);
		free_list_.get()[free_top_++] = slot;
		--live_count_;
		return true;
	}

	[[nodiscard]] constexpr std::size_t capacity() const noexcept { return Capacity; }
	[[nodiscard]] std::size_t size() const noexcept { return live_count_; }
	[[nodiscard]] std::size_t available() const noexcept { return free_top_; }

	[[nodiscard]] bool owns(const T* p) const noexcept {
		return index_of(p).has_value();
	}

private:
	[[nodiscard]] T* ptr_at(std::size_t slot) noexcept {
		return std::launder(reinterpret_cast<T*>(storage_.get() + slot * sizeof(T)));
	}

	[[nodiscard]] const T* ptr_at(std::size_t slot) const noexcept {
		return std::launder(reinterpret_cast<const T*>(storage_.get() + slot * sizeof(T)));
	}

	template <typename Ptr>
	[[nodiscard]] std::optional<std::size_t> index_of(Ptr* p) const noexcept {
		if (p == nullptr) {
			return std::nullopt;
		}

		const auto base = reinterpret_cast<std::uintptr_t>(storage_.get());
		const auto end = reinterpret_cast<std::uintptr_t>(storage_.get() + sizeof(T) * Capacity);
		const auto addr = reinterpret_cast<std::uintptr_t>(p);

		if (addr < base || addr >= end) {
			return std::nullopt;
		}

		const std::uintptr_t offset = addr - base;
		constexpr std::uintptr_t stride = sizeof(T);
		if (offset % stride != 0) {
			return std::nullopt;
		}

		const std::size_t slot = static_cast<std::size_t>(offset / stride);
		if (slot >= Capacity) {
			return std::nullopt;
		}

		return slot;
	}

	std::unique_ptr<std::byte[]> storage_;
	std::unique_ptr<std::size_t[]> free_list_;
	std::bitset<Capacity> occupied_;
	std::size_t free_top_;
	std::size_t live_count_;
};

}
