#include <gtest/gtest.h>
#include <merak/storage/image_service.hpp>
#include <merak/storage/local_file_image_store.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace merak;

class ImageServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = fs::temp_directory_path() / "merak_image_service_test";
        fs::remove_all(test_dir_);
        fs::create_directories(test_dir_);

        upload_temp_dir_ = test_dir_ / "chunked_uploads";
        fs::create_directories(upload_temp_dir_);

        store_ = std::make_shared<LocalFileImageStore>(test_dir_.string(), "/images");

        db_rows_ = nlohmann::json::array();
        id_counter_ = 0;

        auto db_query = [this](const std::string&, const nlohmann::json& params) -> nlohmann::json {
            // Filter by id if requested
            if (params.contains("id")) {
                std::string target_id = params["id"].get<std::string>();
                for (const auto& row : db_rows_) {
                    if (row.value("id", "") == target_id) {
                        return nlohmann::json::array({row});
                    }
                }
                return nlohmann::json::array();
            }
            // Filter by agent_id if requested
            if (params.contains("agent_id")) {
                std::string target_agent = params["agent_id"].get<std::string>();
                auto result = nlohmann::json::array();
                // Handle COUNT queries — count matching rows
                std::string target_type = params.value("image_type", "");
                for (const auto& row : db_rows_) {
                    if (row.value("agent_id", "") == target_agent) {
                        if (target_type.empty() || row.value("image_type", "") == target_type) {
                            result.push_back(row);
                        }
                    }
                }
                // If the query looks like a COUNT, return a count object
                // We detect by checking if the caller is asking about first-image determination
                // For the COUNT(*) case used in upload(), we do special handling:
                // The query is: SELECT COUNT(*) as cnt FROM images WHERE agent_id = @agent_id AND image_type = @image_type
                // We'll always return the array; the caller checks .is_array() and reads cnt from first element
                // But the actual SQL pattern uses COUNT(*). Let's handle both cases cleanly:
                // The upload() method checks for existing.is_array() && !existing.empty() && reads "cnt"
                // So we need to return objects with a "cnt" field.
                // Actually, looking at the upload() code: it expects:
                //   if (existing.is_array() && !existing.empty()) count = existing[0].value("cnt", 0);
                //   else if (existing.is_object()) count = existing.value("cnt", 0);
                // So for COUNT queries, we should return a single-element array with a "cnt" key.
                // But wait, our fake DB doesn't parse SQL. We need a heuristic:
                // If both agent_id and image_type are present AND we're not doing a regular list query,
                // return the count. The simplest approach: always return the matching array,
                // and the COUNT detection happens on the caller side.
                // Check: when upload() calls db_query_ for COUNT, it passes agent_id AND image_type.
                // When list_images calls db_query_, it also passes agent_id (and possibly image_type).
                // We need to distinguish. Simplest approach: when the caller is checking for
                // "first image existence" (upload's COUNT query), we return something
                // with cnt. When the caller is list_images, we return full rows.
                //
                // Since we can't distinguish SQL, let's use a different approach:
                // The upload() code checks existing[0].value("cnt", 0) which will return 0 if
                // the row doesn't have "cnt" — but it would still have agent_id etc.
                // For list_images, it reads rows with actual fields.
                //
                // Better: always return the matching array. For upload()'s COUNT query,
                // existing[0].value("cnt", 0) will be 0 since rows don't have "cnt".
                // That means upload will always set is_primary=true on first upload.
                // This is incorrect if there are already images.
                //
                // Let's take a different strategy: return full rows, and in a separate
                // in-memory "count answer" path handle the COUNT query.
                //
                // Actually the cleanest: always return the full array. The upload() code
                // will read existing[0].value("cnt", 0) → 0 (no cnt field) → thinks count=0.
                // This is wrong. We need cnt in the result.
                //
                // Let me re-read upload()'s count logic more carefully...
                //
                //   auto existing = db_query_(...);
                //   int count = 0;
                //   if (existing.is_array() && !existing.empty()) {
                //       count = existing[0].value("cnt", 0);
                //   } else if (existing.is_object()) {
                //       count = existing.value("cnt", 0);
                //   }
                //
                // If CNT is not present in the first element, count stays 0.
                // For the test fixture, we want upload to correctly determine if there's
                // an existing image. The solution: include "cnt" in the first returned element.
                // But then list_images would also see this and try to access "cnt".
                //
                // Alternative approach: modify our db_query lambda to detect COUNT pattern.
                // Since we can't parse SQL, we'll use a flag or return two-format result.
                //
                // Simplest solution: for the test fixture's db_query, when both agent_id
                // AND image_type are passed (COUNT query from upload), return the count.
                // When only agent_id is passed (list_images query), return full rows.
                // This works because:
                // - upload() passes both agent_id AND image_type
                // - list_images() passes only agent_id
                if (!target_type.empty()) {
                    // COUNT query pattern: return count
                    return nlohmann::json::array({nlohmann::json::object({{"cnt", result.size()}})});
                }
                return result;
            }
            return db_rows_;
        };

        auto db_exec = [this](const std::string&, const nlohmann::json& params) -> bool {
            if (params.contains("id")) {
                // Check if this is an INSERT or UPDATE
                // INSERT: params has many fields like agent_id, image_type, etc.
                // UPDATE: params has just id and maybe sort_order, or "set_primary" pattern
                bool is_insert = params.contains("agent_id") && params.contains("image_type") &&
                                 params.contains("storage_key");

                if (is_insert) {
                    nlohmann::json row;
                    row["id"] = params["id"];
                    row["agent_id"] = params.value("agent_id", "");
                    row["image_type"] = params.value("image_type", "");
                    row["storage_key"] = params.value("storage_key", "");
                    row["mime_type"] = params.value("mime_type", "");
                    row["original_name"] = params.value("original_name", "");
                    row["file_size_bytes"] = params.value("file_size_bytes", 0);
                    row["is_primary"] = params.value("is_primary", false);
                    row["sort_order"] = params.value("sort_order", 0);
                    row["created_at"] = params.value("created_at", "");
                    db_rows_.push_back(row);
                } else {
                    // UPDATE or DELETE
                    std::string target_id = params["id"].get<std::string>();

                    // Check for set_primary: unset all then set one
                    if (params.contains("agent_id") && params.contains("image_type")) {
                        // Unset all primaries for this agent + image_type
                        std::string agent = params["agent_id"].get<std::string>();
                        std::string type = params["image_type"].get<std::string>();
                        for (auto& row : db_rows_) {
                            if (row.value("agent_id", "") == agent &&
                                row.value("image_type", "") == type) {
                                row["is_primary"] = false;
                            }
                        }
                    } else if (params.contains("sort_order")) {
                        // Update sort_order
                        for (auto& row : db_rows_) {
                            if (row.value("id", "") == target_id) {
                                row["sort_order"] = params["sort_order"].get<int>();
                                break;
                            }
                        }
                    } else if (params.contains("is_primary")) {
                        // Set specific row as primary (from set_primary's second query)
                        for (auto& row : db_rows_) {
                            if (row.value("id", "") == target_id) {
                                row["is_primary"] = params["is_primary"].get<bool>();
                                break;
                            }
                        }
                    } else {
                        // DELETE
                        auto it = db_rows_.begin();
                        while (it != db_rows_.end()) {
                            if ((*it).value("id", "") == target_id) {
                                it = db_rows_.erase(it);
                            } else {
                                ++it;
                            }
                        }
                    }
                }
            }
            return true;
        };

        auto id_gen = [this]() -> std::string {
            return "img_" + std::to_string(++id_counter_);
        };

        auto ts = []() -> std::string {
            return "2026-06-11T12:00:00Z";
        };

        image_service_ = std::make_unique<ImageService>(
            store_,
            std::move(db_query),
            std::move(db_exec),
            std::move(id_gen),
            std::move(ts),
            upload_temp_dir_.string()
        );
    }

    void TearDown() override {
        image_service_.reset();
        store_.reset();
        fs::remove_all(test_dir_);
    }

    std::vector<unsigned char> make_bytes(const std::string& s) {
        return std::vector<unsigned char>(s.begin(), s.end());
    }

    fs::path test_dir_;
    fs::path upload_temp_dir_;
    std::shared_ptr<LocalFileImageStore> store_;
    std::unique_ptr<ImageService> image_service_;
    nlohmann::json db_rows_;
    int id_counter_ = 0;
};

