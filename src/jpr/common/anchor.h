// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "jpr/common/modifiers.h"

namespace jpr {

class AnchorBase;

//==============================================================================
// AnchorHold
//==============================================================================

// RAII handle for holding an anchor (see Anchor below).
//
// While a hold is alive, its anchor refers to the held object. Destroying or
// resetting the hold releases the anchor. If the anchor is cleared or destroyed
// first, the hold becomes empty, so a hold never refers to an anchor that is no
// longer held by it, and holds and anchors may be destroyed in any order.
//
// AnchorHold is move-only.
class AnchorHold final {
 public:
  AnchorHold() = default;
  AnchorHold(const AnchorHold&) = delete;
  AnchorHold& operator=(const AnchorHold&) = delete;
  AnchorHold(AnchorHold&& other);
  AnchorHold& operator=(AnchorHold&& other);
  ~AnchorHold() { Reset(); }

  // Returns the anchor this hold is holding, or null if it is empty.
  const AnchorBase* GetAnchor() const { return anchor_; }

  // Returns true if this hold is holding its anchor.
  bool IsHeld() const { return anchor_ != nullptr; }

  // Releases the anchor (if held), and makes this an empty hold.
  void Reset();

 private:
  friend class AnchorBase;

  explicit AnchorHold(AnchorBase* anchor);

  AnchorBase* anchor_ = nullptr;
};

//==============================================================================
// AnchorBase
//==============================================================================

// Untyped state and behavior shared by all Anchor<T> types.
//
// An anchor refers to at most one object at a time, which is set by holding the
// anchor and cleared when the hold is released. The first hold wins: holding an
// anchor that is already held returns an empty hold.
//
// An anchor may optionally be given a modifier bit, which is on exactly while
// the anchor is held. This keeps the modifier in sync with the anchor however
// it is released or cleared.
class AnchorBase {
 public:
  AnchorBase(const AnchorBase&) = delete;
  AnchorBase& operator=(const AnchorBase&) = delete;

  // Returns true if the anchor is currently held.
  bool IsHeld() const { return held_ != nullptr; }

  // Clears the anchor, making its hold (if any) empty.
  void Clear();

  // The modifier bit (or bits) that is on while the anchor is held, or zero for
  // none. Changing it while held turns off the old modifier and turns on the
  // new one.
  Modifiers GetModifier() const { return modifier_; }
  void SetModifier(Modifiers modifier);

 protected:
  AnchorBase() = default;
  ~AnchorBase() { Clear(); }

  // Holds the anchor on the object, or returns an empty hold if the object is
  // null or the anchor is already held.
  AnchorHold DoHold(void* object);

  // Returns the held object, or null if the anchor is not held.
  void* DoGet() const { return held_; }

 private:
  friend class AnchorHold;

  void* held_ = nullptr;
  AnchorHold* hold_ = nullptr;  // Non-null exactly when held_ is non-null.
  Modifiers modifier_ = 0;
};

//==============================================================================
// Anchor
//==============================================================================

// An anchor that refers to at most one object of type T at a time.
//
// For example, an anchor may be held on a track while a button is held down, so
// that other actions can refer to that track for as long as it is held.
template <typename T>
class Anchor final : public AnchorBase {
 public:
  Anchor() = default;
  ~Anchor() = default;

  // Holds the anchor on the object, or returns an empty hold if the object is
  // null or the anchor is already held.
  AnchorHold Hold(T* object) { return DoHold(object); }

  // Returns the held object, or null if the anchor is not held.
  T* Get() const { return static_cast<T*>(DoGet()); }
};

}  // namespace jpr
