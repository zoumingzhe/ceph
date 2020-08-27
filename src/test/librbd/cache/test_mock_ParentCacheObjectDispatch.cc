// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "test/librbd/test_mock_fixture.h"
#include "test/librbd/test_support.h"
#include "test/librbd/mock/MockImageCtx.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "include/Context.h"
#include "tools/immutable_object_cache/CacheClient.h"
#include "test/immutable_object_cache/MockCacheDaemon.h"
#include "librbd/io/Utils.h"
#include "librbd/cache/ParentCacheObjectDispatch.h"
#include "test/librbd/test_mock_fixture.h"
#include "test/librbd/mock/MockImageCtx.h"

using namespace ceph::immutable_obj_cache;

namespace librbd {

namespace {

struct MockParentImageCacheImageCtx : public MockImageCtx {
  MockParentImageCacheImageCtx(ImageCtx& image_ctx)
   : MockImageCtx(image_ctx) {
  }
  ~MockParentImageCacheImageCtx() {}
};

}; // anonymous namespace

namespace cache {

template<>
struct TypeTraits<MockParentImageCacheImageCtx> {
  typedef ceph::immutable_obj_cache::MockCacheClient CacheClient;
};

}; // namespace cache

namespace io {
namespace util {

namespace {

struct Mock {
  static Mock* s_instance;

  Mock() {
    s_instance = this;
  }

  MOCK_METHOD7(read_parent,
               void(librbd::MockParentImageCacheImageCtx *, uint64_t,
                    const io::Extents &, librados::snap_t, const ZTracer::Trace &,
                    ceph::bufferlist*, Context*));
};

Mock *Mock::s_instance = nullptr;

} // anonymous namespace

template<> void read_parent(
    librbd::MockParentImageCacheImageCtx *image_ctx, uint64_t object_no,
    const io::Extents &extents, librados::snap_t snap_id,
    const ZTracer::Trace &trace, ceph::bufferlist* data, Context* on_finish) {
  Mock::s_instance->read_parent(image_ctx, object_no, extents, snap_id, trace,
                                data, on_finish);
}

} // namespace util
} // namespace io

}; // namespace librbd

#include "librbd/cache/ParentCacheObjectDispatch.cc"
template class librbd::cache::ParentCacheObjectDispatch<librbd::MockParentImageCacheImageCtx>;

namespace librbd {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::WithArg;
using ::testing::WithArgs;

class TestMockParentCacheObjectDispatch : public TestMockFixture {
public :
  typedef cache::ParentCacheObjectDispatch<librbd::MockParentImageCacheImageCtx> MockParentImageCache;
  typedef io::util::Mock MockUtils;

  // ====== mock cache client ====
  void expect_cache_run(MockParentImageCache& mparent_image_cache, bool ret_val) {
    auto& expect = EXPECT_CALL(*(mparent_image_cache.get_cache_client()), run());

    expect.WillOnce((Invoke([]() {
    })));
  }

   void expect_cache_session_state(MockParentImageCache& mparent_image_cache, bool ret_val) {
     auto & expect = EXPECT_CALL(*(mparent_image_cache.get_cache_client()), is_session_work());

     expect.WillOnce((Invoke([ret_val]() {
        return ret_val;
     })));
   }

   void expect_cache_connect(MockParentImageCache& mparent_image_cache, int ret_val) {
     auto& expect = EXPECT_CALL(*(mparent_image_cache.get_cache_client()), connect());

     expect.WillOnce((Invoke([ret_val]() {
        return ret_val;
      })));
   }

   void expect_cache_async_connect(MockParentImageCache& mparent_image_cache, int ret_val,
                                   Context* on_finish) {
     auto& expect = EXPECT_CALL(*(mparent_image_cache.get_cache_client()), connect(_));

     expect.WillOnce(WithArg<0>(Invoke([on_finish, ret_val](Context* ctx) {
       ctx->complete(ret_val);
       on_finish->complete(ret_val);
     })));
   }

  void expect_cache_lookup_object(MockParentImageCache& mparent_image_cache,
                                  const std::string &cache_path) {
    EXPECT_CALL(*(mparent_image_cache.get_cache_client()),
                lookup_object(_, _, _, _, _))
      .WillOnce(WithArg<4>(Invoke([cache_path](CacheGenContextURef on_finish) {
        auto ack = new ObjectCacheReadReplyData(RBDSC_READ_REPLY, 0, cache_path);
        on_finish.release()->complete(ack);
      })));
  }

  void expect_read_parent(MockUtils &mock_utils, uint64_t object_no,
                          const io::Extents &extents, librados::snap_t snap_id,
                          int r) {
    EXPECT_CALL(mock_utils,
                read_parent(_, object_no, extents, snap_id, _, _, _))
      .WillOnce(WithArg<6>(CompleteContext(r, static_cast<asio::ContextWQ*>(nullptr))));
  }