TEST_F(ImageServiceTest, SimpleUpload) {
    auto data = make_bytes("fake-png-data");
    auto rec = image_service_->upload("world1", "agent1", "avatar", "photo.png", "image/png", data);

    // Verify record fields
    EXPECT_FALSE(rec.id.empty());
    EXPECT_EQ(rec.agent_id, "agent1");
    EXPECT_EQ(rec.image_type, "avatar");
    EXPECT_EQ(rec.mime_type, "image/png");
    EXPECT_EQ(rec.original_name, "photo.png");
    EXPECT_EQ(rec.file_size_bytes, 13);
    EXPECT_TRUE(rec.is_primary);  // first avatar, should be primary
    EXPECT_EQ(rec.sort_order, 0);
    EXPECT_EQ(rec.created_at, "2026-06-11T12:00:00Z");

    // Verify file exists on disk
    EXPECT_TRUE(fs::exists(test_dir_ / rec.storage_key));

    // Verify storage key format: agent_id/image_id.ext
    EXPECT_NE(rec.storage_key.find("agent1/img_"), std::string::npos);
    EXPECT_NE(rec.storage_key.find(".png"), std::string::npos);
}

TEST_F(ImageServiceTest, SecondUploadNotPrimary) {
    auto data1 = make_bytes("first-image");
    auto rec1 = image_service_->upload("world1", "agent1", "avatar", "first.png", "image/png", data1);
    EXPECT_TRUE(rec1.is_primary);

    auto data2 = make_bytes("second-image");
    auto rec2 = image_service_->upload("world1", "agent1", "avatar", "second.png", "image/png", data2);
    EXPECT_FALSE(rec2.is_primary);
}

