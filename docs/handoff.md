# Session handoff

Last updated: 2026-11-21

[Milestone 16](milestone-16-plan.md) (Android version) is closed, committed
and pushed (`20e317a`, `247e218`, record `8e59c0e`). No milestone is active;
the maintainer starts the next one explicitly.

Maintenance fix (committed and pushed): the shared-save
staging cleanup in `Stage::~Stage` no longer uses
`std::filesystem::remove_all`. The tolerant removal that the tests used moved
to `src/app/remove_tree.h` and now serves both. It also removes Windows
junctions and directory symlinks as links: GCC 16.2's `symlink_status`
reports them as directories, so the old code deleted a link target's files
(confirmed with a probe). An entry whose attributes cannot be read is
skipped and reported, not walked. `tests/shared_save_tests.cpp` covers nested
trees and links. The build has no warnings; all 182 fast tests pass. The
independent review approved the change. Its remaining low notes were left
as they are: non-link reparse points such as OneDrive placeholders would leave
a harmless stage behind, and a link swapped in mid-walk is not defended against.

The complete suite on `build-rel` passed 390/390 with zero skips, exit 0
(1445 s). It ran in a background shell, and its completion notice was
awaited. The log is `build-rel/stage-cleanup-suite.log`.

The old worktrees from milestones 5-14 were removed. Their uncommitted
changes were saved as patches in `../worktree-archive/`, and the merged
branch `milestone-6-fast-forward` was deleted.
