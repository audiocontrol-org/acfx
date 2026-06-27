### CPM bootstrap ignores its cache and re-downloads every configure

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    cmake/CPM.cmake:1-30

The bootstrap says CPM is downloaded “on first configure” and placed in the cache, but the implementation always runs `file(DOWNLOAD ...)` at lines 24-28 before including the cached path at line 30. That means a workspace with a valid `${CPM_SOURCE_CACHE}/cpm/CPM_0.40.5.cmake` still requires network access on every configure. The blast radius is medium: downstream users and CI jobs can lose otherwise-reproducible cached builds whenever GitHub is unavailable or the environment is intentionally offline, even though the cache contains the exact pinned file.

A reasonable fix is to check whether `CPM_DOWNLOAD_LOCATION` already exists and either trust it after hash validation or only download when missing. The invariant should be: the pinned file is fetched only when absent or invalid, and every included cached copy is still verified against `CPM_HASH_SUM`.
