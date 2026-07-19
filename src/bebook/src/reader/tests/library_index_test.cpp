#include "../library_index.h"
#include "../state_store.h"

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>

namespace
{

// The index is defined by what is on disk, so the tests need a real directory pair.
class LibraryIndexTest : public ::testing::Test
{
protected:
    std::filesystem::path root;
    std::filesystem::path store_dir;
    std::filesystem::path books_dir;

    void SetUp() override
    {
        root = std::filesystem::temp_directory_path() /
            ("bebook_library_test_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
             "_" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(root);

        store_dir = root / "store";
        books_dir = root / "books";
        std::filesystem::create_directories(books_dir);
    }

    void TearDown() override
    {
        std::filesystem::remove_all(root);
    }

    std::filesystem::path write_book(const std::string &name, const std::string &contents)
    {
        auto path = books_dir / name;
        std::ofstream fp(path);
        fp << contents;
        fp.close();
        return path;
    }
};

} // namespace

TEST_F(LibraryIndexTest, unindexed_book_is_stale_and_titled_by_stem)
{
    auto path = write_book("some book.txt", "hello");

    StateStore store(store_dir);
    LibraryIndex index(store, books_dir);

    ASSERT_EQ(1u, index.entries().size());
    EXPECT_EQ("some book", index.entries()[0].title);
    EXPECT_EQ(std::vector<std::filesystem::path>{path}, index.stale_paths());
}

TEST_F(LibraryIndexTest, unsupported_files_are_ignored)
{
    write_book("manual.pdf", "hello");

    StateStore store(store_dir);
    LibraryIndex index(store, books_dir);

    EXPECT_TRUE(index.entries().empty());
}

TEST_F(LibraryIndexTest, indexed_book_is_not_stale_until_it_changes)
{
    auto path = write_book("book.txt", "hello");

    {
        StateStore store(store_dir);
        LibraryIndex index(store, books_dir);
        index.index_one(path);
        EXPECT_TRUE(index.stale_paths().empty());
        index.flush();
        store.flush();
    }

    // A fresh index over an unchanged directory does no work at all.
    {
        StateStore store(store_dir);
        LibraryIndex index(store, books_dir);
        ASSERT_EQ(1u, index.entries().size());
        EXPECT_FALSE(index.entries()[0].book_id.empty());
        EXPECT_TRUE(index.stale_paths().empty());
    }

    // Size change.
    write_book("book.txt", "hello there");
    {
        StateStore store(store_dir);
        LibraryIndex index(store, books_dir);
        EXPECT_EQ(std::vector<std::filesystem::path>{path}, index.stale_paths());
    }
}

TEST_F(LibraryIndexTest, mtime_change_alone_makes_a_book_stale)
{
    auto path = write_book("book.txt", "hello");

    StateStore store(store_dir);
    LibraryIndex index(store, books_dir);
    index.index_one(path);
    ASSERT_TRUE(index.stale_paths().empty());

    std::filesystem::last_write_time(
        path,
        std::filesystem::last_write_time(path) + std::chrono::hours(1)
    );

    EXPECT_EQ(std::vector<std::filesystem::path>{path}, index.stale_paths());
}

TEST_F(LibraryIndexTest, deleted_book_drops_out_of_the_index)
{
    auto path = write_book("book.txt", "hello");

    {
        StateStore store(store_dir);
        LibraryIndex index(store, books_dir);
        index.index_one(path);
        index.flush();
    }

    std::filesystem::remove(path);

    StateStore store(store_dir);
    LibraryIndex index(store, books_dir);
    EXPECT_TRUE(index.entries().empty());
}

TEST_F(LibraryIndexTest, recents_are_sorted_by_last_opened)
{
    auto first = write_book("first.txt", "a");
    auto second = write_book("second.txt", "b");
    write_book("never.txt", "c");

    StateStore store(store_dir);
    LibraryIndex index(store, books_dir);

    index.note_opened(first, 10);
    index.note_opened(second, 20);
    index.note_opened(first, 30);

    auto recents = index.recents(5);
    ASSERT_EQ(2u, recents.size());
    EXPECT_EQ(first, recents[0].path);
    EXPECT_EQ(30, recents[0].progress_percent);
    EXPECT_EQ(second, recents[1].path);

    EXPECT_EQ(1u, index.recents(1).size());
}

TEST_F(LibraryIndexTest, titles_and_progress_survive_a_round_trip)
{
    auto path = write_book("book.txt", "hello\tworld");

    {
        StateStore store(store_dir);
        LibraryIndex index(store, books_dir);
        index.index_one(path);
        index.note_opened(path, 42);
        index.flush();
    }

    StateStore store(store_dir);
    LibraryIndex index(store, books_dir);
    ASSERT_EQ(1u, index.entries().size());
    EXPECT_EQ("book", index.entries()[0].title);
    EXPECT_EQ(42, index.entries()[0].progress_percent);
    EXPECT_GT(index.entries()[0].last_opened, 0);
}
