/**
 * This file is part of the CernVM File System.
 */

#include <gtest/gtest.h>

#include "fuse_evict.h"
#include "glue_buffer.h"
#include "util/string.h"

extern "C" {
unsigned fuse_lowlevel_notify_inval_inode_cnt = 0;
}

class T_FuseInvalidator : public ::testing::Test {
 protected:
  virtual void SetUp() {
    invalidator_ = new FuseInvalidator(&inode_tracker_, NULL);
    invalidator_->Spawn();
  }

  virtual void TearDown() {
    delete invalidator_;
  }

 protected:
  glue::InodeTracker inode_tracker_;
  FuseInvalidator *invalidator_;
};


TEST_F(T_FuseInvalidator, StartStop) {
  FuseInvalidator *idle_invalidator =
    new FuseInvalidator(&inode_tracker_, NULL);
  EXPECT_FALSE(idle_invalidator->spawned_);
  delete idle_invalidator;

  FuseInvalidator *noop_invalidator =
    new FuseInvalidator(&inode_tracker_, NULL);
  noop_invalidator->Spawn();
  EXPECT_TRUE(noop_invalidator->spawned_);
  EXPECT_GE(noop_invalidator->pipe_ctrl_[0], 0);
  EXPECT_GE(noop_invalidator->pipe_ctrl_[1], 0);
  delete noop_invalidator;
}


TEST_F(T_FuseInvalidator, InvalidateTimeout) {
  FuseInvalidator::Handle handle(0);
  EXPECT_FALSE(handle.IsDone());
  invalidator_->InvalidateInodes(&handle);
  handle.WaitFor();
  EXPECT_TRUE(handle.IsDone());

  invalidator_->terminated_ = 1;
  FuseInvalidator::Handle handle2(1000000);
  EXPECT_FALSE(handle2.IsDone());
  invalidator_->InvalidateInodes(&handle2);
  handle2.WaitFor();
  EXPECT_TRUE(handle2.IsDone());
}


TEST_F(T_FuseInvalidator, InvalidateOps) {
  invalidator_->fuse_channel_ = reinterpret_cast<struct fuse_chan **>(this);
  inode_tracker_.VfsGet(1, PathString(""));
  for (unsigned i = 2; i <= 1024; ++i) {
    inode_tracker_.VfsGet(i, PathString("/" + StringifyInt(i)));
  }

  FuseInvalidator::Handle handle(0);
  EXPECT_FALSE(handle.IsDone());
  invalidator_->InvalidateInodes(&handle);
  handle.WaitFor();
  EXPECT_TRUE(handle.IsDone());
  EXPECT_EQ(FuseInvalidator::kCheckTimeoutFreqOps,
            fuse_lowlevel_notify_inval_inode_cnt);

  FuseInvalidator::Handle handle2(1000000);
  EXPECT_FALSE(handle2.IsDone());
  invalidator_->InvalidateInodes(&handle2);
  handle2.WaitFor();
  EXPECT_TRUE(handle2.IsDone());
  EXPECT_EQ(FuseInvalidator::kCheckTimeoutFreqOps + 1024,
            fuse_lowlevel_notify_inval_inode_cnt);

  invalidator_->terminated_ = 1;
  handle2.Reset();
  EXPECT_FALSE(handle2.IsDone());
  invalidator_->InvalidateInodes(&handle2);
  handle2.WaitFor();
  EXPECT_TRUE(handle2.IsDone());
  EXPECT_EQ((2 * FuseInvalidator::kCheckTimeoutFreqOps) + 1024,
            fuse_lowlevel_notify_inval_inode_cnt);
}
