#include "BookTextSearch.h"

#include <Utf8.h>

#include <algorithm>

namespace {
constexpr uint32_t IGNORED = 0xFFFFFFFF;  // folds to nothing (soft hyphen, zero-width marks)
constexpr char ELLIPSIS[] = "\xE2\x80\xA6";

bool isSpace(const uint32_t cp) {
  return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\f' || cp == 0xA0 ||
         (cp >= 0x2000 && cp <= 0x200A) || cp == 0x202F || cp == 0x3000;
}

// Search folding: one space for any whitespace, lowercase ASCII and Latin-1,
// straight quotes for curly ones, invisible marks dropped.
uint32_t fold(const uint32_t cp) {
  if (isSpace(cp)) return ' ';
  if (cp == 0xAD || cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0xFEFF) return IGNORED;
  if (cp >= 'A' && cp <= 'Z') return cp + 32;
  if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 32;
  if (cp == 0x2018 || cp == 0x2019 || cp == 0x201B || cp == 0x02BC) return '\'';
  if (cp == 0x201C || cp == 0x201D || cp == 0x201E) return '"';
  return cp;
}
}  // namespace

bool BookTextSearch::begin(const Epub& epub, const std::string& term) {
  needleLength = 0;
  bool lastSpace = true;
  const auto* p = reinterpret_cast<const unsigned char*>(term.c_str());
  while (*p && needleLength < MAX_TERM_CODEPOINTS) {
    const uint32_t folded = fold(utf8NextCodepoint(&p));
    if (folded == IGNORED || (folded == ' ' && lastSpace)) continue;
    lastSpace = folded == ' ';
    needle[needleLength++] = folded;
  }
  if (needleLength > 0 && needle[needleLength - 1] == ' ') needleLength--;
  if (needleLength == 0 || !index.open(epub)) return false;

  results.clear();
  results.reserve(MAX_MATCHES);
  pending.clear();
  pending.reserve(4);
  capped = false;
  spine = 0;
  filePos = index.textStart();
  spineCodepoints = 0;
  spineFirstMatch = 0;
  utf8Remaining = 0;
  recentCount = 0;
  contextCount = 0;
  lastWasSpace = true;
  return true;
}

int BookTextSearch::progressPercent() const {
  const uint32_t total = index.textEnd() - index.textStart();
  if (total == 0) return 100;
  return static_cast<int>((static_cast<uint64_t>(filePos - index.textStart()) * 100) / total);
}

bool BookTextSearch::step(size_t budget) {
  while (budget > 0) {
    if (spine >= index.spineCount()) return false;
    if (capped) {
      endSpine();
      spine = index.spineCount();
      return false;
    }
    const uint32_t end = index.spineEnd(spine);
    if (filePos >= end) {
      endSpine();
      continue;
    }
    const size_t want = std::min({budget, sizeof(buffer), static_cast<size_t>(end - filePos)});
    HalFile& file = index.file();
    if (file.position() != filePos) file.seek(filePos);
    const int got = file.read(buffer, want);
    if (got <= 0) {
      // Unreadable: stop here and keep what was found.
      endSpine();
      spine = index.spineCount();
      return false;
    }
    for (int i = 0; i < got; ++i) {
      const uint8_t b = buffer[i];
      if (utf8Remaining > 0 && (b & 0xC0) == 0x80) {
        utf8Codepoint = (utf8Codepoint << 6) | (b & 0x3F);
        if (--utf8Remaining == 0) feedCodepoint(utf8Codepoint);
        continue;
      }
      utf8Remaining = 0;
      if (b < 0x80) {
        feedCodepoint(b);
      } else if ((b & 0xE0) == 0xC0) {
        utf8Codepoint = b & 0x1F;
        utf8Remaining = 1;
      } else if ((b & 0xF0) == 0xE0) {
        utf8Codepoint = b & 0x0F;
        utf8Remaining = 2;
      } else if ((b & 0xF8) == 0xF0) {
        utf8Codepoint = b & 0x07;
        utf8Remaining = 3;
      }
    }
    filePos += static_cast<uint32_t>(got);
    budget -= static_cast<size_t>(got);
  }
  return true;
}

