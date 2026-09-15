#pragma once
#include <array>
#include <cmath>
#include <stdexcept>

namespace BrushGridSnap
{
    // Translate the selection as one unit. On ties prefer its minimum bound;
    // grid midpoint ties round away from zero, including negative coordinates.
    inline std::array<double,3> Translation(const std::array<double,3>& minimum,
        const std::array<double,3>& maximum,const std::array<double,3>& grid,unsigned axes)
    {
        if(!axes || (axes&~7u))throw std::runtime_error("Choose X, Y, Z or all axes.");
        std::array<double,3> delta{};
        for(int axis=0;axis<3;++axis)
        {
            if(!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) || minimum[axis]>maximum[axis])
                throw std::runtime_error("The selected brushes have invalid bounds.");
            if(!(axes&(1u<<axis)))continue;
            if(!std::isfinite(grid[axis]) || grid[axis]<=0)
                throw std::runtime_error("The current grid spacing must be greater than zero.");
            const double lower=std::round(minimum[axis]/grid[axis])*grid[axis]-minimum[axis];
            const double upper=std::round(maximum[axis]/grid[axis])*grid[axis]-maximum[axis];
            delta[axis]=std::abs(lower)<=std::abs(upper)?lower:upper;
            if(!std::isfinite(delta[axis]))throw std::runtime_error("The grid snap is out of range.");
        }
        return delta;
    }
}
