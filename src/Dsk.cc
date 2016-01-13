#include "Dsk.hpp"

Dsk::Dsk(u8 numSides, const struct XDPB& params, CatalogType catType) {
	this->_numSides = numSides;
	memcpy(&this->_specs, &params, sizeof(struct XDPB));

	this->_blockSize = DSK_RECORD_SIZE * (1 << (this->_specs.blockShift));

	u16 sectorSize = this->_specs.sectSizeInRecords * DSK_RECORD_SIZE;
	u16 sectorsPerBlock = (this->_blockSize / sectorSize);

	this->_numTracks = (((this->_specs.numBlocks + 1) * sectorsPerBlock) / this->_specs.sectorsPerTrack) + this->_specs.reservedTracks;

	this->_catalogType = catType;

	switch (this->_catalogType) {
	case CAT_NONE:
		this->_catalogSizeInSectors = 0;
		break;
	case CAT_RAW:
		this->_catalogSizeInSectors = 1;
		break;
	case CAT_CPM:
	case CAT_SF2:
		u32 size;
		size = this->_numSides * (params.dirEntries + 1) * (sizeof(struct CatalogEntryAmsdos) - 1);
		size = size / sectorSize;
		this->_catalogSizeInSectors = (u8)size;
		break;
	default:
		throw "ERROR: Catalog type is unknown!";
	}
	this->initCatalog();
	this->initDskHeader();

	this->setTrack(0, this->_specs.reservedTracks);
	this->advanceSectors(this->_catalogSizeInSectors);
}

void Dsk::advanceSectors(u8 sectors) {
	this->_currentSector += sectors;
	while (this->_currentSector >= this->_specs.firstSectorNumber + this->_specs.sectorsPerTrack) {
		this->_currentSector -= this->_specs.sectorsPerTrack;
		this->_currentTrack++;
		if (this->_currentTrack >= (this->_numSides * this->_numTracks)) {
			this->_currentTrack -= this->_numTracks;
			this->_currentSide++;
			if (this->_currentSide >= this->_numSides) {
				throw "ERROR: Disk full!";
			}
		}
	}
	this->updateCurrentBlock();
}

void Dsk::updateCurrentBlock(void) {
	this->_currentBlock = (
		(this->_currentTrack * this->_specs.sectorsPerTrack + (this->_currentSector - this->_specs.firstSectorNumber)) *
		this->_specs.sectSizeInRecords * DSK_RECORD_SIZE)
		/ this->_blockSize;
}

void Dsk::setTrack(u8 side, u8 track) {
	this->_currentTrack = track;
	this->_currentSide = side;
	this->_currentSector = this->_specs.firstSectorNumber;
	this->updateCurrentBlock();
}

void Dsk::initDskHeader(void) {
	memset(&this->_dskHeader, 0, sizeof(struct DskHeader));
	for (u16 t = 0; t < 256; ++t) {
		memset(&this->_dskTracks[t], 0, sizeof(struct DskTrack));
	}

	memcpy(this->_dskHeader.Id, "EXTENDED CPC DSK File\r\nDisk-Info\r\n", 34);
	memcpy(this->_dskHeader.Creator, "dskgen-C", 8);

	this->_dskHeader.Sides = this->_numSides;
	this->_dskHeader.Tracks = this->_numTracks;

	u16 trackSize = (((u16)this->_specs.sectorsPerTrack * this->_specs.sectSizeInRecords) / 2) + 1;

	this->_dskHeader.Unused = trackSize << 8;
	for (u8 ts = 0, lts = this->_numTracks * this->_numSides; ts < lts; ++ts) {
		this->_dskHeader.TrackSizes[ts] = trackSize;
		struct DskTrack &track = this->_dskTracks[ts];
		memcpy(track.Id, "Track-Info\r\n", 12);
		track.Track = ts / this->_numSides;
		track.Side = ts % this->_numSides;
		track.SectSize = this->_specs.sectSizeInRecords / 2;
		track.SectCount = this->_specs.sectorsPerTrack;
		track.Gap3 = this->_specs.gapF; // 0x4E
		track.FillerByte = AMSDOS_EMPTY_BYTE;
		for (u8 s = 0; s < this->_specs.sectorsPerTrack; ++s) {
			DskSector *sect = &track.Sectors[s];
			sect->Track = track.Track;
			sect->Side = track.Side;
			sect->Sector = this->_specs.firstSectorNumber + s;
			sect->Size = track.SectSize;
			sect->SizeInBytes = this->_specs.sectSizeInRecords * DSK_RECORD_SIZE;
			sect->Data = new u8[sect->SizeInBytes];
			memset(sect->Data, AMSDOS_EMPTY_BYTE, sect->SizeInBytes);
		}
	}
}