void BookTextSearch::feedCodepoint(const uint32_t cp) {
  // Every decoded codepoint counts toward the offset, as in the layout parser.
  const uint32_t offset = spineCodepoints++;
  const uint32_t folded = fold(cp);
  if (folded == IGNORED) return;
  const bool space = folded == ' ';
  if (space && lastWasSpace) return;
  lastWasSpace = space;

  const uint32_t display = space ? ' ' : cp;
  context[contextHead] = display;
  contextHead = static_cast<uint8_t>((contextHead + 1) % CONTEXT_RING);
  if (contextCount < CONTEXT_RING) contextCount++;
  appendToPending(display);

  recentFolded[recentHead] = folded;
  recentOffsets[recentHead] = offset;
  recentHead = static_cast<uint8_t>((recentHead + 1) % MAX_TERM_CODEPOINTS);
  if (recentCount < MAX_TERM_CODEPOINTS) recentCount++;
  if (recentCount < needleLength || capped) return;
  const int first = (recentHead - needleLength + MAX_TERM_CODEPOINTS) % MAX_TERM_CODEPOINTS;
  for (int k = 0; k < needleLength; ++k) {
    if (recentFolded[(first + k) % MAX_TERM_CODEPOINTS] != needle[k]) return;
  }

  Match match;
  match.spine = static_cast<uint16_t>(spine);
  match.offset = recentOffsets[first];
  // Context ring holds the match itself plus up to CONTEXT_BEFORE before it.
  const size_t take = std::min<size_t>(contextCount, CONTEXT_BEFORE + needleLength);
  const bool cutStart = contextCount > take || (contextCount == CONTEXT_RING);
  std::string snippet;
  snippet.reserve(CONTEXT_BEFORE + needleLength + CONTEXT_AFTER + 8);
  size_t skip = 0;
  if (cutStart) {
    // Start at a word boundary within the leading context.
    const size_t beforeLength = take - needleLength;
    for (size_t i = 0; i < beforeLength; ++i) {
      if (context[(contextHead + CONTEXT_RING - take + i) % CONTEXT_RING] == ' ') {
        skip = i + 1;
        break;
      }
    }
    snippet = ELLIPSIS;
  }
  for (size_t i = skip; i < take; ++i) {
    utf8AppendCodepoint(context[(contextHead + CONTEXT_RING - take + i) % CONTEXT_RING], snippet);
  }
  match.snippet = std::move(snippet);
  results.push_back(std::move(match));
  pending.push_back({results.size() - 1, static_cast<uint8_t>(CONTEXT_AFTER)});
  recentCount = 0;  // matches do not overlap
  if (results.size() >= MAX_MATCHES) capped = true;
}

void BookTextSearch::appendToPending(const uint32_t displayCp) {
  for (size_t i = 0; i < pending.size();) {
    Match& match = results[pending[i].result];
    utf8AppendCodepoint(displayCp, match.snippet);
    if (--pending[i].remaining == 0) {
      finishSnippet(match);
      pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(i));
    } else {
      ++i;
    }
  }
}

// The after-context ran out: end on a word boundary with an ellipsis.
void BookTextSearch::finishSnippet(Match& match) const {
  const size_t lastSpace = match.snippet.find_last_of(' ');
  if (lastSpace != std::string::npos && lastSpace > match.snippet.size() / 2) match.snippet.resize(lastSpace);
  match.snippet += ELLIPSIS;
}

void BookTextSearch::endSpine() {
  // Matches near the end of a chapter keep the context they have.
  pending.clear();
  const float length = static_cast<float>(std::max<uint32_t>(1, spineCodepoints));
  for (size_t i = spineFirstMatch; i < results.size(); ++i) {
    results[i].chapterFraction = static_cast<float>(results[i].offset) / length;
  }
  spine++;
  if (spine < index.spineCount()) filePos = index.spineStart(spine);
  spineCodepoints = 0;
  spineFirstMatch = results.size();
  utf8Remaining = 0;
  recentCount = 0;
  contextCount = 0;
  lastWasSpace = true;
}
