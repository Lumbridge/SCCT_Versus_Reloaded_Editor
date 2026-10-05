#pragma once
#include "WorkflowModel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Placement tools for selected actors: drop onto the surface below (or above,
// or ahead), align and distribute by bounding box, and copies in a line, round
// a centre or along a path. The editor side (PlacementNative.inl) reads the
// boxes and traces the level; everything here is plain geometry.
//
// Conventions are Unreal's: a Rotation is pitch/yaw/roll in 65536ths of a
// turn, yaw turns +X towards +Y, and an actor's local point p reaches the
// world as Rotation * (DrawScale3D * DrawScale * (p - PrePivot)) + Location.
namespace Workflow::Placement
{
constexpr double kPi = 3.14159265358979323846;
// How far a drop looks for a surface: the editor's WORLD_MAX.
constexpr double kReach = 524288;
// Copies per operation, and actors created by one operation.
constexpr int kMaxCopies = 500;
constexpr size_t kMaxActors = 2000;

using Matrix = std::array<Vector, 3>; // m[row][column]; columns are the X, Y and Z axes
struct Box { Vector lo{}, hi{}; };

inline void Check(const Vector& v, const char* what = "Position is out of range.")
{
    for (double x : v) if (!std::isfinite(x) || std::abs(x) > 10000000) throw std::runtime_error(what);
}
inline Vector Add(const Vector& a, const Vector& b) { return { a[0] + b[0], a[1] + b[1], a[2] + b[2] }; }
inline Vector Sub(const Vector& a, const Vector& b) { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
inline Vector Scale(const Vector& a, double s) { return { a[0] * s, a[1] * s, a[2] * s }; }
inline double Dot(const Vector& a, const Vector& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline Vector Cross(const Vector& a, const Vector& b) { return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }; }
inline double Length(const Vector& a) { return std::sqrt(Dot(a, a)); }
inline Vector Unit(const Vector& a)
{
    const double length = Length(a);
    if (!(length > 1e-9) || !std::isfinite(length)) throw std::runtime_error("A direction needs a length.");
    return Scale(a, 1 / length);
}

inline Vector Centre(const Box& b) { return { (b.lo[0] + b.hi[0]) / 2, (b.lo[1] + b.hi[1]) / 2, (b.lo[2] + b.hi[2]) / 2 }; }
inline Vector HalfSize(const Box& b) { return { (b.hi[0] - b.lo[0]) / 2, (b.hi[1] - b.lo[1]) / 2, (b.hi[2] - b.lo[2]) / 2 }; }
inline Box Moved(const Box& b, const Vector& d) { return { Add(b.lo, d), Add(b.hi, d) }; }
inline Box PointsBox(const std::vector<Vector>& points)
{
    if (points.empty()) throw std::runtime_error("A box needs at least one point.");
    Box box{ points.front(), points.front() };
    for (const auto& p : points)
    {
        Check(p);
        for (int i = 0; i < 3; ++i) { box.lo[i] = std::min(box.lo[i], p[i]); box.hi[i] = std::max(box.hi[i], p[i]); }
    }
    return box;
}
inline Box Union(const std::vector<Box>& boxes)
{
    if (boxes.empty()) throw std::runtime_error("Select something first.");
    Box box = boxes.front();
    for (const auto& b : boxes)
        for (int i = 0; i < 3; ++i) { box.lo[i] = std::min(box.lo[i], b.lo[i]); box.hi[i] = std::max(box.hi[i], b.hi[i]); }
    return box;
}
inline std::array<Vector, 8> Corners(const Box& b)
{
    std::array<Vector, 8> c{};
    for (int i = 0; i < 8; ++i) c[i] = { (i & 1) ? b.hi[0] : b.lo[0], (i & 2) ? b.hi[1] : b.lo[1], (i & 4) ? b.hi[2] : b.lo[2] };
    return c;
}
// A collision cylinder (pawns, pickups, lights and other sprites) as a box.
inline Box CylinderBox(const Vector& location, double radius, double height)
{
    Check(location);
    if (!std::isfinite(radius) || !std::isfinite(height) || radius < 0 || height < 0) throw std::runtime_error("Invalid collision size.");
    return { { location[0] - radius, location[1] - radius, location[2] - height }, { location[0] + radius, location[1] + radius, location[2] + height } };
}

// Rotation <-> axes, as the engine's FRotationMatrix and FMatrix::Rotator.
inline Matrix Axes(const Rotation& r)
{
    const double k = 2 * kPi / 65536;
    const double sp = std::sin(r[0] * k), cp = std::cos(r[0] * k), sy = std::sin(r[1] * k), cy = std::cos(r[1] * k), sr = std::sin(r[2] * k), cr = std::cos(r[2] * k);
    return { { { cp * cy, sr * sp * cy - cr * sy, -(cr * sp * cy + sr * sy) },
               { cp * sy, sr * sp * sy + cr * cy, cy * sr - cr * sp * sy },
               { sp, -sr * cp, cr * cp } } };
}
inline Vector Column(const Matrix& m, int c) { return { m[0][c], m[1][c], m[2][c] }; }
inline Vector Multiply(const Matrix& m, const Vector& v)
{
    Vector out{};
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) out[i] += m[i][j] * v[j];
    return out;
}
inline Matrix Multiply(const Matrix& a, const Matrix& b)
{
    Matrix c{};
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k) c[i][j] += a[i][k] * b[k][j];
    return c;
}
inline int Units(double radians) { return static_cast<int>(std::lround(radians * 65536 / (2 * kPi))); }
inline Rotation FromAxes(const Matrix& m)
{
    const double pitch = std::asin(std::clamp(m[2][0], -1.0, 1.0));
    const bool pole = std::abs(std::cos(pitch)) < 1e-7;
    const double yaw = pole ? std::atan2(-m[0][1], m[1][1]) : std::atan2(m[1][0], m[0][0]);
    const double roll = pole ? 0 : std::atan2(-m[2][1], m[2][2]);
    return { Units(pitch), Units(yaw), Units(roll) };
}
// Turning about Z by a yaw in Unreal units.
inline Vector Turn(const Vector& v, int yaw)
{
    const double a = yaw * 2 * kPi / 65536, c = std::cos(a), s = std::sin(a);
    return { v[0] * c - v[1] * s, v[0] * s + v[1] * c, v[2] };
}
inline int WrapYaw(long long yaw) { return static_cast<int>(((yaw % 65536) + 65536) % 65536); }