void Dsk::initCatalog(void) {
	memset(this->_catalog, (u8)AMSDOS_EMPTY_BYTE, sizeof(struct CatalogEntryAmsdos) * 512);
	this->_catalogPtr = this->_catalog;

	if (this->_catalogType == CAT_RAW || this->_catalogType == CAT_SF2) {
		memcpy(((u8*)this->_catalogPtr) + 1, "RAW CATSF2CPC", 13);
		this->_catalogPtr[14] = 0;
		this->_catalogPtr[15] = 0;
		this->_catalogPtr += 2 * sizeof(struct CatalogEntryRaw);
	}
}

int Dsk::AddFile(FileToProcess &file) {
	ifstream f(file.SourcePath, ifstream::binary);
	if (f.good()) {
		f.seekg(0, f.end);
		file.Length = f.tellg();
		f.seekg(0, f.beg);
		u8* fileData = new u8[file.Length];
		f.read((char*)fileData, (streamsize)file.Length);
		f.close();

		if (file.Header == HDR_AMSDOS) {
			if (!checkAmsdosHeader(fileData)) {
				u8 headerSize = sizeof(struct AmsdosHeader);
				u32 newLength = file.Length + headerSize;
				u8* newData = new u8[newLength];

				memcpy(newData + headerSize, fileData, file.Length);
				u8* oldData = fileData;
				delete oldData;

				fillAmsdosHeader((struct AmsdosHeader*)newData, file);
				file.Length += headerSize;
				fileData = newData;
			}
		}

		_filesInDsk.push_back(file);
		addToCatalog(file);

		// add data to sectors!
		this->writeDataToSectors(fileData, file.Length);
	}
	return 0;
}

void Dsk::writeDataToSectors(u8* data, u32 length) {
	u32 remainingBytes = length;
	u8* ptrData = data;
	u32 sectorSizeInBytes = this->_specs.sectSizeInRecords * DSK_RECORD_SIZE;
	while (remainingBytes > 0) {
		u32 bytesToWrite = min(remainingBytes, sectorSizeInBytes);
		DskTrack *track = &this->_dskTracks[(this->_numSides * this->_currentTrack) + this->_currentSide];
		DskSector* sector = &(track->Sectors[this->_currentSector - this->_specs.firstSectorNumber]);
		memcpy(sector->Data, ptrData, bytesToWrite);
		ptrData += bytesToWrite;
		remainingBytes -= bytesToWrite;
		this->advanceSectors(1);
	}
}

int Dsk::AddBootFile(const string& path) {
	ifstream f(path, ifstream::binary);
	if (f.good()) {
		f.seekg(0, f.end);
		u32 length = f.tellg();
		u32 maxSize = DSK_RECORD_SIZE * this->_specs.sectorsPerTrack * this->_specs.sectSizeInRecords * this->_specs.reservedTracks;
		if (length >= maxSize) {
			return -1;
		}
		f.seekg(0, f.beg);
		u8* fileData = new u8[length];
		f.read((char*)fileData, (streamsize)length);
		f.close();

		this->setTrack(0, 0);
		this->writeDataToSectors(fileData, length);

		this->setTrack(0, this->_specs.reservedTracks);
		this->advanceSectors(this->_catalogSizeInSectors);
	}
	else {
		return -2;
	}
	return 0;
}