TEST_F(ImageServiceTest, ListImages) {
    auto data1 = make_bytes("img1");
    auto data2 = make_bytes("img2");
    image_service_->upload("world1", "agent1", "avatar", "a.png", "image/png", data1);
    image_service_->upload("world1", "agent1", "avatar", "b.png", "image/png", data2);

    auto images = image_service_->list_images("agent1");
    ASSERT_EQ(images.size(), 2);
}

TEST_F(ImageServiceTest, GetImage) {
    auto data = make_bytes("test-data");
    auto uploaded = image_service_->upload("world1", "agent1", "avatar", "test.png", "image/png", data);

    auto found = image_service_->get_image(uploaded.id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->id, uploaded.id);
    EXPECT_EQ(found->agent_id, "agent1");

    auto missing = image_service_->get_image("nonexistent");
    EXPECT_FALSE(missing.has_value());
}

TEST_F(ImageServiceTest, DeleteImage) {
    auto data = make_bytes("delete-me");
    auto rec = image_service_->upload("world1", "agent1", "avatar", "del.png", "image/png", data);

    // Verify file exists
    EXPECT_TRUE(fs::exists(test_dir_ / rec.storage_key));

    image_service_->delete_image(rec.id);

    // Verify file removed
    EXPECT_FALSE(fs::exists(test_dir_ / rec.storage_key));

    // Verify DB record removed
    auto found = image_service_->get_image(rec.id);
    EXPECT_FALSE(found.has_value());
}

TEST_F(ImageServiceTest, DeleteNonExistentImage) {
    // Should not throw
    EXPECT_NO_THROW(image_service_->delete_image("nonexistent"));
}

TEST_F(ImageServiceTest, SetPrimary) {
    auto data1 = make_bytes("img1");
    auto rec1 = image_service_->upload("world1", "agent1", "avatar", "a.png", "image/png", data1);
    EXPECT_TRUE(rec1.is_primary);

    auto data2 = make_bytes("img2");
    auto rec2 = image_service_->upload("world1", "agent1", "avatar", "b.png", "image/png", data2);
    EXPECT_FALSE(rec2.is_primary);

    // Set rec2 as primary
    image_service_->update_image(rec2.id, true, std::nullopt);

    auto updated1 = image_service_->get_image(rec1.id);
    auto updated2 = image_service_->get_image(rec2.id);
    ASSERT_TRUE(updated1.has_value());
    ASSERT_TRUE(updated2.has_value());
    EXPECT_FALSE(updated1->is_primary);
    EXPECT_TRUE(updated2->is_primary);
}

TEST_F(ImageServiceTest, UpdateSortOrder) {
    auto data = make_bytes("img");
    auto rec = image_service_->upload("world1", "agent1", "avatar", "sort.png", "image/png", data);

    image_service_->update_image(rec.id, std::nullopt, 42);

    auto updated = image_service_->get_image(rec.id);
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->sort_order, 42);
}

