#include "DictionaryLookup.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

bool DictionaryLookup::indexBuildYield(void* self) {
  vTaskDelay(1);
  const auto* cancel = static_cast<const DictionaryLookup*>(self)->cancelFlag;
  return !cancel || !cancel->load();
}

bool DictionaryLookup::prepare(const char* folderName) {
  if (!opened || openName != folderName) {
    opened = true;
    openName = folderName;
    openOk = dict.open(folderName);
    // needsIndex() opens and validates the sidecars, so ask once per open: the
    // answer only changes when lookup() builds them.
    needsIndex = openOk && dict.needsIndex();
  }
  return needsIndex;
}

bool DictionaryLookup::lookup(const char* word, std::string& definitionOut, std::string& headwordOut, StrId& errorOut) {
  if (!openOk) {
    errorOut = StrId::STR_DICT_ERROR;
    return false;
  }
  if (needsIndex) {
    Dictionary::IndexResult indexResult = Dictionary::IndexResult::Ok;
    if (!dict.buildIndex(&indexBuildYield, this, &indexResult)) {
      // An index build allocates a scan buffer, so it fails the same way
      // lookups do on a fragmented heap — name that rather than a generic error.
      errorOut = indexResult == Dictionary::IndexResult::LowMemory   ? StrId::STR_DICT_LOW_MEMORY
                 : indexResult == Dictionary::IndexResult::ReadError ? StrId::STR_DICT_READ_FAILED
                                                                     : StrId::STR_DICT_ERROR;
      return false;  // needsIndex stays set: the next lookup retries the build
    }
    needsIndex = false;
  }

  Dictionary::LookupResult result = Dictionary::LookupResult::NotFound;
  if (dict.lookup(word, definitionOut, headwordOut, &result)) return true;
  switch (result) {
    case Dictionary::LookupResult::Decompress:
      errorOut = StrId::STR_DICT_DECOMPRESS_ERROR;
      break;
    case Dictionary::LookupResult::LowMemory:
      errorOut = StrId::STR_DICT_LOW_MEMORY;
      break;
    case Dictionary::LookupResult::ReadError:
      errorOut = StrId::STR_DICT_READ_FAILED;
      break;
    case Dictionary::LookupResult::NotFound:
    case Dictionary::LookupResult::Found:
    default:
      errorOut = StrId::STR_DICT_NOT_FOUND;
      break;
  }
  return false;
}
