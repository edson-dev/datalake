//===----------------------------------------------------------------------===//
//                         DuckDB
//
// datalake/datalake_storage.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/storage/storage_extension.hpp"

namespace duckdb {

//! Storage extension that attaches an object storage folder as a datalake catalog
class DatalakeStorageExtension : public StorageExtension {
public:
	DatalakeStorageExtension();
};

} // namespace duckdb
