#include "PackageParser.h"

#include "WallpaperEngine/Data/Utils/BinaryReader.h"
#include "WallpaperEngine/Logging/Log.h"

#include "WallpaperEngine/Data/Assets/Package.h"

#include <fstream>
#include <memory>

using namespace WallpaperEngine::Data::Parsers;
using namespace WallpaperEngine::Data::Assets;
using namespace WallpaperEngine::Data::Utils;

// Highest pkg format version whose binary layout has been verified compatible. Newer versions are
// still attempted (the index structure has been stable across all known versions) but get a warning.
static constexpr int PKG_MAX_KNOWN_VERSION = 24;

PackageUniquePtr PackageParser::parse (ReadStreamSharedPtr stream) {
    auto reader = std::make_unique<BinaryReader> (std::move (stream));

    const std::string header = reader->nextSizedString ();

    if (header.starts_with ("PKGV") == false) {
	sLog.exception ("Expected header to start with PKGV, got ", header);
    }

    int version = 0;

    try {
	version = std::stoi (header.substr (4));
    } catch (...) {
	sLog.exception ("Cannot parse version number from pkg header: ", header);
    }

    if (version > PKG_MAX_KNOWN_VERSION) {
	sLog.error ("scene.pkg version ", header, " has not been verified, some assets may fail to load");
    }

    // work out the total file size up-front so the index layout can be self-validated below
    auto& base = reader->base ();
    const auto headerEnd = base.tellg ();
    base.seekg (0, std::ios::end);
    const auto fileSize = static_cast<uint32_t> (base.tellg ());
    base.seekg (headerEnd, std::ios::beg);

    auto result = std::make_unique<Package> (Package {
	.file = std::move (reader),
    });

    result->files = parseFileList (*result->file, fileSize);
    result->baseOffset = 0;

    return result;
}

FileEntryList PackageParser::parseFileList (const BinaryReader& stream, uint32_t fileSize) {
    const uint32_t filesCount = stream.nextUInt32 ();
    const auto indexStart = stream.base ().tellg ();

    // Unpadded layout (every known version except the PKGV0021 padded variant):
    //   [uint32 nameLen][name][uint32 offset][uint32 length]
    // offset is relative to the start of the data section, i.e. right after the index.
    FileEntryList entries = {};
    entries.reserve (filesCount);

    for (uint32_t i = 0; i < filesCount; i++) {
	entries.push_back (
	    std::make_unique<FileEntry> (FileEntry {
		.filename = stream.nextSizedString (), .offset = stream.nextUInt32 (), .length = stream.nextUInt32 () })
	);
    }

    const auto dataStart = static_cast<uint32_t> (stream.base ().tellg ());
    const bool unpaddedValid = entries.empty ()
	? dataStart == fileSize
	: static_cast<uint64_t> (dataStart) + entries.back ()->offset + entries.back ()->length == fileSize;

    if (unpaddedValid) {
	for (const auto& entry : entries) {
	    entry->offset += dataStart;
	}

	return entries;
    }

    // Fall back to the PKGV0021 padded variant: the name is padded to a 4-byte boundary before the
    // offset/length pair, and the offset is already absolute from the start of the file.
    stream.base ().seekg (indexStart, std::ios::beg);
    entries.clear ();

    for (uint32_t i = 0; i < filesCount; i++) {
	std::string filename = stream.nextSizedString ();
	stream.base ().seekg (static_cast<int64_t> ((4 - filename.size () % 4) % 4), std::ios::cur);

	const uint32_t offset = stream.nextUInt32 ();
	const uint32_t length = stream.nextUInt32 ();

	if (static_cast<uint64_t> (offset) + length > fileSize) {
	    sLog.exception ("PKG index parsing failed for both known layouts");
	}

	entries.push_back (
	    std::make_unique<FileEntry> (FileEntry {
		.filename = std::move (filename), .offset = offset, .length = length })
	);
    }

    return entries;
}