// The world box of a local box (a static mesh's bounds) under an actor's
// transform: the box of its eight transformed corners.
inline Box WorldBox(const Box& local, const Vector& location, const Rotation& rotation, const Vector& scale, const Vector& prePivot = {})
{
    Check(location); Check(scale, "Invalid draw scale."); Check(prePivot, "Invalid pre-pivot.");
    const auto axes = Axes(rotation);
    std::vector<Vector> points;
    for (const auto& corner : Corners(local))
    {
        Vector p = Sub(corner, prePivot);
        for (int i = 0; i < 3; ++i) p[i] *= scale[i];
        points.push_back(Add(Multiply(axes, p), location));
    }
    return PointsBox(points);
}

// ---------------------------------------------------------------- Align

enum class Edge { Min, Centre, Max };
inline double EdgeOf(const Box& b, int axis, Edge edge)
{
    return edge == Edge::Min ? b.lo[axis] : edge == Edge::Max ? b.hi[axis] : (b.lo[axis] + b.hi[axis]) / 2;
}
// Each actor moves along one axis so its box's minimum, centre or maximum
// matches the key actor's (the last one selected), which stays.
inline std::vector<Vector> AlignDeltas(const std::vector<Box>& boxes, size_t key, int axis, Edge edge)
{
    if (boxes.size() < 2) throw std::runtime_error("Select two or more actors; the last one selected stays put.");
    if (key >= boxes.size() || axis < 0 || axis > 2) throw std::runtime_error("Invalid alignment.");
    const double target = EdgeOf(boxes[key], axis, edge);
    std::vector<Vector> deltas(boxes.size());
    for (size_t i = 0; i < boxes.size(); ++i) if (i != key) deltas[i][axis] = target - EdgeOf(boxes[i], axis, edge);
    return deltas;
}

// ------------------------------------------------------------ Distribute

