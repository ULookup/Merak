#include <gtest/gtest.h>
#include <merak/storage/local_file_image_store.hpp>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace merak;

class LocalFileImageStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = fs::temp_directory_path() / "merak_imagestore_test";
        fs::remove_all(test_dir_);
        fs::create_directories(test_dir_);
        store_ = std::make_unique<LocalFileImageStore>(test_dir_.string(), "/images");
    }

    void TearDown() override {
        store_.reset();
        fs::remove_all(test_dir_);
    }

    fs::path test_dir_;
    std::unique_ptr<LocalFileImageStore> store_;
};

TEST_F(LocalFileImageStoreTest, SaveAndLoadPng) {
    ImageData data;
    data.bytes = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 'h', 'e', 'l', 'l', 'o'};
    data.mime_type = "image/png";

    std::string key = store_->save("test/agent1/avatar.png", data);

    EXPECT_EQ(key, "test/agent1/avatar.png");
    EXPECT_TRUE(fs::exists(test_dir_ / key));

    auto loaded = store_->load(key);
    EXPECT_EQ(loaded.bytes, data.bytes);
    EXPECT_EQ(loaded.mime_type, data.mime_type);
}

TEST_F(LocalFileImageStoreTest, RemoveFile) {
    ImageData data;
    data.bytes = {'a', 'b', 'c'};
    data.mime_type = "text/plain";

    std::string key = store_->save("rm_test/file.txt", data);
    EXPECT_TRUE(fs::exists(test_dir_ / key));

    store_->remove(key);
    EXPECT_FALSE(fs::exists(test_dir_ / key));
}

TEST_F(LocalFileImageStoreTest, RemoveDoesNotThrowWhenMissing) {
    EXPECT_NO_THROW(store_->remove("nonexistent/file.png"));
}

TEST_F(LocalFileImageStoreTest, PublicUrl) {
    std::string url = store_->public_url("w1/agent1/img1.png");
    EXPECT_EQ(url, "/images/w1/agent1/img1.png");
}

TEST_F(LocalFileImageStoreTest, CreatesParentDirectories) {
    ImageData data;
    data.bytes = {'x'};
    data.mime_type = "text/plain";

    std::string key = store_->save("deep/nested/path/file.txt", data);
    EXPECT_TRUE(fs::exists(test_dir_ / key));
}
