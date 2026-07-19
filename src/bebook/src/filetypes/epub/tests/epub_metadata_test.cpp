#include "../epub_metadata.h"

#include <gtest/gtest.h>

TEST(EPUB_METADATA, epub_parse_ncx__invalid_xml)
{
    std::vector<NavPoint> navmap;
    ASSERT_FALSE(epub_parse_ncx("root/toc.ncx", "", navmap));
    ASSERT_TRUE(navmap.empty());
}

TEST(EPUB_METADATA, epub_parse_ncx__navmap_not_found)
{
    const char *xml = (
        "<?xml version='1.0' encoding='utf-8'?>"
        "<ncx>"
        "</ncx>"
    );
    std::vector<NavPoint> navmap;
    ASSERT_FALSE(epub_parse_ncx("root/toc.ncx", xml, navmap));
    ASSERT_TRUE(navmap.empty());
}

TEST(EPUB_METADATA, epub_parse_ncx__parses_correct)
{
    const char *xml = (
        "<?xml version='1.0' encoding='utf-8'?>"
        "<ncx>"
          "<head></head>"
          "<docTitle></docTitle>"
          "<navMap>"
            "<navPoint>"
              "<navLabel>"
                "<text>Top 1</text>"
              "</navLabel>"
              "<content src=\"subdir/top1.html\"/>"
              "<navPoint>"
                "<navLabel>"
                  "<text>  Sub 2  </text>"
                "</navLabel>"
                "<content src=\"subdir/sub2.html#fragment\"/>"
              "</navPoint>"
            "</navPoint>"
            "<navPoint>"
              "<navLabel>"
                "<text>Top 3</text>"
              "</navLabel>"
              "<content src=\"sub3.html\"/>"
            "</navPoint>"
          "</navMap>"
        "</ncx>"
    );

    std::vector<NavPoint> expected_navmap = {
        {
            "Top 1",
            "subdir/top1.html",
            "root/subdir/top1.html",
            {
                {
                    "Sub 2",
                    "subdir/sub2.html#fragment",
                    "root/subdir/sub2.html#fragment"
                }
            }
        },
        {
            "Top 3",
            "sub3.html",
            "root/sub3.html"
        }
    };

    
    std::vector<NavPoint> navmap;
    ASSERT_TRUE(epub_parse_ncx("root/toc.ncx", xml, navmap));
    ASSERT_EQ(navmap, expected_navmap);
}

TEST(EPUB_METADATA, epub_parse_ncx__ignores_incomplete_navpoints)
{
    const char *xml = (
        "<?xml version='1.0' encoding='utf-8'?>"
        "<ncx>"
          "<navMap>"
            "<navPoint>"
              "<navLabel>"
                "<text>Correct</text>"
              "</navLabel>"
              "<content src=\"correct.html\"/>"
            "</navPoint>"
            "<navPoint>"
              "<navLabel>"
              "</navLabel>"
              "<content src=\"missing_label.html\"/>"
            "</navPoint>"
            "<navPoint>"
              "<content src=\"missing_label.html\"/>"
            "</navPoint>"
            "<navPoint>"
              "<navLabel>"
                "<text>Missing content</text>"
              "</navLabel>"
            "</navPoint>"
            "<navPoint>"
              "<navLabel>"
                "<text>Missing content attr</text>"
              "</navLabel>"
              "<content />"
            "</navPoint>"
            "<navPoint>"
              "<navLabel>"
                "<text>Content attr empty</text>"
              "</navLabel>"
              "<content src=\"\"/>"
            "</navPoint>"
          "</navMap>"
        "</ncx>"
    );

    std::vector<NavPoint> expected_navmap = {
        {
            "Correct",
            "correct.html",
            "root/correct.html"
        }
    };
    
    std::vector<NavPoint> navmap;
    ASSERT_TRUE(epub_parse_ncx("root/toc.ncx", xml, navmap));
    ASSERT_EQ(navmap, expected_navmap);
}