void Dsk::addToCatalog(FileToProcess &file) {
	if (this->_catalogType == CAT_RAW || (this->_catalogType == CAT_SF2 && file.AmsdosType == AMSDOS_FILE_RAW_CAT)) {
		struct CatalogEntryRaw* rawEntry = (struct CatalogEntryRaw*) this->_catalogPtr;
		rawEntry->EmptyByte = AMSDOS_EMPTY_BYTE;
		rawEntry->Side = this->_currentSide;
		rawEntry->InitialTrack = this->_currentTrack;
		rawEntry->InitialSectorOffset = this->_currentSector;
		rawEntry->LengthInBytes = file.Length;
		this->_catalogPtr += sizeof(struct CatalogEntryRaw);

		rawEntry = (struct CatalogEntryRaw*) this->_catalog;
		u16 entries = rawEntry[1].Padding[0] + (256 * rawEntry[1].Padding[1]) + 1;
		rawEntry[1].Padding[0] = entries % 256;
		rawEntry[1].Padding[1] = entries / 256;
	}
	else if (this->_catalogType == CAT_CPM || (this->_catalogType == CAT_SF2 && file.AmsdosType != AMSDOS_FILE_RAW_CAT)) {
		// We will need one catalog entry per 16 blocks.
		int fileLengthInBlocks = ceil((float)file.Length / this->_blockSize);
		int blocksCovered = 0;
		u8 extentIdx = 0;
		while (blocksCovered < fileLengthInBlocks) {
			u8 blocksInExtent = min(fileLengthInBlocks - blocksCovered, 16);
			struct CatalogEntryAmsdos* amsEntry = (struct CatalogEntryAmsdos*) this->_catalogPtr;
			amsEntry->Side = this->_currentSide;
			amsEntry->UserNumber = 0;
			memcpy(amsEntry->Name, file.AmsDosName, 11);
			amsEntry->ExtentLoByte = extentIdx % 32;
			amsEntry->ExtentPadding = 0;
			amsEntry->ExtentHiByte = extentIdx / 32;
			extentIdx++;
			amsEntry->Records = (u8)min(ceil(((float)file.Length - (blocksCovered * this->_blockSize)) / DSK_RECORD_SIZE), (float)DSK_RECORD_SIZE);
			memset(amsEntry->Blocks, 0, 16);
			for (u8 b = 0; b < blocksInExtent; b++) {
				amsEntry->Blocks[b] = this->_currentBlock++;
			}
			blocksCovered += blocksInExtent;
			this->_catalogPtr += sizeof(struct CatalogEntryAmsdos*);
		}
	}
}

bool Dsk::checkAmsdosHeader(u8* buffer) {
	u16 checksum = 0;
	u16 checkSumInHeader = buffer[67] + (buffer[68] << 8);
	u8* bufPtr = buffer;
	for (u8 i = 0; i < 67; ++i) {
		checksum += (*bufPtr);
		++bufPtr;
	}
	return checkSumInHeader == checksum;
}

void Dsk::fillAmsdosHeader(struct AmsdosHeader* header, const FileToProcess& file) {
	memset(header, 0, sizeof(struct AmsdosHeader));
	memcpy(header->FileName, file.AmsDosName, 11);

	header->Length = 0;
	header->RealLength.low = file.Length;
	header->LogicalLength = file.Length;
	header->FileType = (u8)file.AmsdosType;
	header->LoadAddress = file.LoadAddress;
	header->EntryAddress = file.ExecutionAddress;

	u16 checksum = 0;
	u8* ptr = (u8*)header;
	for (u32 i = 0; i < 67; i++) {
		checksum += *(ptr);
		++ptr;
	}

	header->CheckSum = checksum;
}