// Along one axis: the two outermost actors stay and the rest are spaced so
// their centres are evenly apart between them, keeping their order.
inline std::vector<Vector> DistributeAxisDeltas(const std::vector<Box>& boxes, int axis)
{
    if (boxes.size() < 3) throw std::runtime_error("Select three or more actors to distribute.");
    if (axis < 0 || axis > 2) throw std::runtime_error("Invalid axis.");
    std::vector<size_t> order(boxes.size());
    std::iota(order.begin(), order.end(), size_t{ 0 });
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return Centre(boxes[a])[axis] < Centre(boxes[b])[axis]; });
    const double first = Centre(boxes[order.front()])[axis], last = Centre(boxes[order.back()])[axis];
    std::vector<Vector> deltas(boxes.size());
    const double n = static_cast<double>(order.size() - 1);
    for (size_t rank = 1; rank + 1 < order.size(); ++rank)
        deltas[order[rank]][axis] = first + (last - first) * static_cast<double>(rank) / n - Centre(boxes[order[rank]])[axis];
    return deltas;
}
// On the straight line between two actors (the first and last selected),
// which stay: the others go onto the line, evenly apart, in the order their
// centres fall along it.
inline std::vector<Vector> DistributeLineDeltas(const std::vector<Box>& boxes, size_t first, size_t last)
{
    if (boxes.size() < 3) throw std::runtime_error("Select three or more actors to distribute.");
    if (first >= boxes.size() || last >= boxes.size() || first == last) throw std::runtime_error("Invalid distribution ends.");
    const Vector a = Centre(boxes[first]), b = Centre(boxes[last]), line = Sub(b, a);
    const double length2 = Dot(line, line);
    if (!(length2 > 1e-6)) throw std::runtime_error("The first and last actors selected are in the same place.");
    std::vector<size_t> middle;
    for (size_t i = 0; i < boxes.size(); ++i) if (i != first && i != last) middle.push_back(i);
    std::stable_sort(middle.begin(), middle.end(), [&](size_t i, size_t j) { return Dot(Sub(Centre(boxes[i]), a), line) < Dot(Sub(Centre(boxes[j]), a), line); });
    std::vector<Vector> deltas(boxes.size());
    const double n = static_cast<double>(boxes.size() - 1);
    for (size_t rank = 0; rank < middle.size(); ++rank)
    {
        const Vector target = Add(a, Scale(line, static_cast<double>(rank + 1) / n));
        deltas[middle[rank]] = Sub(target, Centre(boxes[middle[rank]]));
    }
    return deltas;
}

// ------------------------------------------------------------------ Drop

enum class Surface { Floor, Ceiling, Wall };
inline const char* SurfaceName(Surface s) { return s == Surface::Floor ? "floor" : s == Surface::Ceiling ? "ceiling" : "wall"; }
// Down, up, or level along the way the actor faces (its X axis).
inline Vector DropDirection(Surface surface, const Rotation& rotation)
{
    if (surface == Surface::Floor) return { 0, 0, -1 };
    if (surface == Surface::Ceiling) return { 0, 0, 1 };
    const auto forward = Column(Axes(rotation), 0);
    const Vector level{ forward[0], forward[1], 0 };
    if (Length(level) < 1e-6) throw std::runtime_error("The actor faces straight up or down, so it has no wall ahead.");
    return Unit(level);
}
// Half the box's size along a direction: how far it reaches from its centre.
inline double Support(const Box& box, const Vector& direction)
{
    const auto half = HalfSize(box);
    return std::abs(direction[0]) * half[0] + std::abs(direction[1]) * half[1] + std::abs(direction[2]) * half[2];
}
// Where the drop's rays start: the box's centre and four points across its
// face towards the surface (at 80% of its reach, so a box touching a wall at
// its side does not find that wall), all at the centre's depth. Rays are
// straight lines; a box's footprint is covered by the five of them.
inline std::vector<Vector> DropStarts(const Box& box, const Vector& direction)
{
    const Vector centre = Centre(box);
    Vector u = std::abs(direction[2]) > 0.9 ? Vector{ 1, 0, 0 } : Vector{ -direction[1], direction[0], 0 };
    u = Unit(u);
    const Vector w = Unit(Cross(direction, u));
    const double su = Support(box, u) * 0.8, sw = Support(box, w) * 0.8;
    std::vector<Vector> starts{ centre };
    for (int i = 0; i < 4; ++i)
        starts.push_back(Add(centre, Add(Scale(u, (i & 1) ? su : -su), Scale(w, (i & 2) ? sw : -sw))));
    return starts;
}
// From each ray's distance to its hit (none when it found nothing), how far
// the box moves along the direction so its face rests on the nearest surface.
// A ray that hit at once started inside something and says nothing. A
// negative result lifts a box that is sunk into the surface.
inline std::optional<double> DropDistance(const Box& box, const Vector& direction, const std::vector<std::optional<double>>& hits)
{
    std::optional<double> nearest;
    for (const auto& hit : hits)
        if (hit && std::isfinite(*hit) && *hit > 0.5 && (!nearest || *hit < *nearest)) nearest = *hit;
    if (!nearest) return std::nullopt;
    return *nearest - Support(box, direction);
}
// Turns an actor the least it takes to sit flat on a surface with this
// normal: its up axis along the floor's normal, against a ceiling's, or its
// front facing into a wall. What does not need to change (the yaw on a level
// floor) stays.
inline Rotation AlignToSurface(const Rotation& rotation, const Vector& normal, Surface surface)
{
    const Vector n = Unit(normal);
    const auto axes = Axes(rotation);
    const int which = surface == Surface::Wall ? 0 : 2;
    const Vector from = Column(axes, which);
    const Vector to = surface == Surface::Floor ? n : Scale(n, -1);
    const double cosine = std::clamp(Dot(from, to), -1.0, 1.0);
    if (cosine > 1 - 1e-9) return rotation;
    Vector k;
    double angle;
    if (cosine < -1 + 1e-9) { k = Column(axes, which == 2 ? 0 : 2); angle = kPi; }
    else { k = Unit(Cross(from, to)); angle = std::acos(cosine); }
    // Rodrigues: the rotation by angle about k, as a matrix.
    const double c = std::cos(angle), s = std::sin(angle), t = 1 - c;
    const Matrix q{ { { t * k[0] * k[0] + c, t * k[0] * k[1] - s * k[2], t * k[0] * k[2] + s * k[1] },
                      { t * k[0] * k[1] + s * k[2], t * k[1] * k[1] + c, t * k[1] * k[2] - s * k[0] },
                      { t * k[0] * k[2] - s * k[1], t * k[1] * k[2] + s * k[0], t * k[2] * k[2] + c } } };
    return FromAxes(Multiply(q, axes));
}

