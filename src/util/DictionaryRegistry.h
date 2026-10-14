#pragma once

#include <string>
#include <vector>

// One StarDict dictionary found under /dictionaries or /.dictionaries: a
// subfolder holding <stem>.idx plus <stem>.dict or <stem>.dict.dz.
struct DictionaryEntry {
  std::string name;  // subfolder name (shown to the user, stored in settings)
  std::string stem;  // index basename without .idx
  // .ifo `lang=` tags, e.g. "en", "fr-en" or "de,nl"; empty serves any language.
  std::string lang;
};

namespace DictionaryRegistry {

// Scan /dictionaries/*/ and /.dictionaries/*/ for dictionaries. Folders with
// multiple index stems are ambiguous and skipped. Result is sorted
// case-insensitively by name.
void discover(std::vector<DictionaryEntry>& out);

// Resolve a folder name to its extensionless base path
// ("/dictionaries/<folder>/<stem>" or "/.dictionaries/<folder>/<stem>").
// Returns false if the folder holds no usable dictionary in either root.
bool resolveBasePath(const char* folderName, std::string& basePathOut);

// Resolve an absolute dictionary folder path, in any location, to its
// extensionless base path ("<folderPath>/<stem>"). Returns false if the folder
// holds no usable dictionary.
bool resolveFolderPath(const char* folderPath, std::string& basePathOut);

// True when the dictionary may be used for a book in bookLanguage (BCP 47,
// from the EPUB metadata): one of its comma-separated lang tags has the same
// primary subtag, case-insensitively ("en-GB" and "en-fr" both serve "en-US").
// An untagged dictionary or an untagged book always matches.
bool servesLanguage(const DictionaryEntry& entry, const std::string& bookLanguage);

// Order dictionaries for a lookup in a book: those serving bookLanguage
// first, then the rest. activeName (the configured default) leads its group;
// otherwise the sorted order is kept.
void orderForBook(std::vector<DictionaryEntry>& dictionaries, const std::string& bookLanguage, const char* activeName);

}  // namespace DictionaryRegistry
