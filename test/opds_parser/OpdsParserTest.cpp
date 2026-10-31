#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>

#include "Opds2Parser.h"
#include "OpdsParser.h"
#include "OpdsPublicationDoc.h"

bool operator==(const OpdsEntry& lhs, const OpdsEntry& rhs) {
  return lhs.author == rhs.author && lhs.href == rhs.href && lhs.id == rhs.id && lhs.title == rhs.title &&
         lhs.type == rhs.type;
}
void PrintTo(const OpdsEntry& e, std::ostream* os) {
  const std::string type = e.type == OpdsEntryType::BOOK ? "BOOK" : "NAVIGATION";
  *os << "OpdsEntry{type: " << type << ", title: " << e.title << ", author: " << e.author << ", href: " << e.href
      << ", id: " << e.id << "}";
}

namespace {

// The firmware's device preferences, as set by OpdsBookBrowserActivity.
const char* const X4_EPUB_VARIANTS[] = {".x4.epub", ".x3.epub"};
const char* const X3_EPUB_VARIANTS[] = {".x3.epub", ".x4.epub"};

/// @brief Defines both a file path and the properties an OpdsParser should have after parsing that path
struct TestFile {
  std::string filename;
  /// @brief Optimized EPUB variants to prefer, best first (none when null)
  const char* const* preferred_variants = nullptr;
  bool error = false;
  std::string search_template = "";
  std::string next_page_url = "";
  std::string prev_page_url = "";
  std::vector<OpdsEntry> entries = {};
  std::vector<OpdsEntry> books = {};
};

struct TestFile testFiles[] = {
    {.filename = "test_invalid_file.xml", .error = true},
    {.filename = "test_empty_feed.xml", .error = false},
    {
        .filename = "test_one_of_everything.xml",
        .error = false,
        .search_template = "/search/{searchTerms}.xml",
        .next_page_url = "/page-3.xml",
        .prev_page_url = "/page-1.xml",
        .entries =
            {
                {.type = OpdsEntryType::NAVIGATION, .title = "nav", .author = "", .href = "navhref", .id = "navid"},
                {.type = OpdsEntryType::BOOK, .title = "book", .author = "author", .href = "bookhref", .id = "bookid"},
            },
        .books =
            {{.type = OpdsEntryType::BOOK, .title = "book", .author = "author", .href = "bookhref", .id = "bookid"}},
    },
    {
        .filename = "test_download_priorities.xml",
        .preferred_variants = X4_EPUB_VARIANTS,
        .books =
            {
                {.type = OpdsEntryType::BOOK, .title = "onlybook", .href = "onlybook"},
                {.type = OpdsEntryType::BOOK,
                 .title = "Prefers paths containing /epub/",
                 .href = "/epub/c.somethingelse"},
                {.type = OpdsEntryType::BOOK, .title = "Prefers explicit epubs", .href = "d.epub"},
                {.type = OpdsEntryType::BOOK, .title = "Prefers compact epubs", .href = "e.x3.epub"},
                {.type = OpdsEntryType::BOOK, .title = "Ignores order to choose compact epub", .href = "e.x3.epub"},
            },
    },
    {
        .filename = "test_device_download_priorities.xml",
        .preferred_variants = X3_EPUB_VARIANTS,
        .books =
            {
                {.type = OpdsEntryType::BOOK, .title = "Device-specific preferences 1", .href = "a.x3.epub"},
                {.type = OpdsEntryType::BOOK, .title = "Device-specific preferences 2", .href = "b.x3.epub"},
            },
    },
    {
        .filename = "test_device_download_priorities.xml",
        .preferred_variants = X4_EPUB_VARIANTS,
        .books =
            {
                {.type = OpdsEntryType::BOOK, .title = "Device-specific preferences 1", .href = "b.x4.epub"},
                {.type = OpdsEntryType::BOOK, .title = "Device-specific preferences 2", .href = "a.x4.epub"},
            },
    },
};

class OpdsParserTestFixture : public testing::TestWithParam<TestFile> {
 public:
  OpdsParser* parser;

  void SetUp() override {
    const TestFile p = GetParam();

    opdsSetPreferredEpubVariants(p.preferred_variants, std::size(X4_EPUB_VARIANTS));
    parser = new OpdsParser();

    std::string filepath = std::string(TEST_DATA_DIR) + p.filename;
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
      FAIL() << "Failed to open test file " << filepath << std::endl;
      return;
    }
    // Get file size
    std::streamsize size = file.tellg();
    // go back to to the start
    file.seekg(0, std::ios::beg);
    // read the file into a buffer
    char* buffer = new char[size];
    if (!file.read(buffer, size)) {
      FAIL() << "Failed to read test file " << filepath << std::endl;
      delete[] buffer;
      return;
    }
    parser->write(reinterpret_cast<uint8_t*>(buffer), size);
    parser->flush();

    delete[] buffer;
  }

  void TearDown() override {
    delete parser;
    opdsSetPreferredEpubVariants(nullptr, 0);
  }
};

