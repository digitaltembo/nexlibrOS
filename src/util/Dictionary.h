#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <string>
#include <vector>

// Result of an index search — file locations of a headword's definitions
// without reading them. A headword can own several consecutive .idx entries
// (common in dictionaries converted from other formats, which split senses or
// parts of speech into separate entries); all of them are kept, in index order.
struct DictLocation {
  static constexpr uint8_t MAX_ENTRIES = 8;
  struct Span {
    uint32_t offset = 0;  // byte offset in .dict data
    uint32_t size = 0;    // byte length in .dict data
  };
  Span entries[MAX_ENTRIES];
  uint8_t count = 0;
  bool found = false;
  // Set when the search was cut short by an .idx open or seek failure rather than
  // reaching a verdict, so a failed search isn't reported as a genuine miss.
  bool readError = false;

  // Records one more entry; false once MAX_ENTRIES are held.
  bool add(uint32_t offset, uint32_t size) {
    if (count >= MAX_ENTRIES) return false;
    entries[count++] = {offset, size};
    found = true;
    return count < MAX_ENTRIES;
  }
};

// Slim StarDict reader: exact-match lookup with a synonym and mini stemming
// fallback.
//
// Expects /dictionaries/<folder>/<stem>.idx (uncompressed) plus <stem>.dict or
// <stem>.dict.dz, and an optional <stem>.syn synonym index. Lookups
// binary-search a lazily built sampled-offset sidecar (<stem>.qidx, byte offset
// of every SAMPLE_INTERVAL-th .idx entry), then linear-scan at most
// SAMPLE_INTERVAL entries. A present .syn gets a parallel <stem>.sidx sidecar;
// its entries carry an ordinal (the N-th .idx entry) resolved back to a byte
// offset via the same fixed-interval .qidx samples. Everything streams from SD;
// no index is held in RAM.
class Dictionary {
 public:
  // Why a lookup did not return a definition — so the UI can tell a genuine
  // miss apart from a real failure, and name the failure.
  enum class LookupResult : uint8_t {
    Found,       // hit — definition filled
    NotFound,    // the word is genuinely not in the dictionary
    LowMemory,   // found, but an allocation failed: the ~32KB .dict.dz inflate
                 // window / chunk buffer, or the definition text buffer, couldn't
                 // be obtained from the fragmented heap. THIS is the "stopped
                 // finding words until restart" case — definitively memory.
    Decompress,  // found, but decompression genuinely failed (corrupt/truncated
                 // .dict.dz — a read/inflate error, not a memory shortage)
    ReadError,   // found, but a file open/bounds/IO error prevented reading it
  };

  // Resolve the dictionary folder and validate its files. Rejects
  // dictionaries with 64-bit index offsets (idxoffsetbits=64 in .ifo).
  bool open(const char* folderName);
  // As open(), for a dictionary folder given by absolute path.
  bool openFolder(const char* folderPath);
  bool isOpen() const { return !basePath.empty(); }

  // True when the .qidx sidecar (or the .sidx sidecar of a present .syn) is
  // missing or stale — call buildIndex() first so the UI can show an
  // "Indexing…" message for the slow first pass.

  // True when the .ifo declares sametypesequence=h or x (HTML or XDXF) and
  // the viewer may lay them out through the EPUB rendering pipeline.
  bool definitionsAreHtml() const { return htmlDefinitions; }

  bool needsIndex();

  // Why an index build failed — the scan buffer is a heap allocation, so the
  // same fragmentation that breaks lookups can break indexing, and it deserves
  // the same "Not enough memory" rather than a generic error.
  enum class IndexResult : uint8_t {
    Ok,
    LowMemory,  // the scan buffer couldn't be allocated
    ReadError,  // source open/read or sidecar write failure
    Cancelled,  // yieldFn asked the build to stop
  };

