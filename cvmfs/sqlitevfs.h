/**
 * This file is part of the CernVM File System.
 */

#ifndef CVMFS_SQLITEVFS_H_
#define CVMFS_SQLITEVFS_H_

#include <string>

class CacheManager;
namespace perf {
class Statistics;
}

namespace sqlite {

enum VfsOptions {
  kVfsOptNone = 0,
  kVfsOptDefault,  // the VFS becomes the default for new database connections.
};

bool RegisterVfsRdOnly(CacheManager *cache_mgr,
                       perf::Statistics *statistics,
                       const VfsOptions options);
bool UnregisterVfsRdOnly();

}  // namespace sqlite

#endif  // CVMFS_SQLITEVFS_H_
