//===----------------------------------------------------------------------===//
//                         DuckDB
//
// datalake/datalake_util.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/enums/catalog_type.hpp"
#include "duckdb/common/string.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "duckdb/common/vector.hpp"

namespace duckdb {
class ClientContext;
class FileSystem;
class TableFunction;
struct FunctionData;

//! A single object (file) found in the object storage folder backing a datalake catalog
struct DatalakeObject {
	//! Entry name inside the schema (file name without extension, folders joined with '_')
	string name;
	//! Schema the object belongs to
	string schema;
	//! Path of the object as it is handed to the reader function
	string full_path;
	//! Reader function used to read the object
	string reader;
	//! Catalog type of the entry generated for this object
	CatalogType entry_type;
};

class DatalakeUtil {
public:
	//! Name of the schema that holds the objects of the catalog root
	static constexpr const char *DEFAULT_SCHEMA_NAME = "main";

public:
	//! Join a path relative to the catalog root onto the catalog root itself
	static string JoinRootPath(FileSystem &fs, const string &root, const string &relative_path);
	//! Recursively list all files below a directory as paths relative to it
	static vector<string> ListRelativePaths(ClientContext &context, const string &directory);
	//! Map listed files to the catalog entries they generate
	static vector<DatalakeObject> MapObjects(FileSystem &fs, const string &root, const vector<string> &relative_paths);
	//! Reader function for a file extension, or nullptr if the extension is not supported
	static const char *ReaderForExtension(const string &extension);
	//! SELECT statement that reads an object through its reader function
	static string BuildSelectStatement(const string &reader, const string &full_path);
	//! Bind a reader function on a path and return its table function together with the bind result
	static void BindReader(ClientContext &context, const string &reader, const string &full_path,
	                       TableFunction &function, unique_ptr<FunctionData> &bind_data, vector<LogicalType> &types,
	                       vector<string> &names);
	//! Columns returned by a reader function on a path
	static void GetColumns(ClientContext &context, const string &reader, const string &full_path,
	                       vector<LogicalType> &types, vector<string> &names);
};

} // namespace duckdb
