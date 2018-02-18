#ifndef _DSKGEN_DSK_H_
#define _DSKGEN_DSK_H_

#include "Types.hpp"
#include "Options.hpp"
#include "FileToProcess.hpp"
#include <iomanip>
#include <iostream>
#include <math.h>   

using namespace std;

class Dsk {

    struct XDPB _specs;
    CatalogType _catalogType;
    u8 _numSides;
    u8 _numTracks;
    u8 _catalogSizeInSectors;
    u32 _blockSize;

    vector<FileToProcess> _filesInDsk;

    struct DskHeader _dskHeader;
    struct DskTrack _dskTracks[256];

    u8 _currentTrack;
    u8 _currentSector;
    u8 _currentSide;
    u8 _currentBlock;

	u8 _catalog[512 * sizeof(CatalogEntryAmsdos)];
	u8* _catalogPtr;

    void setTrack(u8 side, u8 track);
    void advanceSectors(u8 sectors);
    void updateCurrentBlock(void);
    void writeDataToSectors(u8* data, u32 length);

    void initCatalog(void);
    void initDskHeader(void);
    void addToCatalog(FileToProcess &file);
    void dumpCatalogToDisc(string &path);
	void dumpAsmCatalog(string &path);

    bool checkAmsdosHeader(u8* buffer);
    void fillAmsdosHeader(struct AmsdosHeader* header, const FileToProcess& file);

public:
    Dsk(u8 numSides, const struct XDPB& params, CatalogType catType);

    int AddBootFile(const string& bootFilePath);
    int AddFile(FileToProcess &file);
    void Save(string &path);
};

#endif