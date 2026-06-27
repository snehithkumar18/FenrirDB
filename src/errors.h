#ifndef FENRIRDB_ERRORS_H
#define FENRIRDB_ERRORS_H

#include <string>

namespace FenrirDB {

enum class DBErrorCode {
    SUCCESS = 0,
    ERR_INVALID_PARAMETER = 1,
    ERR_IO_ERROR = 2,
    ERR_CACHE_FULL = 3,
    ERR_PAGE_NOT_FOUND = 4,
    ERR_RECORD_NOT_FOUND = 5,
    ERR_INDEX_CORRUPT = 6,
    ERR_TYPE_MISMATCH = 7,
    ERR_OUT_OF_BOUNDS = 8,
    ERR_GENERIC = 9
};

inline std::string db_error_to_string(DBErrorCode code) {
    switch (code) {
        case DBErrorCode::SUCCESS: return "Success";
        case DBErrorCode::ERR_INVALID_PARAMETER: return "Invalid Parameter";
        case DBErrorCode::ERR_IO_ERROR: return "I/O Error";
        case DBErrorCode::ERR_CACHE_FULL: return "Cache Full";
        case DBErrorCode::ERR_PAGE_NOT_FOUND: return "Page Not Found";
        case DBErrorCode::ERR_RECORD_NOT_FOUND: return "Record Not Found";
        case DBErrorCode::ERR_INDEX_CORRUPT: return "Index Corrupted";
        case DBErrorCode::ERR_TYPE_MISMATCH: return "Type Mismatch";
        case DBErrorCode::ERR_OUT_OF_BOUNDS: return "Out of Bounds";
        case DBErrorCode::ERR_GENERIC: return "Generic Error";
        default: return "Unknown Error";
    }
}

} // namespace FenrirDB

#endif // FENRIRDB_ERRORS_H
