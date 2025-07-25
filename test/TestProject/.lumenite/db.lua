---@meta
---@module "lumenite.db"
local db = {}

---@alias ColumnOptions { primary_key?: boolean, default?: any }

---@alias ColumnDef { name: string, type: string, primary_key: boolean }

---@class ColumnHelper
---@field asc  fun(self: ColumnHelper): string  @“<col> ASC”
---@field desc fun(self: ColumnHelper): string  @“<col> DESC”

---@class QueryTable
---@field filter_by fun(self: QueryTable, filters: { [string]: string|number }): QueryTable @add a WHERE clause
---@field order_by  fun(self: QueryTable, expr: string):         QueryTable @add an ORDER BY clause
---@field limit     fun(self: QueryTable, n: integer):           QueryTable @limit results
---@field get       fun(self: QueryTable, id: string|integer):   table?      @fetch one by id
---@field first     fun(self: QueryTable):                       table?      @fetch first match
---@field all       fun(self: QueryTable):                       table[]     @fetch all matches

---@class ModelTable
---@field new   fun(def: { [string]: any }): table    @create a new instance
---@field query QueryTable                            @the query API
---@field [string] ColumnHelper                       @each column name → helper with :asc()/:desc()

---@class DB
---@field open           fun(filename: string):      DB?, string?        @open/create `./db/filename`
---@field Column         fun(name: string, type: string, options?: ColumnOptions): ColumnDef
---@field Model          fun(def: { __tablename: string, [string]: ColumnDef }): ModelTable
---@field create_all     fun():                      nil                @CREATE TABLE IF NOT EXISTS …
---@field session_add    fun(row: table):            nil                @stage an insert
---@field session_commit fun():                      nil                @commit staged inserts
---@field select_all     fun(tablename: string):     table[]            @SELECT * FROM tablename

--- Opens (or creates) a SQLite file under `./db/`
---@param filename string
---@return DB?, string?       — the DB instance or nil+error
function db.open(filename) end

--- Defines a new column descriptor
---@param name string
---@param type string
---@param options? ColumnOptions
---@return ColumnDef
function db.Column(name, type, options) end

--- Defines a new model
---@param def { __tablename: string, [string]: ColumnDef }
---@return ModelTable
function db.Model(def) end

--- Creates all defined tables
function db.create_all() end

--- Stage a row for insertion
---@param row table
function db.session_add(row) end

--- Commit all staged inserts
function db.session_commit() end

--- Select * from a table
---@param tablename string
---@return table[]
function db.select_all(tablename) end

return db
