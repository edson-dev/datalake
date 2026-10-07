//===----------------------------------------------------------------------===//
//                         DuckDB
//
// datalake/datalake_schema_entry.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"
#include "duckdb/common/case_insensitive_map.hpp"
#include "duckdb/common/mutex.hpp"

#include <functional>

namespace duckdb {
class ClientContext;
class DatalakeCatalog;
struct DatalakeObject;

//! A schema of a datalake catalog - it holds the entries generated from the objects of the catalog folder
class DatalakeSchemaEntry : public SchemaCatalogEntry {
public:
	DatalakeSchemaEntry(Catalog &catalog, CreateSchemaInfo &info, DatalakeCatalog &datalake_catalog_p);

public:
	void Scan(ClientContext &context, CatalogType type, const std::function<void(CatalogEntry &)> &callback) override;
	void Scan(CatalogType type, const std::function<void(CatalogEntry &)> &callback) override;

	optional_ptr<CatalogEntry> LookupEntry(CatalogTransaction transaction, const EntryLookupInfo &lookup_info) override;

	optional_ptr<CatalogEntry> CreateIndex(CatalogTransaction transaction, CreateIndexInfo &info,
	                                       TableCatalogEntry &table) override;
	optional_ptr<CatalogEntry> CreateFunction(CatalogTransaction transaction, CreateFunctionInfo &info) override;
	optional_ptr<CatalogEntry> CreateTable(CatalogTransaction transaction, BoundCreateTableInfo &info) override;
	optional_ptr<CatalogEntry> CreateView(CatalogTransaction transaction, CreateViewInfo &info) override;
	optional_ptr<CatalogEntry> CreateSequence(CatalogTransaction transaction, CreateSequenceInfo &info) override;
	optional_ptr<CatalogEntry> CreateTableFunction(CatalogTransaction transaction,
	                                               CreateTableFunctionInfo &info) override;
	optional_ptr<CatalogEntry> CreateCopyFunction(CatalogTransaction transaction,
	                                              CreateCopyFunctionInfo &info) override;
	optional_ptr<CatalogEntry> CreatePragmaFunction(CatalogTransaction transaction,
	                                                CreatePragmaFunctionInfo &info) override;
	optional_ptr<CatalogEntry> CreateCollation(CatalogTransaction transaction, CreateCollationInfo &info) override;
	optional_ptr<CatalogEntry> CreateType(CatalogTransaction transaction, CreateTypeInfo &info) override;

	void DropEntry(ClientContext &context, DropInfo &info) override;
	void Alter(CatalogTransaction transaction, AlterInfo &info) override;

private:
	//! Create the entries of all objects that are not present yet
	void CreateMissingEntries(ClientContext &context, bool ignore_errors);
	//! Create the entry for a single object
	unique_ptr<CatalogEntry> CreateEntry(ClientContext &context, const DatalakeObject &object);
	//! Invoke the callback for every entry of the given type
	void ScanInternal(CatalogType type, const std::function<void(CatalogEntry &)> &callback);
	//! Whether an entry of the given type can be looked up in this schema
	static bool IsEntrySupported(CatalogType type);

private:
	DatalakeCatalog &datalake_catalog;
	mutable mutex entry_lock;
	case_insensitive_map_t<unique_ptr<CatalogEntry>> entries;
};

} // namespace duckdb
