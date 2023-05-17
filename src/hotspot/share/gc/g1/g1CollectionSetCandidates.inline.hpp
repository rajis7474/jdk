/*
 * Copyright (c) 2023, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_GC_G1_G1COLLECTIONSETCANDIDATES_INLINE_HPP
#define SHARE_GC_G1_G1COLLECTIONSETCANDIDATES_INLINE_HPP

#include "gc/g1/g1CollectionSetCandidates.hpp"

#include "utilities/growableArray.hpp"

inline G1CollectionCandidateListIterator::G1CollectionCandidateListIterator(G1CollectionCandidateList* which, uint position) :
  _which(which), _position(position) { }

inline G1CollectionCandidateListIterator& G1CollectionCandidateListIterator::operator++() {
  assert(_position < _which->length(), "must be");
  _position++;
  return *this;
}

inline HeapRegion* G1CollectionCandidateListIterator::operator*() {
  return _which->_candidates.at(_position)._r;
}

inline bool G1CollectionCandidateListIterator::operator==(const G1CollectionCandidateListIterator& rhs) {
  assert(_which == rhs._which, "iterator belongs to different array");
  return _position == rhs._position;
}

inline bool G1CollectionCandidateListIterator::operator!=(const G1CollectionCandidateListIterator& rhs) {
  return !(*this == rhs);
}

inline G1CollectionSetCandidatesIterator::G1CollectionSetCandidatesIterator(G1CollectionSetCandidates* which, uint marking_position, uint retained_position) :
  _which(which), _is_marking_selected(false), _marking_position(marking_position), _retained_position(retained_position) {

  select_list();
}

inline void G1CollectionSetCandidatesIterator::select_list() {
  if (_marking_position >= _which->_marking_regions.length()) {
    _is_marking_selected = false;
  } else if (_retained_position >= _which->_retained_regions.length()) {
    _is_marking_selected = true;
  } else {
    _is_marking_selected = _which->_marking_regions.at(_marking_position)._gc_efficiency >= _which->_retained_regions.at(_retained_position)._gc_efficiency;
  }
}

inline G1CollectionSetCandidatesIterator& G1CollectionSetCandidatesIterator::operator++() {
  assert(_marking_position < _which->_marking_regions.length() || _retained_position < _which->_retained_regions.length(),
         "must not be at end already");

  if (_is_marking_selected) {
    _marking_position++;
  } else {
    _retained_position++;
  }
  select_list();
  return *this;
}

inline HeapRegion* G1CollectionSetCandidatesIterator::operator*() {
  if (_is_marking_selected) {
    return _which->_marking_regions.at(_marking_position)._r;
  } else {
    return _which->_retained_regions.at(_retained_position)._r;
  }
}

inline bool G1CollectionSetCandidatesIterator::operator==(const G1CollectionSetCandidatesIterator& rhs)  {
  assert(_which == rhs._which, "iterator belongs to different array");
  return _marking_position == rhs._marking_position && _retained_position == rhs._retained_position;
}

inline bool G1CollectionSetCandidatesIterator::operator!=(const G1CollectionSetCandidatesIterator& rhs)  {
  return !(*this == rhs);
}

#endif /* SHARE_GC_G1_G1COLLECTIONSETCANDIDATES_INLINE_HPP */
