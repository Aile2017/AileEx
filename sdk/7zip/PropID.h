#pragma once
// 7-Zip property IDs used with IInArchive::GetProperty / IArchiveUpdateCallback::GetProperty

enum {
    kpidNoProperty       = 0,
    kpidMainSubfile      = 1,
    kpidHandlerItemIndex = 2,
    kpidPath             = 3,   // VT_BSTR
    kpidName             = 4,   // VT_BSTR
    kpidExtension        = 5,   // VT_BSTR
    kpidIsDir            = 6,   // VT_BOOL
    kpidSize             = 7,   // VT_UI8
    kpidPackSize         = 8,   // VT_UI8
    kpidAttrib           = 9,   // VT_UI4
    kpidCTime            = 10,  // VT_FILETIME
    kpidATime            = 11,  // VT_FILETIME
    kpidMTime            = 12,  // VT_FILETIME
    kpidSolid            = 13,
    kpidCommented        = 14,
    kpidEncrypted        = 15,  // VT_BOOL
    kpidSplitBefore      = 16,
    kpidSplitAfter       = 17,
    kpidDictionarySize   = 18,
    kpidCRC              = 19,  // VT_UI4
    kpidType             = 20,  // VT_BSTR
    kpidIsAnti           = 21,
    kpidMethod           = 22,  // VT_BSTR
    kpidHostOS           = 23,
    kpidFileSystem       = 24,
    kpidUser             = 25,
    kpidGroup            = 26,
    kpidBlock            = 27,
    kpidComment          = 28,
    kpidPosition         = 29,
    kpidPrefix           = 30,
    kpidNumSubDirs       = 31,
    kpidNumSubFiles      = 32,
    kpidUnpackVer        = 33,
    kpidVolume           = 34,
    kpidIsVolume         = 35,
    kpidOffset           = 36,
    kpidLinks            = 37,
    kpidNumBlocks        = 38,
    kpidNumVolumes       = 39,
    kpidTimeType         = 40,
    kpidBit64            = 41,
    kpidBigEndian        = 42,
    kpidCpu              = 43,
    kpidPhySize          = 44,
    kpidHeadersSize      = 45,
    kpidChecksum         = 46,
    kpidCharacts         = 47,
    kpidVa               = 48,
    kpidId               = 49,
    kpidShortName        = 50,
    kpidCreatorApp       = 51,
    kpidSectorSize       = 52,
    kpidPosixAttrib      = 53,
    kpidSymLink          = 54,
    kpidError            = 55,
    kpidTotalSize        = 56,
    kpidFreeSpace        = 57,
    kpidClusterSize      = 58,
    kpidVolumeName       = 59,
    kpidLocalName        = 60,
    kpidProvider         = 61,
    kpidNtSecure         = 62,
    kpidIsAltStream      = 63,
    kpidIsAux            = 64,
    kpidIsDeleted        = 65,
    kpidIsTree           = 66,
    kpidSha1             = 67,
    kpidSha256           = 68,
    kpidErrorType        = 69,
    kpidNumErrors        = 70,
    kpidErrorFlags       = 71,
    kpidWarningFlags     = 72,
    kpidWarning          = 73,
    kpidNumStreams        = 74,
    kpidNumAltStreams     = 75,
    kpidAltStreamsSize    = 76,
    kpidVirtualSize      = 77,
    kpidUnpackSize       = 78,
    kpidTotalPhySize     = 79,
    kpidVolumeIndex      = 80,
    kpidSubType          = 81,
    kpidShortComment     = 82,
    kpidCodePage         = 83,
    kpidIsNotArcType     = 84,
    kpidPhySizeCantBeDetected = 85,
    kpidZerosTailIsAllowed    = 86,
    kpidTailSize         = 87,
    kpidEmbeddedStubSize = 88,
    kpidNtReparse        = 89,
    kpidHardLink         = 90,
    kpidINode            = 91,
    kpidStreamId         = 92,
    kpidReadOnly         = 93,
    kpidOutName          = 94,
    kpidCopyLink         = 95,
};

// NArchive::NExtract::NAskMode
namespace NArchive { namespace NExtract { namespace NAskMode {
    const Int32 kExtract = 0;
    const Int32 kTest    = 1;
    const Int32 kSkip    = 2;
}}}

// NArchive::NExtract::NOperationResult
namespace NArchive { namespace NExtract { namespace NOperationResult {
    const Int32 kOK                  = 0;
    const Int32 kUnsupportedMethod   = 1;
    const Int32 kDataError           = 2;
    const Int32 kCRCError            = 3;
    const Int32 kUnavailable         = 4;
    const Int32 kUnexpectedEnd       = 5;
    const Int32 kDataAfterEnd        = 6;
    const Int32 kIsNotArc            = 7;
    const Int32 kHeadersError        = 8;
    const Int32 kWrongPassword       = 9;
    const Int32 kMemError            = 10;
}}}

// NUpdate::NOperationResult
namespace NArchive { namespace NUpdate { namespace NOperationResult {
    const Int32 kOK    = 0;
    const Int32 kError = 1;
}}}