  // One streaming pass over .idx writing the .qidx sidecar, plus a pass over
  // .syn writing the .sidx sidecar when a synonym file is present. Each sidecar
  // is rebuilt only when actually stale. yieldFn (optional) is called every
  // ~64KB consumed to feed the watchdog / repaint the UI; returning false
  // abandons the build (IndexResult::Cancelled) and removes the partial
  // sidecar. *outResult (if provided) reports why a failed build failed; only
  // the mandatory .idx pass can fail the build (a failed .syn pass just
  // disables synonyms).
  bool buildIndex(bool (*yieldFn)(void*) = nullptr, void* ctx = nullptr, IndexResult* outResult = nullptr);

  // Clean the word, look it up, and on a miss retry dictionary-authored
  // synonyms then mini stem variants (-'s/-s/-es/-ies/-ed/-ing). On a hit fills
  // the definition text (capped at MAX_DEFINITION_BYTES) and the headword as
  // stored in the index. A headword with several entries yields all of them,
  // joined by <hr/> in HTML dictionaries and a blank line otherwise. Returns true on a hit. *outResult (if provided)
  // reports the precise outcome so the UI can distinguish a genuine miss from a
  // decompression / low-memory / read failure.
  bool lookup(const char* word, std::string& definitionOut, std::string& matchedHeadwordOut,
              LookupResult* outResult = nullptr, DictLocation* locationOut = nullptr);

  // Reload the exact entry, including synonym targets with duplicate headwords.
  // Reads and joins the definitions at location. Fails (setting *outResult, if
  // given, to Decompress / LowMemory / ReadError) only when the first entry
  // can't be read; a later entry that can't be read or doesn't fit in memory
  // ends the definition early instead.
  bool readDefinition(const DictLocation& location, std::string& out, LookupResult* outResult = nullptr);

  static std::string cleanWord(const char* word);

  // The .ifo `lang=` value for the dictionary at basePath ("<folder>/<stem>"),
  // e.g. "en" or "fr-en"; empty when the .ifo is missing or has no lang key.
  static std::string readLanguage(const std::string& basePath);
  static constexpr size_t MAX_LANG_BYTES = 24;

  static constexpr uint32_t MAX_DEFINITION_BYTES = 64 * 1024;

 private:
  static constexpr uint32_t SAMPLE_INTERVAL = 256;

  // Longest "<basePath><suffix>" the lookup path builds, rounded up. basePath is
  // "/dictionaries/<folder>/<stem>" (14 fixed chars) and the longest suffix is
  // ".dict.dz", leaving ~137 chars for folder + stem — far beyond any real
  // dictionary. open() rejects anything that would not fit, so the hot path
  // cannot fail on length. Kept under the 256-byte stack-local guideline.
  static constexpr size_t PATH_BUF_BYTES = 160;

  // Length of the longest suffix appended to basePath (".dict.dz"); used for
  // the open()-time length check.
  static constexpr size_t LONGEST_SUFFIX_LEN = sizeof(".dict.dz") - 1;

  // Compose "<basePath><suffix>" into a caller-supplied stack buffer. The
  // lookup path runs this instead of `basePath + suffix` so path construction
  // costs no transient heap — see LookupSession. (A lookup still allocates
  // elsewhere: cleanWord(), stemVariants() and the matched headword.) False
  // (and logs) when the path would not fit, which open() has already ruled out.
  bool buildPath(char* buf, size_t bufSize, const char* suffix) const;

  // Validate the files at resolved ("<folder>/<stem>") and adopt it as basePath.
  bool openBasePath(std::string resolved);

  // The .idx / .qidx handles shared by every locate() call in one lookup. A
  // lookup probes up to ~5 stem variants; opening the two files per probe cost
  // ~10 SD opens and ~10 std::string path temporaries per word, churning the
  // same heap whose fragmentation makes lookups fail mid-session. Opened once
  // per lookup instead, with the paths built via buildPath(). The .syn / .sidx
  // handles are opened lazily by locateSynonym() — only an exact miss consults
  // them, so a hit never pays for two extra SD opens.
  struct LookupSession {
    HalFile idx;
    HalFile qidx;
    HalFile syn;
    HalFile sidx;
    uint32_t idxSize = 0;
    uint32_t sampleCount = 0;  // 0 when the sidecar is absent, stale or empty
    uint32_t entryCount = 0;   // .idx entries the sidecar was built over; 0 = unknown
    uint32_t synSize = 0;
    uint32_t synSampleCount = 0;
    bool synOpened = false;  // openSynonyms() has run (success or failure)
    // A .syn exists but couldn't be searched (open failure, or no usable .sidx),
    // so a miss is an unfinished search rather than a verdict. Distinct from
    // "this dictionary has no .syn", which is a legitimate miss.
    bool synFailed = false;
  };

