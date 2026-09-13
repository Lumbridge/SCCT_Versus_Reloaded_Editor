#pragma once

namespace BspCollisionFix
{
    void Initialize();

    // Only recovery may reorder its disjoint, generated structural brushes.
    // Ordinary builds retry with the saved source order unchanged.
    class RecoveryScope
    {
        bool previous;
    public:
        RecoveryScope();
        ~RecoveryScope();
        RecoveryScope(const RecoveryScope&) = delete;
        RecoveryScope& operator=(const RecoveryScope&) = delete;
    };
}