void Dsk::dumpCatalogToDisc(void) {
	if (this->_catalogType == CAT_NONE) {
		return;
	}

	u32 sectorSize = this->_specs.sectSizeInRecords * DSK_RECORD_SIZE;
	//u32 remainingBytes = this->_catalogPtr - this->_catalog;
	u8* catPtr = this->_catalog;

	u8 side = 0;
	u8 trackIdx = this->_specs.reservedTracks + side;
	u8 sectorIdx = 0;
	DskSector *sector = &(this->_dskTracks[trackIdx].Sectors[sectorIdx]);
	u8* sectorPtr = sector->Data;

	if (this->_catalogType == CAT_RAW || this->_catalogType == CAT_SF2) {
		memcpy(sectorPtr, catPtr, 16);
		catPtr += 16;
		sectorPtr += 16;
	}

	while (catPtr < this->_catalogPtr) {
		if (catPtr[0] == AMSDOS_EMPTY_BYTE) {
			struct CatalogEntryRaw* entry = (struct CatalogEntryRaw*)catPtr;
			// Raw catalog entry.
			memcpy(sectorPtr, entry, 4);
			//sectorPtr[0] = entry->EmptyByte;
			//sectorPtr[1] = entry->Side;
			//sectorPtr[2] = entry->InitialTrack;
			//sectorPtr[3] = entry->InitialSectorOffset;
			sectorPtr[4] = (u8)(entry->LengthInBytes & 0xFF);
			sectorPtr[5] = (u8)(entry->LengthInBytes >> 8);
			catPtr += sizeof(struct CatalogEntryRaw);
			sectorPtr += sizeof(struct CatalogEntryRaw);
		}
		else {
			struct CatalogEntryAmsdos* entry = (struct CatalogEntryAmsdos*)catPtr;

			if (side != entry->Side) {
				side = entry->Side;
				trackIdx = this->_specs.reservedTracks + side;
				sectorIdx = 0;
				sector = &(this->_dskTracks[trackIdx].Sectors[sectorIdx]);
				sectorPtr = sector->Data;
			}

			// Amsdos entry.
			memcpy(sectorPtr, ((u8*)entry) + 1, sizeof(struct CatalogEntryAmsdos) - 1);
			catPtr += sizeof(struct CatalogEntryAmsdos);
			sectorPtr += sizeof(struct CatalogEntryAmsdos) - 1;
		}
		if (sectorPtr - sector->Data > sectorSize) {
			throw "Error doing math with catalog dump!";
		}
		if (sectorPtr - sector->Data == sectorSize) {
			sectorIdx++;
			if (sectorIdx == _specs.sectorsPerTrack) {
				sectorIdx -= _specs.sectorsPerTrack;
				trackIdx += 2;
			}
			sector = &(this->_dskTracks[trackIdx].Sectors[sectorIdx]);
			sectorPtr = sector->Data;
		}
	}
}

void Dsk::Save(string &path) {
	ofstream f(path, ofstream::binary);
	if (f.good()) {
		this->dumpCatalogToDisc();
		f.write((const char*)&this->_dskHeader, sizeof(struct DskHeader));
		for (int t = 0; t < this->_numTracks * this->_numSides; ++t) {
			DskTrack *track = &this->_dskTracks[t];
			f.write((const char*)track, 24);
			int sIdx = 0;

			int s = 0;
			for (s = 0; s < this->_specs.sectorsPerTrack; ++s) {
				DskSector *sector = &track->Sectors[sIdx];
				f.write((const char*)sector, 8);
				sIdx = (sIdx + 1) % this->_specs.sectorsPerTrack;
			}
			for (; s < DSK_SECTORS_IN_TRACK_HEADER; ++s) {
				DskSector *sector = &track->Sectors[s];
				f.write((const char*)sector, 8);
			}

			sIdx = 0;
			for (s = 0; s < this->_specs.sectorsPerTrack; ++s) {
				DskSector *sector = &track->Sectors[sIdx];
				f.write((const char*)sector->Data, DSK_RECORD_SIZE * this->_specs.sectSizeInRecords);
				sIdx = (sIdx + 1) % this->_specs.sectorsPerTrack;
			}
		}
		f.close();
	}
}