TEST_P(OpdsParserTestFixture, VerifyTestFiles) {
  const TestFile p = GetParam();
  if (p.error) {
    EXPECT_EQ(parser->error(), true);
    return;
  }
  EXPECT_EQ(parser->error(), false);
  EXPECT_EQ(parser->getSearchTemplate(), p.search_template);
  EXPECT_EQ(parser->getNextPageUrl(), p.next_page_url);
  EXPECT_EQ(parser->getPrevPageUrl(), p.prev_page_url);
  if (!p.entries.empty()) {
    // broadly checking entries is redundant with checking books,
    // so only check entries if they are explicitly set
    EXPECT_EQ(parser->getEntries(), p.entries);
  }
  EXPECT_EQ(parser->getBooks(), p.books);
}

INSTANTIATE_TEST_SUITE_P(OpdsParserTest, OpdsParserTestFixture, testing::ValuesIn(testFiles),
                         [](const testing::TestParamInfo<OpdsParserTestFixture::ParamType>& info) {
                           std::stringstream ss;
                           ss << info.index << "_"
                              << info.param.filename.substr(0, info.param.filename.find_first_of("."));
                           return ss.str();
                         });

TEST(OpdsDownloadPriority, PlainEpubWinsWithoutDevicePreference) {
  opdsSetPreferredEpubVariants(nullptr, 0);
  const std::string xml = R"(<feed xmlns="http://www.w3.org/2005/Atom"><entry><title>t</title>
    <link rel="http://opds-spec.org/acquisition" href="a.epub" type="application/epub+zip"/>
    <link rel="http://opds-spec.org/acquisition" href="a.x4.epub" type="application/epub+zip"/>
  </entry></feed>)";
  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
  parser.flush();
  ASSERT_EQ(parser.getBooks().size(), 1u);
  EXPECT_EQ(parser.getBooks()[0].href, "a.epub");
}

TEST(OpdsDownloadPriority, AcquisitionRankBeatsOptimizedFormat) {
  opdsSetPreferredEpubVariants(X4_EPUB_VARIANTS, std::size(X4_EPUB_VARIANTS));
  const std::string xml = R"(<feed xmlns="http://www.w3.org/2005/Atom"><entry><title>t</title>
    <link rel="http://opds-spec.org/acquisition/borrow" href="a.x4.epub" type="application/epub+zip"/>
    <link rel="http://opds-spec.org/acquisition/open-access" href="a.epub" type="application/epub+zip"/>
  </entry></feed>)";
  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
  parser.flush();
  opdsSetPreferredEpubVariants(nullptr, 0);
  ASSERT_EQ(parser.getBooks().size(), 1u);
  EXPECT_EQ(parser.getBooks()[0].href, "a.epub");
}

TEST(OpdsDownloadPriority, Opds2FeedPrefersDeviceOptimizedEpub) {
  opdsSetPreferredEpubVariants(X3_EPUB_VARIANTS, std::size(X3_EPUB_VARIANTS));
  const std::string json = R"({"metadata":{"title":"f"},"publications":[{"metadata":{"title":"t"},"links":[
    {"rel":"http://opds-spec.org/acquisition","href":"a.epub","type":"application/epub+zip"},
    {"rel":"http://opds-spec.org/acquisition","href":"a.x4.epub","type":"application/epub+zip"},
    {"rel":"http://opds-spec.org/acquisition","href":"a.x3.epub","type":"application/epub+zip"},
    {"rel":"http://opds-spec.org/acquisition","href":"b.epub","type":"application/epub+zip"}]}]})";
  Opds2Parser parser;
  parser.write(reinterpret_cast<const uint8_t*>(json.data()), json.size());
  parser.flush();
  opdsSetPreferredEpubVariants(nullptr, 0);
  ASSERT_FALSE(parser.error());
  ASSERT_EQ(parser.getEntries().size(), 1u);
  EXPECT_EQ(parser.getEntries()[0].href, "a.x3.epub");
}

TEST(OpdsDownloadPriority, PublicationDocPrefersDeviceOptimizedEpub) {
  opdsSetPreferredEpubVariants(X4_EPUB_VARIANTS, std::size(X4_EPUB_VARIANTS));
  const std::string json = R"({"metadata":{"title":"t"},"links":[
    {"rel":"http://opds-spec.org/acquisition","href":"a.epub","type":"application/epub+zip"},
    {"rel":"http://opds-spec.org/acquisition","href":"a.x3.epub","type":"application/epub+zip"},
    {"rel":"http://opds-spec.org/acquisition","href":"a.x4.epub","type":"application/epub+zip"}]})";
  std::string href;
  bool isEpub = false;
  ASSERT_TRUE(resolveOpdsIndirectAcquisition(json.data(), json.size(), href, isEpub));
  EXPECT_EQ(href, "a.x4.epub");
  OpdsPublication publication;
  ASSERT_TRUE(parseOpdsPublicationDoc(json.data(), json.size(), publication));
  opdsSetPreferredEpubVariants(nullptr, 0);
  EXPECT_EQ(publication.acquisitionHref, "a.x4.epub");
}

}  // namespace
