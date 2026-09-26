// Kickarse — pure node-list surgery behind the editor (no state, no mapping; node space only).
//
// Segment curves y = ya + (yb − ya)·shape(u, t) with shape = (e^{6tu} − 1)/(e^{6t} − 1) are closed
// under subdivision: cutting a segment at u = c gives sub-segments with tensions t·c and t·(1 − c)
// and the *same* curve. cut() uses that, so every span operation below leaves the curve outside
// the span untouched except for where the span's own end values change. Reversing a segment
// negates its tension (shape(u, −t) = 1 − shape(1 − u, t)); affine maps of y keep tensions.
//
// Conventions: a NodeList is sorted by x; the last node's tension is meaningless and fromList()
// zeroes it. Transform helpers work on a list normalised to [0,1] (first.x = 0, last.x = 1).
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "EditorTypes.h"

namespace kick::editor::ops {

using NodeList = std::vector<EnvNode>;

// Transient flag bits the editor uses inside one operation (selection tracking through sorts and
// splices). Stored/published envelopes never carry them; fromList() keeps them, strip() removes them.
inline constexpr uint32_t kTagSelected = 1u << 31;
inline constexpr uint32_t kTagNew      = 1u << 30;
inline constexpr uint32_t kTagMask     = kTagSelected | kTagNew;

NodeList toList(const Envelope& env);
// Builds a normalised envelope: false (out untouched) when the list has < 2 or > kMaxNodes nodes.
bool     fromList(const NodeList& nodes, Envelope& out);
void     strip(Envelope& env, uint32_t bits = kTagMask) noexcept;

// Structural validity: 2…kMaxNodes nodes, finite, x/y in [0,1], tension in [-1,1], x non-decreasing,
// first.x == 0, last.x == 1. `why` gets a short reason on failure.
bool  isValid(const Envelope& env, std::string* why = nullptr);
// Node-wise equality within tol (x, y, tension, flags); the last node's tension is ignored.
bool  sameNodes(const Envelope& a, const Envelope& b, float tol = 0.f) noexcept;
// Max |a − b| of the heard curves over `samples` phases (plus left limits at every node of both).
float curveDistance(const Envelope& a, const Envelope& b, int samples = 2048);

// Value of segment i (node i → i+1) at node x (no wrap).
float segmentValue(const NodeList& nodes, int i, float x) noexcept;
// Left-continuous value at x in [0,1] (the value arriving at x; at x = 1 the last node's value).
float valueLeft(const NodeList& nodes, float x) noexcept;
// Right-continuous value at x in [0,1] (what is heard from x on; at x = 1 the last node's value).
float valueRight(const NodeList& nodes, float x) noexcept;

// Ensures a node at x (within kSameX) without changing the curve; returns its index: the first
// node at x when !lastAtX (the left limit side), the last one when lastAtX. -1 on bad input.
// The list may temporarily exceed kMaxNodes; the span operations check the final size.
inline constexpr float kSameX = 1e-6f;
int  cut(NodeList& nodes, float x, bool lastAtX);

// Replaces the curve strictly inside [x0, x1] by `piece` (sorted, piece.front().x ≈ x0,
// piece.back().x ≈ x1, ≥ 2 nodes). The node arriving at x0 (first node there, cut in if needed)
// takes the piece's first y/tension; the node leaving x1 (last node there) takes the piece's last y
// and keeps its own outgoing tension. Neighbouring segments keep their (subdivided) tension. With
// x0 == x1 the piece is a vertical step: arrive at front().y, leave at back().y.
// Flags: boundary nodes keep theirs (|= piece flags); interior piece nodes bring their own.
bool replaceSpan(NodeList& nodes, float x0, float x1, const NodeList& piece);

// Applies fn to the nodes i0…i1 mapped to [0,1] and splices the result back into [x_i0, x_i1]
// (connect semantics as replaceSpan). i0 = 0, i1 = last is the whole envelope. fn returns false
// to abort (the list is then untouched). Fails when the result would exceed kMaxNodes.
bool transformSpan(NodeList& nodes, int i0, int i1, const std::function<bool(NodeList&)>& fn);

// ---- Transforms on a list normalised to [0,1] -----------------------------------------------------
void invertY(NodeList& nodes, uint32_t onlyWithFlag = 0) noexcept;         // 0 = all nodes
void scaleY(NodeList& nodes, float factor, uint32_t onlyWithFlag = 0) noexcept;
void reverse(NodeList& nodes);
bool rotate(NodeList& nodes, float amount);           // new(x) = old(frac(x − amount))
bool duplicate(NodeList& nodes);                      // new(x) = old(frac(2x)); false if too many nodes
bool halve(NodeList& nodes);                          // new(x) = old(x / 2)
bool mirrorLeftToRight(NodeList& nodes);              // new(x) = old(x) for x < ½, old(1 − x) after
// Greedy simplification: repeatedly removes the interior node whose removal changes the curve
// least (the merged segment's tension is refitted, error measured against the original curve),
// while that error is <= tolerance or the list still has more than maxNodes nodes. Surviving nodes
// keep their x, y and flags; steps survive unless removing a node is inaudible.
bool simplify(NodeList& nodes, int maxNodes, float tolerance);

// Greedy decimation of an envelope to at most maxNodes (used by simplify and capture).
void decimate(Envelope& env, int maxNodes);

} // namespace kick::editor::ops