// ---------------------------------------------------------------- Copies

// One copy of the selection: each actor's location p becomes
// pivot + Turn(p + before - pivot, yaw) + after; with turn, its yaw turns by
// the same angle (a copy round a centre faces it as the original does).
struct Copy
{
    Vector before{}, pivot{};
    int yaw = 0;
    bool turn = false;
    Vector after{};
};
inline Vector Apply(const Copy& copy, const Vector& p)
{
    return Add(Add(copy.pivot, Turn(Sub(Add(p, copy.before), copy.pivot), copy.yaw)), copy.after);
}
inline Rotation Apply(const Copy& copy, Rotation r)
{
    if (copy.turn) r[1] = WrapYaw(static_cast<long long>(r[1]) + copy.yaw);
    return r;
}
inline void CheckCount(int copies, size_t actors)
{
    if (copies < 1 || copies > kMaxCopies) throw std::runtime_error("Make between 1 and " + std::to_string(kMaxCopies) + " copies.");
    if (actors == 0) throw std::runtime_error("Select something to copy first.");
    if (static_cast<size_t>(copies) * actors > kMaxActors)
        throw std::runtime_error("That is " + std::to_string(static_cast<size_t>(copies) * actors) + " new actors; one operation makes at most " + std::to_string(kMaxActors) + ".");
}
// In a line: copy k sits k offsets from the original.
inline std::vector<Copy> Linear(int count, const Vector& offset)
{
    Check(offset, "Offset is out of range.");
    if (Length(offset) < 0.01) throw std::runtime_error("Enter an offset between copies.");
    if (count < 1 || count > kMaxCopies) throw std::runtime_error("Make between 1 and " + std::to_string(kMaxCopies) + " copies.");
    std::vector<Copy> copies;
    for (int k = 1; k <= count; ++k) { Copy c; c.after = Scale(offset, k); Check(c.after, "The copies would leave the world."); copies.push_back(c); }
    return copies;
}
// Round a vertical axis through pivot. Without a radius the copies follow the
// selection's own circle and share the arc with it (a full turn of n copies
// steps 360/(n+1) degrees, so the original is one of n+1 evenly spaced
// places). With a radius every copy sits that far out from the pivot, level
// with the selection, starting in the selection's direction (or along +X when
// it is on the pivot), and the original is left where it is. An arc short of
// a full turn ends on its last copy.
inline std::vector<Copy> Radial(int count, const Vector& centre, const Vector& pivot, double arc, double radius, bool turn)
{
    Check(centre); Check(pivot);
    if (count < 1 || count > kMaxCopies) throw std::runtime_error("Make between 1 and " + std::to_string(kMaxCopies) + " copies.");
    if (!std::isfinite(arc) || arc <= 0 || arc > 360) throw std::runtime_error("The arc is between 0 and 360 degrees.");
    if (!std::isfinite(radius) || radius < 0 || radius > 1000000) throw std::runtime_error("The radius is 0 (keep the selection's own) or a positive distance.");
    const bool full = arc > 360 - 1e-6;
    const bool own = radius == 0;
    Vector before{};
    if (!own)
    {
        const Vector out{ centre[0] - pivot[0], centre[1] - pivot[1], 0 };
        const Vector direction = Length(out) > 0.01 ? Unit(out) : Vector{ 1, 0, 0 };
        before = Sub(Scale(direction, radius), out);
    }
    else if (std::hypot(centre[0] - pivot[0], centre[1] - pivot[1]) < 0.01 && !turn)
        throw std::runtime_error("The selection is on the centre: give a radius, or turn the copies.");
    const int places = own ? count + 1 : count;
    const double step = full ? arc / places : (places > 1 ? arc / (places - 1) : 0);
    std::vector<Copy> copies;
    for (int k = 0; k < count; ++k)
    {
        Copy c;
        c.pivot = pivot;
        c.before = before;
        c.turn = turn;
        c.yaw = static_cast<int>(std::lround(step * (own ? k + 1 : k) * 65536 / 360));
        copies.push_back(c);
    }
    return copies;
}
// Along the straight path from the selection's centre to an end point: count
// copies with the last on the end, or (with a spacing) one every spacing
// units for as far as the path goes.
inline std::vector<Copy> Path(const Vector& from, const Vector& to, int count, double spacing)
{
    Check(from); Check(to);
    const Vector path = Sub(to, from);
    const double length = Length(path);
    if (length < 0.01) throw std::runtime_error("The path's end is where the selection already is.");
    std::vector<Copy> copies;
    if (spacing > 0)
    {
        if (!std::isfinite(spacing)) throw std::runtime_error("Invalid spacing.");
        const double fit = std::floor(length / spacing + 1e-6);
        if (fit < 1) throw std::runtime_error("The spacing is longer than the path.");
        if (fit > kMaxCopies) throw std::runtime_error("That spacing makes more than " + std::to_string(kMaxCopies) + " copies.");
        const Vector step = Scale(Unit(path), spacing);
        for (int k = 1; k <= static_cast<int>(fit); ++k) { Copy c; c.after = Scale(step, k); copies.push_back(c); }
        return copies;
    }
    if (count < 1 || count > kMaxCopies) throw std::runtime_error("Make between 1 and " + std::to_string(kMaxCopies) + " copies.");
    for (int k = 1; k <= count; ++k) { Copy c; c.after = Scale(path, static_cast<double>(k) / count); copies.push_back(c); }
    return copies;
}