TEST_F(ImageServiceTest, ChunkedUploadLifecycle) {
    auto chunk1 = make_bytes("AAAA");
    auto chunk2 = make_bytes("BBBB");
    auto chunk3 = make_bytes("CCCC");

    auto state = image_service_->init_chunked(
        "world1", "agent1", "avatar", "big.png", "image/png",
        12,  // total_size = 4*3
        4    // chunk_size = 4
    );

    EXPECT_EQ(state.total_size, 12);
    EXPECT_EQ(state.chunk_size, 4);
    EXPECT_EQ(state.chunks_total, 3);
    EXPECT_FALSE(state.upload_id.empty());
    EXPECT_TRUE(fs::exists(state.temp_dir));

    image_service_->upload_chunk(state.upload_id, 0, chunk1);
    image_service_->upload_chunk(state.upload_id, 1, chunk2);
    image_service_->upload_chunk(state.upload_id, 2, chunk3);

    auto uploaded = image_service_->uploaded_chunks(state.upload_id);
    EXPECT_EQ(uploaded.size(), 3);
    EXPECT_TRUE(uploaded.count(0));
    EXPECT_TRUE(uploaded.count(1));
    EXPECT_TRUE(uploaded.count(2));

    auto rec = image_service_->complete_chunked(state.upload_id);

    EXPECT_EQ(rec.file_size_bytes, 12);
    EXPECT_EQ(rec.agent_id, "agent1");
    EXPECT_EQ(rec.image_type, "avatar");

    // Verify combined data
    auto loaded = store_->load(rec.storage_key);
    std::string content(loaded.bytes.begin(), loaded.bytes.end());
    EXPECT_EQ(content, "AAAABBBBCCCC");

    // Temp dir should be cleaned up
    EXPECT_FALSE(fs::exists(state.temp_dir));
}

TEST_F(ImageServiceTest, ChunkedUploadResume) {
    auto chunk1 = make_bytes("1111");
    auto chunk2 = make_bytes("2222");
    auto chunk3 = make_bytes("3333");

    auto state = image_service_->init_chunked(
        "world1", "agent1", "avatar", "resume.png", "image/png",
        12, 4
    );

    // Upload chunk 0
    image_service_->upload_chunk(state.upload_id, 0, chunk1);
    auto after_one = image_service_->uploaded_chunks(state.upload_id);
    EXPECT_EQ(after_one.size(), 1);
    EXPECT_TRUE(after_one.count(0));

    // Simulate querying what chunks are already uploaded (resume scenario)
    auto known_chunks = image_service_->uploaded_chunks(state.upload_id);

    // Upload remaining chunks (only those not already uploaded)
    if (known_chunks.count(1) == 0) {
        image_service_->upload_chunk(state.upload_id, 1, chunk2);
    }
    if (known_chunks.count(2) == 0) {
        image_service_->upload_chunk(state.upload_id, 2, chunk3);
    }

    auto rec = image_service_->complete_chunked(state.upload_id);
    EXPECT_EQ(rec.file_size_bytes, 12);

    auto loaded = store_->load(rec.storage_key);
    std::string content(loaded.bytes.begin(), loaded.bytes.end());
    EXPECT_EQ(content, "111122223333");
}

TEST_F(ImageServiceTest, CancelChunkedCleansUp) {
    auto chunk = make_bytes("data");
    auto state = image_service_->init_chunked(
        "world1", "agent1", "avatar", "cancel.png", "image/png",
        4, 4
    );

    image_service_->upload_chunk(state.upload_id, 0, chunk);

    auto temp_dir = state.temp_dir;
    EXPECT_TRUE(fs::exists(temp_dir));

    image_service_->cancel_chunked(state.upload_id);

    // Temp dir should be removed
    EXPECT_FALSE(fs::exists(temp_dir));
}

TEST_F(ImageServiceTest, CompleteChunkedRequiresAllChunks) {
    auto chunk = make_bytes("AAAA");
    auto state = image_service_->init_chunked(
        "world1", "agent1", "avatar", "incomplete.png", "image/png",
        8,   // total_size = 8 (expecting 2 chunks)
        4    // chunk_size = 4
    );

    // Only upload chunk 0, not chunk 1
    image_service_->upload_chunk(state.upload_id, 0, chunk);

    // complete_chunked should throw because chunk 1 is missing
    EXPECT_THROW(image_service_->complete_chunked(state.upload_id), std::runtime_error);
}

TEST_F(ImageServiceTest, PublicUrl) {
    auto data = make_bytes("url-test");
    auto rec = image_service_->upload("world1", "agent1", "avatar", "url.png", "image/png", data);

    std::string url = image_service_->public_url(rec.storage_key);
    EXPECT_EQ(url, "/images/" + rec.storage_key);
}

TEST_F(ImageServiceTest, LoadImageData) {
    auto data = make_bytes("load-test-data");
    auto rec = image_service_->upload("world1", "agent1", "avatar", "load.png", "image/png", data);

    auto loaded = image_service_->load_image_data(rec.storage_key);
    EXPECT_EQ(loaded.bytes, data);
    EXPECT_EQ(loaded.mime_type, "image/png");
}