  void expect_cache_close(MockParentImageCache& mparent_image_cache, int ret_val) {
    auto& expect = EXPECT_CALL(*(mparent_image_cache.get_cache_client()), close());

    expect.WillOnce((Invoke([]() {
    })));
  }

  void expect_cache_stop(MockParentImageCache& mparent_image_cache, int ret_val) {
    auto& expect = EXPECT_CALL(*(mparent_image_cache.get_cache_client()), stop());

    expect.WillOnce((Invoke([]() {
    })));
  }

  void expect_cache_register(MockParentImageCache& mparent_image_cache, Context* mock_handle_register, int ret_val) {
    auto& expect = EXPECT_CALL(*(mparent_image_cache.get_cache_client()), register_client(_));

    expect.WillOnce(WithArg<0>(Invoke([mock_handle_register, ret_val](Context* ctx) {
      if(ret_val == 0) {
        mock_handle_register->complete(true);
      } else {
        mock_handle_register->complete(false);
      }
      ctx->complete(true);
      return ret_val;
    })));
  }

  void expect_io_object_dispatcher_register_state(MockParentImageCache& mparent_image_cache,
                                                  int ret_val) {
    auto& expect = EXPECT_CALL((*(mparent_image_cache.get_image_ctx()->io_object_dispatcher)),
                               register_dispatch(_));

        expect.WillOnce(WithArg<0>(Invoke([&mparent_image_cache]
                (io::ObjectDispatchInterface* object_dispatch) {
          ASSERT_EQ(object_dispatch, &mparent_image_cache);
        })));
  }
};

TEST_F(TestMockParentCacheObjectDispatch, test_initialization_success) {
  librbd::ImageCtx* ictx;
  ASSERT_EQ(0, open_image(m_image_name, &ictx));
  MockParentImageCacheImageCtx mock_image_ctx(*ictx);
  mock_image_ctx.child = &mock_image_ctx;

  auto mock_parent_image_cache = MockParentImageCache::create(&mock_image_ctx);

  expect_cache_run(*mock_parent_image_cache, 0);
  C_SaferCond cond;
  Context* handle_connect = new LambdaContext([&cond](int ret) {
    ASSERT_EQ(ret, 0);
    cond.complete(0);
  });
  expect_cache_async_connect(*mock_parent_image_cache, 0, handle_connect);
  Context* ctx = new LambdaContext([](bool reg) {
    ASSERT_EQ(reg, true);
  });
  expect_cache_register(*mock_parent_image_cache, ctx, 0);
  expect_io_object_dispatcher_register_state(*mock_parent_image_cache, 0);
  expect_cache_close(*mock_parent_image_cache, 0);
  expect_cache_stop(*mock_parent_image_cache, 0);

  mock_parent_image_cache->init();
  cond.wait();

  ASSERT_EQ(mock_parent_image_cache->get_dispatch_layer(),
            io::OBJECT_DISPATCH_LAYER_PARENT_CACHE);
  expect_cache_session_state(*mock_parent_image_cache, true);
  ASSERT_EQ(mock_parent_image_cache->get_cache_client()->is_session_work(), true);

  mock_parent_image_cache->get_cache_client()->close();
  mock_parent_image_cache->get_cache_client()->stop();

  delete mock_parent_image_cache;
}

TEST_F(TestMockParentCacheObjectDispatch, test_initialization_fail_at_connect) {
  librbd::ImageCtx* ictx;
  ASSERT_EQ(0, open_image(m_image_name, &ictx));
  MockParentImageCacheImageCtx mock_image_ctx(*ictx);
  mock_image_ctx.child = &mock_image_ctx;

  auto mock_parent_image_cache = MockParentImageCache::create(&mock_image_ctx);

  expect_cache_run(*mock_parent_image_cache, 0);
  C_SaferCond cond;
  Context* handle_connect = new LambdaContext([&cond](int ret) {
    ASSERT_EQ(ret, -1);
    cond.complete(0);
  });
  expect_cache_async_connect(*mock_parent_image_cache, -1, handle_connect);
  expect_io_object_dispatcher_register_state(*mock_parent_image_cache, 0);
  expect_cache_session_state(*mock_parent_image_cache, false);
  expect_cache_close(*mock_parent_image_cache, 0);
  expect_cache_stop(*mock_parent_image_cache, 0);

  mock_parent_image_cache->init();

  // initialization fails.
  ASSERT_EQ(mock_parent_image_cache->get_dispatch_layer(),
            io::OBJECT_DISPATCH_LAYER_PARENT_CACHE);
  ASSERT_EQ(mock_parent_image_cache->get_cache_client()->is_session_work(), false);

  mock_parent_image_cache->get_cache_client()->close();
  mock_parent_image_cache->get_cache_client()->stop();

  delete mock_parent_image_cache;

}

TEST_F(TestMockParentCacheObjectDispatch, test_initialization_fail_at_register) {
  librbd::ImageCtx* ictx;
  ASSERT_EQ(0, open_image(m_image_name, &ictx));
  MockParentImageCacheImageCtx mock_image_ctx(*ictx);
  mock_image_ctx.child = &mock_image_ctx;

  auto mock_parent_image_cache = MockParentImageCache::create(&mock_image_ctx);

  expect_cache_run(*mock_parent_image_cache, 0);
  C_SaferCond cond;
  Context* handle_connect = new LambdaContext([&cond](int ret) {
    ASSERT_EQ(ret, 0);
    cond.complete(0);
  });
  expect_cache_async_connect(*mock_parent_image_cache, 0, handle_connect);
  Context* ctx = new LambdaContext([](bool reg) {
    ASSERT_EQ(reg, false);
  });
  expect_cache_register(*mock_parent_image_cache, ctx, -1);
  expect_io_object_dispatcher_register_state(*mock_parent_image_cache, 0);
  expect_cache_close(*mock_parent_image_cache, 0);
  expect_cache_stop(*mock_parent_image_cache, 0);

  mock_parent_image_cache->init();
  cond.wait();

  ASSERT_EQ(mock_parent_image_cache->get_dispatch_layer(),
            io::OBJECT_DISPATCH_LAYER_PARENT_CACHE);
  expect_cache_session_state(*mock_parent_image_cache, true);
  ASSERT_EQ(mock_parent_image_cache->get_cache_client()->is_session_work(), true);

  mock_parent_image_cache->get_cache_client()->close();
  mock_parent_image_cache->get_cache_client()->stop();

  delete mock_parent_image_cache;
}

TEST_F(TestMockParentCacheObjectDispatch, test_disble_interface) {
  librbd::ImageCtx* ictx;
  ASSERT_EQ(0, open_image(m_image_name, &ictx));
  MockParentImageCacheImageCtx mock_image_ctx(*ictx);
  mock_image_ctx.child = &mock_image_ctx;

  auto mock_parent_image_cache = MockParentImageCache::create(&mock_image_ctx);

  std::string temp_oid("12345");
  ceph::bufferlist temp_bl;
  ::SnapContext *temp_snapc = nullptr;
  io::DispatchResult* temp_dispatch_result = nullptr;
  io::Extents temp_buffer_extents;
  int* temp_op_flags = nullptr;
  uint64_t* temp_journal_tid = nullptr;
  Context** temp_on_finish = nullptr;
  Context* temp_on_dispatched = nullptr;
  ZTracer::Trace* temp_trace = nullptr;
  io::LightweightBufferExtents buffer_extents;

  ASSERT_EQ(mock_parent_image_cache->discard(0, 0, 0, *temp_snapc, 0, *temp_trace, temp_op_flags,
            temp_journal_tid, temp_dispatch_result, temp_on_finish, temp_on_dispatched), false);
  ASSERT_EQ(mock_parent_image_cache->write(0, 0, std::move(temp_bl), *temp_snapc, 0, 0, std::nullopt,
            *temp_trace, temp_op_flags, temp_journal_tid, temp_dispatch_result,
            temp_on_finish, temp_on_dispatched), false);
  ASSERT_EQ(mock_parent_image_cache->write_same(0, 0, 0, std::move(buffer_extents),
            std::move(temp_bl), *temp_snapc, 0, *temp_trace, temp_op_flags,
            temp_journal_tid, temp_dispatch_result, temp_on_finish, temp_on_dispatched), false );
  ASSERT_EQ(mock_parent_image_cache->compare_and_write(0, 0, std::move(temp_bl), std::move(temp_bl),
            *temp_snapc, 0, *temp_trace, temp_journal_tid, temp_op_flags, temp_journal_tid,
            temp_dispatch_result, temp_on_finish, temp_on_dispatched), false);
  ASSERT_EQ(mock_parent_image_cache->flush(io::FLUSH_SOURCE_USER, *temp_trace, temp_journal_tid,
            temp_dispatch_result, temp_on_finish, temp_on_dispatched), false);
  ASSERT_EQ(mock_parent_image_cache->invalidate_cache(nullptr), false);
  ASSERT_EQ(mock_parent_image_cache->reset_existence_cache(nullptr), false);

  delete mock_parent_image_cache;

}

TEST_F(TestMockParentCacheObjectDispatch, test_read) {
  librbd::ImageCtx* ictx;
  ASSERT_EQ(0, open_image(m_image_name, &ictx));
  MockParentImageCacheImageCtx mock_image_ctx(*ictx);
  mock_image_ctx.child = &mock_image_ctx;

  auto mock_parent_image_cache = MockParentImageCache::create(&mock_image_ctx);

  expect_cache_run(*mock_parent_image_cache, 0);
  C_SaferCond conn_cond;
  Context* handle_connect = new LambdaContext([&conn_cond](int ret) {
    ASSERT_EQ(ret, 0);
    conn_cond.complete(0);
  });
  expect_cache_async_connect(*mock_parent_image_cache, 0, handle_connect);
  Context* ctx = new LambdaContext([](bool reg) {
    ASSERT_EQ(reg, true);
  });
  expect_cache_register(*mock_parent_image_cache, ctx, 0);
  expect_io_object_dispatcher_register_state(*mock_parent_image_cache, 0);
  expect_cache_close(*mock_parent_image_cache, 0);
  expect_cache_stop(*mock_parent_image_cache, 0);

  mock_parent_image_cache->init();
  conn_cond.wait();

  ASSERT_EQ(mock_parent_image_cache->get_dispatch_layer(),
            io::OBJECT_DISPATCH_LAYER_PARENT_CACHE);
  expect_cache_session_state(*mock_parent_image_cache, true);
  ASSERT_EQ(mock_parent_image_cache->get_cache_client()->is_session_work(), true);

  auto& expect = EXPECT_CALL(*(mock_parent_image_cache->get_cache_client()), is_session_work());
  expect.WillOnce(Return(true));

  expect_cache_lookup_object(*mock_parent_image_cache, "/dev/null");

  C_SaferCond on_dispatched;
  io::DispatchResult dispatch_result;
  ceph::bufferlist read_data;
  mock_parent_image_cache->read(0, {{0, 4096}}, CEPH_NOSNAP, 0, {}, &read_data,
                                nullptr, nullptr, nullptr, &dispatch_result,
                                nullptr, &on_dispatched);
  ASSERT_EQ(0, on_dispatched.wait());

  mock_parent_image_cache->get_cache_client()->close();
  mock_parent_image_cache->get_cache_client()->stop();
  delete mock_parent_image_cache;
}

TEST_F(TestMockParentCacheObjectDispatch, test_read_dne) {
  librbd::ImageCtx* ictx;
  ASSERT_EQ(0, open_image(m_image_name, &ictx));
  MockParentImageCacheImageCtx mock_image_ctx(*ictx);
  mock_image_ctx.child = &mock_image_ctx;

  auto mock_parent_image_cache = MockParentImageCache::create(&mock_image_ctx);

  expect_cache_run(*mock_parent_image_cache, 0);
  C_SaferCond conn_cond;
  Context* handle_connect = new LambdaContext([&conn_cond](int ret) {
    ASSERT_EQ(ret, 0);
    conn_cond.complete(0);
  });
  expect_cache_async_connect(*mock_parent_image_cache, 0, handle_connect);
  Context* ctx = new LambdaContext([](bool reg) {
    ASSERT_EQ(reg, true);
  });
  expect_cache_register(*mock_parent_image_cache, ctx, 0);
  expect_io_object_dispatcher_register_state(*mock_parent_image_cache, 0);
  expect_cache_close(*mock_parent_image_cache, 0);
  expect_cache_stop(*mock_parent_image_cache, 0);

  mock_parent_image_cache->init();
  conn_cond.wait();

  ASSERT_EQ(mock_parent_image_cache->get_dispatch_layer(),
            io::OBJECT_DISPATCH_LAYER_PARENT_CACHE);
  expect_cache_session_state(*mock_parent_image_cache, true);
  ASSERT_EQ(mock_parent_image_cache->get_cache_client()->is_session_work(),
            true);

  EXPECT_CALL(*(mock_parent_image_cache->get_cache_client()), is_session_work())
    .WillOnce(Return(true));

  expect_cache_lookup_object(*mock_parent_image_cache, "");

  MockUtils mock_utils;
  expect_read_parent(mock_utils, 0, {{0, 4096}}, CEPH_NOSNAP, 0);

  C_SaferCond on_dispatched;
  io::DispatchResult dispatch_result;
  mock_parent_image_cache->read(0, {{0, 4096}}, CEPH_NOSNAP, 0, {}, nullptr,
                                nullptr, nullptr, nullptr, &dispatch_result,
                                nullptr, &on_dispatched);
  ASSERT_EQ(0, on_dispatched.wait());

  mock_parent_image_cache->get_cache_client()->close();
  mock_parent_image_cache->get_cache_client()->stop();
  delete mock_parent_image_cache;
}

}  // namespace librbd