// The twelve edges of a box as a copy places it: turned with the copy when
// the copy turns, else only carried along with the box's centre.
inline std::vector<std::pair<Vector, Vector>> BoxEdges(const Box& box, const Copy& copy)
{
    std::array<Vector, 8> c = Corners(box);
    if (copy.turn) for (auto& p : c) p = Apply(copy, p);
    else
    {
        const Vector shift = Sub(Apply(copy, Centre(box)), Centre(box));
        for (auto& p : c) p = Add(p, shift);
    }
    static const int edges[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
    std::vector<std::pair<Vector, Vector>> out;
    for (const auto& e : edges) out.push_back({ c[e[0]], c[e[1]] });
    return out;
}

// --------------------------------------------------------- Selection order

// The order actors were selected in, which the editor does not keep: each
// look at the selection appends what is newly selected (in the order given)
// and drops what is not selected any more. The last entry is the key actor.
template<class Id> void UpdateOrder(std::vector<Id>& order, const std::vector<Id>& selected)
{
    order.erase(std::remove_if(order.begin(), order.end(), [&](const Id& id) { return std::find(selected.begin(), selected.end(), id) == selected.end(); }), order.end());
    for (const auto& id : selected) if (std::find(order.begin(), order.end(), id) == order.end()) order.push_back(id);
}
}