TEST(EPUB_METADATA, epub_parse_nav__invalid_xml)
{
    const char *xml = "";
    std::vector<NavPoint> navmap;
    ASSERT_FALSE(epub_parse_nav("root/nav.xhtml", xml, navmap));
    ASSERT_TRUE(navmap.empty());
}

TEST(EPUB_METADATA, epub_parse_nav__missing)
{
    const char *xml = (
        "<html>"
        "    <head>"
        "    </head>"
        "    <body>"
        "    </body>"
        "</html>"
    );
    std::vector<NavPoint> navmap;
    ASSERT_FALSE(epub_parse_nav("root/nav.xhtml", xml, navmap));
    ASSERT_TRUE(navmap.empty());
}

TEST(EPUB_METADATA, epub_parse_nav__basic)
{
    const char *xml = (
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\">"
        "    <head>"
        "    </head>"
        "    <body>"
        "        <nav epub:type=\"page-list\">"
        "        </nav>"
        "        <nav epub:type=\"toc\">"
        "            <h1>Contents</h1>"
        "            <ol class=\"none\">"
        "                <li><a href=\"file1.xhtml\">Item 1</a></li>"
        "                <li><a href=\"file2.xhtml\">Item 2</a></li>"
        "                <li><span><span>Item</span> <span>3</span></span></li>"
        "            </ol>"
        "        </nav>"
        "    </body>"
        "</html>"
    );

    std::vector<NavPoint> expected_navmap = {
        {
            "Item 1",
            "file1.xhtml",
            "root/file1.xhtml"
        },
        {
            "Item 2",
            "file2.xhtml",
            "root/file2.xhtml"
        },
        {
            "Item 3",
            "",
            ""
        }
    };

    std::vector<NavPoint> navmap;
    ASSERT_TRUE(epub_parse_nav("root/nav.xhtml", xml, navmap));
    ASSERT_EQ(navmap, expected_navmap);
}

TEST(EPUB_METADATA, epub_parse_nav__nested)
{
    const char *xml = (
        "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\">"
        "<body>"
        "    <nav epub:type=\"toc\">"
        "        <ol>"
        "            <li>"
        "                <a href=\"file1.xhtml\">Item 1</a>"
        "                <ol>"
        "                    <li><a href=\"file2.xhtml\">Item 2</a></li>"
        "                    <li><a href=\"file3.xhtml\">Item 3</a></li>"
        "                </ol>"
        "            </li>"
        "            <li><a href=\"file4.xhtml\">Item 4</a></li>"
        "        </ol>"
        "    </nav>"
        "</body></html>"
    );

    std::vector<NavPoint> expected_navmap = {
        {
            "Item 1",
            "file1.xhtml",
            "root/file1.xhtml",
            {
                {
                    "Item 2",
                    "file2.xhtml",
                    "root/file2.xhtml"
                },
                {
                    "Item 3",
                    "file3.xhtml",
                    "root/file3.xhtml"
                },
            },
        },
        {
            "Item 4",
            "file4.xhtml",
            "root/file4.xhtml"
        },
    };

    std::vector<NavPoint> navmap;
    ASSERT_TRUE(epub_parse_nav("root/nav.xhtml", xml, navmap));
    ASSERT_EQ(navmap, expected_navmap);
}

