#include "beast/platform/bizutil/config/loader.hpp"
#include "beast/platform/bizutil/config/manifest.hpp"
#include "beast/platform/bizutil/config/store.hpp"
#include "beast/platform/bizutil/config/table_view.hpp"

#include "example_table.pb.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace beast::platform::bizutil::config {
namespace {

namespace fs = std::filesystem;

class BizConfigStoreTest : public ::testing::Test {
protected:
    fs::path temp_dir_;

    void SetUp() override {
        temp_dir_ = fs::temp_directory_path() / "beast_bizutil_test";
        fs::remove_all(temp_dir_);
        fs::create_directories(temp_dir_);
    }

    void TearDown() override {
        fs::remove_all(temp_dir_);
    }

    void write_manifest() const {
        std::ofstream manifest(temp_dir_ / "manifest.json");
        manifest << R"({
  "version": "test-1",
  "tables": {
    "example_table": {
      "file": "example_table.pb",
      "schema": "beast.biz.test.ExampleTableConfig",
      "row_count": 1
    }
  }
})";
    }

    void write_table_pb() const {
        beast::biz::test::ExampleTableConfig config;
        auto* row = config.add_rows();
        row->set_id(1001);
        row->set_index("npc1");
        row->set_name("test-npc");

        std::ofstream output(temp_dir_ / "example_table.pb", std::ios::binary);
        config.SerializeToOstream(&output);
    }
};

TEST_F(BizConfigStoreTest, LoadsRegisteredTableFromManifest) {
    write_manifest();
    write_table_pb();

    BizPaths paths;
    paths.server_dir = temp_dir_;
    paths.manifest_path = temp_dir_ / "manifest.json";

    const std::vector<BizTableRegistration> registrations = {
        BizTableRegistration{
            .logical_name = "example_table",
            .factory = []() { return std::make_unique<beast::biz::test::ExampleTableConfig>(); },
        },
    };

    BizConfigStore store;
    const LoadResult result = store.load(paths, registrations);
    ASSERT_TRUE(result.ok) << (result.errors.empty() ? "" : result.errors.front().to_string());
    ASSERT_TRUE(store.loaded());
    ASSERT_NE(store.manifest(), nullptr);
    EXPECT_EQ(store.manifest()->version(), "test-1");

    const auto& table = store.require<beast::biz::test::ExampleTableConfig>("example_table");
    ASSERT_EQ(table.rows_size(), 1);
    EXPECT_EQ(table.rows(0).id(), 1001u);
    EXPECT_EQ(table.rows(0).index(), "npc1");

    const auto& row_by_id = store.require_row_by_id<beast::biz::test::ExampleTableConfig, beast::biz::test::ExampleRow>(
        "example_table",
        1001);
    EXPECT_EQ(row_by_id.name(), "test-npc");

    const auto& row_by_index = store.require_row_by_index<
        beast::biz::test::ExampleTableConfig,
        beast::biz::test::ExampleRow>("example_table", "npc1");
    EXPECT_EQ(row_by_index.id(), 1001u);

    EXPECT_EQ((store.find_row_by_id<beast::biz::test::ExampleTableConfig, beast::biz::test::ExampleRow>(
                  "example_table", 9999)),
        nullptr);
    EXPECT_EQ((store.find_row_by_index<beast::biz::test::ExampleTableConfig, beast::biz::test::ExampleRow>(
                  "example_table", "missing")),
        nullptr);

    const std::vector<beast::biz::test::ExampleRow> rows(
        table.rows().begin(),
        table.rows().end());
    const auto view = BizTableView<beast::biz::test::ExampleTableConfig, beast::biz::test::ExampleRow>::bind(
        table,
        rows);
    ASSERT_NE(view.by_id().find(1001), nullptr);
    ASSERT_NE(view.by_key().find("npc1"), nullptr);
}

TEST(LoaderTest, RejectsMissingFile) {
    beast::biz::test::ExampleTableConfig config;
    BizError error;
    EXPECT_FALSE(Loader::load_pb_file("/tmp/does-not-exist-beast-biz.pb", config, &error));
    EXPECT_FALSE(error.message.empty());
}

} // namespace
} // namespace beast::platform::bizutil::config
