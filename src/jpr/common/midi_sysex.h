// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "absl/strings/str_cat.h"
#include "absl/types/span.h"

namespace jpr {

//==============================================================================
// SysexPrefix
//==============================================================================

// A unique prefix for a specific sysex message, which can be used to identify a
// message as belong to a registered SysexMessageType.
class SysexPrefix {
 public:
  // Constructs a SysexPrefix with the given prefix bytes. The prefix must be to
  // a constant buffer that lasts for the life of the program.
  explicit SysexPrefix(absl::Span<const uint8_t> prefix) : prefix_(prefix) {}
  SysexPrefix(const SysexPrefix&) = default;
  SysexPrefix& operator=(const SysexPrefix&) = default;
  SysexPrefix(SysexPrefix&&) = default;
  SysexPrefix& operator=(SysexPrefix&&) = default;
  ~SysexPrefix() = default;

  // Returns the prefix bytes for this sysex message type.
  absl::Span<const uint8_t> GetPrefix() const { return prefix_; }

  // Operators and conversion functions
  bool operator==(const SysexPrefix& other) const {
    return prefix_ == other.prefix_;
  }
  template <typename H>
  friend H AbslHashValue(H h, const SysexPrefix& prefix) {
    return H::combine(std::move(h), prefix.prefix_);
  }
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const SysexPrefix& prefix) {
    sink.Append("{");
    for (size_t i = 0; i < prefix.prefix_.size(); ++i) {
      if (i > 0) {
        sink.Append(" ");
      }
      sink.Append(absl::StrCat(absl::Hex(prefix.prefix_[i], absl::kZeroPad2)));
    }
    sink.Append("}");
  }

 private:
  absl::Span<const uint8_t> prefix_;
};

//==============================================================================
// SysexMessage
//==============================================================================

// A sysex message, which consists of a header byte, a prefix that identifies
// the type of message, variable data bytes, and a footer byte.
//
// The prefix is used to identify the type of message and is stored separately
// for easy access, but is also embedded in the message bytes for easy sending.
// The variable data bytes can be modified through a mutable span, while the
// header, prefix, and footer bytes are fixed and required for the message to be
// valid.
class SysexMessage {
 public:
  // Constructs a SysexMessage with the given prefix and a capacity for the
  // message bytes (not including the header, prefix, and footer). The prefix is
  // stored separately for easy access and identification, but is also embedded
  // in the message bytes.
  SysexMessage(SysexPrefix prefix, int size);
  SysexMessage(const SysexMessage&) = default;
  SysexMessage& operator=(const SysexMessage&) = default;
  SysexMessage(SysexMessage&&) = default;
  SysexMessage& operator=(SysexMessage&&) = default;
  ~SysexMessage() = default;

  // The prefix for this message.
  SysexPrefix GetPrefix() const { return prefix_; }

  // Returns the full message bytes of this sysex message.
  //
  // This includes the header, prefix, variable data, and footer bytes that make
  // up the full message. This should be used when sending the message, and is
  // guaranteed to be in the correct format for the message to be valid.
  absl::Span<const uint8_t> GetBytes() const { return bytes_; }

  // Returns a mutable span of the sub-section of the message bytes that can be
  // modified. This does not include the header, prefix, or footer bytes that
  // are required for the message to be valid, and should only be used to modify
  // the portion of the message that contains the variable data for the message.
  absl::Span<uint8_t> GetMutableBytes() {
    const int prefix_size = static_cast<int>(prefix_.GetPrefix().size());
    return absl::MakeSpan(bytes_.data() + prefix_size + 1,
                          bytes_.size() - prefix_size - 2);
  }

 private:
  // The prefix for this message. This is embedded in the message bytes, but is
  // stored separately for easy access and identification.
  SysexPrefix prefix_;
  std::vector<uint8_t> bytes_;
};

//==============================================================================
// SysexMessageState
//==============================================================================

// The device state associated with a sysex message type.
//
// This is used by MidiOut to manage the device state associated with a
// SysexMessageType, update it with messages of that type, and determine if the
// message needs to be sent.
class SysexMessageState {
 public:
  SysexMessageState(const SysexMessageState&) = delete;
  SysexMessageState& operator=(const SysexMessageState&) = delete;
  virtual ~SysexMessageState() = default;

  // Returns a deep copy of this state object. This is used by MidiOut to
  // maintain separate queued and pending state copies.
  virtual std::unique_ptr<SysexMessageState> Clone() const = 0;

  // Updates the state with the given message, and returns whether the message
  // should be sent based on whether it changes the state.
  //
  // Note that this will always return false if the message is of the wrong
  // prefix type.
  virtual bool Update(const SysexMessage& message) = 0;

 protected:
  SysexMessageState() = default;
};

//==============================================================================
// SysexMessageType
//==============================================================================

// A registered type of sysex message, which is identified by a unique prefix
// and is a factory for state for messages of that type.
class SysexMessageType {
 public:
  // Returns the registered SysexMessageType for the given prefix, or nullptr if
  // no such type is registered with that prefix.
  static SysexMessageType* Get(const SysexPrefix& prefix);

  SysexMessageType(const SysexMessageType&) = delete;
  SysexMessageType& operator=(const SysexMessageType&) = delete;

  // Destroys the SysexMessageType, unregistering it if is currently registered.
  virtual ~SysexMessageType();

  // Attempts to register this SysexMessageType.
  //
  // This will succeed and return true if the SysexPrefix for this type was not
  // already registered, and false otherwise. On failure, an error will also be
  // logged.
  bool Register();

  // Returns the prefix for this SysexMessageType, which is used to identify
  // messages of this type.
  const SysexPrefix& GetPrefix() const { return prefix_; }

  // Returns a new state instance for this SysexMessageType with the given
  // message.
  //
  // This is used by MidiOut to manage the state for this SysexMessageType, and
  // should return a new instance of the appropriate SysexMessageState subclass
  // for this type of message.
  //
  // If the message is not valid for this SysexMessageType, this should return
  // nullptr.
  virtual std::unique_ptr<SysexMessageState> CreateState(
      const SysexMessage& message) const = 0;

 protected:
  // Creates a SysexMessageType with the given prefix.
  explicit SysexMessageType(SysexPrefix prefix);

 private:
  SysexPrefix prefix_;
  bool registered_ = false;
};

}  // namespace jpr