  // Open .idx (required) and .qidx (optional — locate() falls back to a full
  // scan without it). False when the dictionary is closed or .idx won't open.
  bool openSession(LookupSession& session);

  // Open .syn / .sidx into the session on first use. Idempotent; returns false
  // when there is no usable synonym index — either because no .syn exists, or
  // because it couldn't be opened / its .sidx is unusable, which sets
  // session.synFailed and releases both handles (an unindexed .syn is never
  // scanned linearly).
  bool openSynonyms(LookupSession& session);

  // Bisect a sampled-offset sidecar (.qidx over .idx, .sidx over .syn) to the
  // byte offset of the last sampled entry whose word is < target, so the caller
  // only has to linear-scan about SAMPLE_INTERVAL entries from there and starts
  // ahead of any run of entries equal to target. Returns
  // 0 — scan source from the start — when sampleCount is 0 or a sample is
  // unreadable. Clobbers wordBuf.
  uint32_t bisectSamples(HalFile& sidecar, HalFile& source, uint32_t sampleCount, const char* target);

  DictLocation locate(LookupSession& session, const char* target, std::string* matchedHeadwordOut);

  // Resolve an ordinal (the N-th .idx entry, 0-based) to its .dict location via
  // the .qidx samples. Used to follow a .syn synonym back to its headword.
  DictLocation locateByOrdinal(LookupSession& session, uint32_t ordinal, std::string* matchedHeadwordOut);

  // Bisect the .syn/.sidx synonym index for target; on a hit follow its ordinal
  // through locateByOrdinal(). Returns not-found when no .syn exists.
  DictLocation locateSynonym(LookupSession& session, const char* target, std::string* matchedHeadwordOut);

  // One streaming pass over sourcePath writing a sampled-offset sidecar. Each
  // source entry is a NUL-terminated word followed by suffixBytes fixed bytes
  // (8 for .idx: offset+size; 4 for .syn: ordinal). magic tags the sidecar.
  // *outResult (if provided) reports why a failed pass failed.
  static bool buildSidecar(const std::string& sourcePath, const std::string& sidecarPath, uint32_t magic,
                           uint32_t suffixBytes, bool (*yieldFn)(void*), void* ctx, IndexResult* outResult);

  // True when sidecarPath must be (re)built from sourcePath: missing/unreadable/
  // wrong-version sidecar, or a source-size mismatch. Shared by needsIndex() and
  // buildIndex() so each sidecar is rebuilt only when actually stale.
  static bool sidecarIsStale(const std::string& sourcePath, const std::string& sidecarPath, uint32_t magic);

  // Append prefix plus size bytes of the entry at offset to out. out grows only
  // after any .dict.dz extraction has released its inflate buffers; on failure
  // it is left unchanged.
  bool appendSpan(uint32_t offset, uint32_t size, const char* prefix, std::string& out, LookupResult* outResult);
  static void stemVariants(const std::string& word, std::vector<std::string>& out);

  // Read a null-terminated word from an open file into buf (max bufSize-1
  // chars). Returns the number of characters read (excluding null), or -1 on
  // EOF/error. Over-long words are truncated but the stream stays in sync.
  static int readWordInto(HalFile& file, char* buf, size_t bufSize);

  std::string basePath;  // "/dictionaries/<folder>/<stem>", empty when not open
  bool hasPlainDict = false;
  bool hasSyn = false;  // a <stem>.syn synonym index exists next to the .idx
  bool htmlDefinitions = false;

  // Shared scan buffer: lookups are single-threaded and this avoids a
  // 256-byte array on the stack of every locate() call.
  char wordBuf[256] = {};
};
