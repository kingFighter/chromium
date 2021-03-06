// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media_galleries/fileapi/itunes_library_parser.h"

#include <string>

#include "base/logging.h"
#include "base/strings/string16.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/common/itunes_xml_utils.h"
#include "googleurl/src/gurl.h"
#include "googleurl/src/url_canon.h"
#include "googleurl/src/url_util.h"
#include "third_party/libxml/chromium/libxml_utils.h"

namespace itunes {

namespace {

struct TrackInfo {
  uint64 id;
  base::FilePath location;
  std::string artist;
  std::string album;
};

// Seek to the start of a tag and read the value into |result| if the node's
// name is |node_name|.
bool ReadSimpleValue(XmlReader* reader, const std::string& node_name,
                     std::string* result) {
  if (!SkipToNextElement(reader))
      return false;
  if (reader->NodeName() != node_name)
    return false;
  return reader->ReadElementContent(result);
}

// Get the value out of a string node.
bool ReadString(XmlReader* reader, std::string* result) {
  return ReadSimpleValue(reader, "string", result);
}

// Get the value out of an integer node.
bool ReadInteger(XmlReader* reader, uint64* result) {
  std::string value;
  if (!ReadSimpleValue(reader, "integer", &value))
    return false;
  return base::StringToUint64(value, result);
}

// Walk through a dictionary filling in |result| with track information. Return
// true if it was all found, false otherwise.  In either case, the cursor is
// advanced out of the dictionary.
bool GetTrackInfoFromDict(XmlReader* reader, TrackInfo* result) {
  DCHECK(result);
  if (reader->NodeName() != "dict")
    return false;

  int dict_content_depth = reader->Depth() + 1;
  // Advance past the dict node and into the body of the dictionary.
  if (!reader->Read())
    return false;

  bool found_id = false;
  bool found_location = false;
  bool found_artist = false;
  bool found_album = false;
  while (reader->Depth() >= dict_content_depth &&
         !(found_id && found_location && found_artist && found_album)) {
    if (!SeekToNodeAtCurrentDepth(reader, "key"))
      break;
    std::string found_key;
    if (!reader->ReadElementContent(&found_key))
      break;
    DCHECK_EQ(dict_content_depth, reader->Depth());

    if (found_key == "Track ID") {
      if (found_id)
        break;
      if (!ReadInteger(reader, &result->id))
        break;
      found_id = true;
    } else if (found_key == "Location") {
      if (found_location)
        break;
      std::string value;
      if (!ReadString(reader, &value))
        break;
      GURL url(value);
      if (!url.SchemeIsFile())
        break;
      url_canon::RawCanonOutputW<1024> decoded_location;
      url_util::DecodeURLEscapeSequences(url.path().c_str() + 1,  // Strip /.
                                         url.path().length() - 1,
                                         &decoded_location);
#if defined(OS_WIN)
      string16 location(decoded_location.data(), decoded_location.length());
#else
      string16 location16(decoded_location.data(), decoded_location.length());
      std::string location = UTF16ToUTF8(location16);
#endif
      result->location = base::FilePath(location);
      found_location = true;
    } else if (found_key == "Album Artist") {
      if (found_artist)
        break;
      if (!ReadString(reader, &result->artist))
        break;
      found_artist = true;
    } else if (found_key == "Album") {
      if (found_album)
        break;
      if (!ReadString(reader, &result->album))
        break;
      found_album = true;
    } else {
      if (!SkipToNextElement(reader))
        break;
      if (!reader->Next())
        break;
    }
  }

  // Seek to the end of the dictionary
  while (reader->Depth() >= dict_content_depth)
    reader->Next();

  return found_id && found_location && found_artist && found_album;
}

}  // namespace

ITunesLibraryParser::Track::Track(uint64 id, const base::FilePath& location)
    : id(id),
      location(location) {
}

bool ITunesLibraryParser::Track::operator<(const Track& other) const {
  return id < other.id;
}

ITunesLibraryParser::ITunesLibraryParser() {}
ITunesLibraryParser::~ITunesLibraryParser() {}

bool ITunesLibraryParser::Parse(const std::string& library_xml) {
  XmlReader reader;

  if (!reader.Load(library_xml))
    return false;

  // Find the plist node and then search within that tag.
  if (!SeekToNodeAtCurrentDepth(&reader, "plist"))
    return false;
  if (!reader.Read())
    return false;

  if (!SeekToNodeAtCurrentDepth(&reader, "dict"))
    return false;

  if (!SeekInDict(&reader, "Tracks"))
    return false;

  // Once inside the Tracks dict, we expect track dictionaries keyed by id. i.e.
  //   <key>Tracks</key>
  //   <dict>
  //     <key>160</key>
  //     <dict>
  //       <key>Track ID</key><integer>160</integer>
  if (!SeekToNodeAtCurrentDepth(&reader, "dict"))
    return false;
  int tracks_dict_depth = reader.Depth() + 1;
  if (!reader.Read())
    return false;

  // Once parsing has gotten this far, return whatever is found, even if
  // some of the data isn't extracted just right.
  bool no_errors = true;
  bool track_found = false;
  while (reader.Depth() >= tracks_dict_depth) {
    if (!SeekToNodeAtCurrentDepth(&reader, "key"))
      return track_found;
    std::string key;  // Should match track id below.
    if (!reader.ReadElementContent(&key))
      return track_found;
    uint64 id;
    bool id_valid = base::StringToUint64(key, &id);
    if (!reader.SkipToElement())
      return track_found;

    TrackInfo track_info;
    if (GetTrackInfoFromDict(&reader, &track_info) &&
        id_valid &&
        id == track_info.id) {
      Track track(track_info.id, track_info.location);
      library_[track_info.artist][track_info.album].insert(track);
      track_found = true;
    } else {
      no_errors = false;
    }
  }

  return track_found || no_errors;
}

}  // namespace itunes