namespace
{

const char *PACKAGE_XML_EPUB2 = (
    "<?xml version='1.0' encoding='utf-8'?>"
    "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"2.0\">"
      "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:opf=\"http://www.idpf.org/2007/opf\">"
        "<dc:title>  Pride and Prejudice  </dc:title>"
        "<dc:creator opf:role=\"edt\">Some Editor</dc:creator>"
        "<dc:creator opf:role=\"aut\">Jane Austen</dc:creator>"
        "<meta name=\"calibre:series\" content=\"nope\"/>"
        "<meta name=\"cover\" content=\"cover-img\"/>"
      "</metadata>"
      "<manifest>"
        "<item id=\"cover-img\" href=\"images/cover.jpg\" media-type=\"image/jpeg\"/>"
        "<item id=\"ch1\" href=\"ch1.html\" media-type=\"application/xhtml+xml\"/>"
      "</manifest>"
      "<spine toc=\"ncx\">"
        "<itemref idref=\"ch1\"/>"
      "</spine>"
    "</package>"
);

const char *PACKAGE_XML_EPUB3 = (
    "<?xml version='1.0' encoding='utf-8'?>"
    "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\">"
      "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
        "<dc:title>Moby Dick</dc:title>"
        "<dc:creator>Herman Melville</dc:creator>"
      "</metadata>"
      "<manifest>"
        "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav scripted\"/>"
        "<item id=\"decoy\" href=\"images/not.jpg\" media-type=\"image/jpeg\" properties=\"not-cover-image\"/>"
        "<item id=\"img\" href=\"images/front.png\" media-type=\"image/png\" properties=\"svg cover-image\"/>"
      "</manifest>"
      "<spine/>"
    "</package>"
);

} // namespace

TEST(EPUB_METADATA, epub_parse_package_contents__epub2_metadata)
{
    PackageContents package;
    ASSERT_TRUE(epub_parse_package_contents("root/content.opf", PACKAGE_XML_EPUB2, package));

    EXPECT_EQ("Pride and Prejudice", package.metadata.title);
    EXPECT_EQ("Jane Austen", package.metadata.author);
    EXPECT_EQ("root/images/cover.jpg", package.metadata.cover_href);
}

TEST(EPUB_METADATA, epub_parse_package_contents__epub3_cover_properties)
{
    PackageContents package;
    ASSERT_TRUE(epub_parse_package_contents("content.opf", PACKAGE_XML_EPUB3, package));

    EXPECT_EQ("Moby Dick", package.metadata.title);
    EXPECT_EQ("Herman Melville", package.metadata.author);
    EXPECT_EQ("images/front.png", package.metadata.cover_href);
}

TEST(EPUB_METADATA, epub_parse_package_contents__undeclared_dc_prefix)
{
    // Producers that never declare xmlns:dc leave libxml2 reporting the qualified name.
    const char *xml = (
        "<?xml version='1.0' encoding='utf-8'?>"
        "<package>"
          "<metadata>"
            "<dc:title>Sloppy</dc:title>"
            "<dc:creator>Anon</dc:creator>"
          "</metadata>"
          "<manifest/>"
          "<spine/>"
        "</package>"
    );

    PackageContents package;
    ASSERT_TRUE(epub_parse_package_contents("content.opf", xml, package));

    EXPECT_EQ("Sloppy", package.metadata.title);
    EXPECT_EQ("Anon", package.metadata.author);
}

TEST(EPUB_METADATA, epub_parse_package_contents__no_metadata)
{
    const char *xml = (
        "<?xml version='1.0' encoding='utf-8'?>"
        "<package>"
          "<manifest>"
            "<item id=\"ch1\" href=\"ch1.html\" media-type=\"application/xhtml+xml\"/>"
          "</manifest>"
          "<spine/>"
        "</package>"
    );

    PackageContents package;
    ASSERT_TRUE(epub_parse_package_contents("content.opf", xml, package));

    EXPECT_TRUE(package.metadata.title.empty());
    EXPECT_TRUE(package.metadata.author.empty());
    EXPECT_TRUE(package.metadata.cover_href.empty());
}

TEST(EPUB_METADATA, epub_parse_package_contents__dangling_cover_meta)
{
    const char *xml = (
        "<?xml version='1.0' encoding='utf-8'?>"
        "<package>"
          "<metadata>"
            "<meta name=\"cover\" content=\"missing-id\"/>"
          "</metadata>"
          "<manifest/>"
          "<spine/>"
        "</package>"
    );

    PackageContents package;
    ASSERT_TRUE(epub_parse_package_contents("content.opf", xml, package));

    EXPECT_TRUE(package.metadata.cover_href.empty());
}
