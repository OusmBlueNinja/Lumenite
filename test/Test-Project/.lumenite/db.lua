---@meta
---@module "lumenite.db"
local db = {}

--[[!!
Lumenite DB — Lua API (EmmyLua annotations)
-------------------------------------------
• All row values returned by query/all/select_all are strings (SQLite text) or nil.
• Query methods are chainable and do not execute until :first(), :all(), :get(), or :count().
• :get() and :first() return a *proxy* table; reading fields reads current values, assigning
  (e.g., proxy.name = "X") queues an UPDATE applied on db.session_commit().
• INTEGER PRIMARY KEY columns are recommended for ids (rowid).
• Defaults: when you pass `options.default` to Column(...), CREATE TABLE will include a DEFAULT
  literal (numeric unquoted, strings quoted).
!!]]

---@alias ColumnOptions { primary_key?: boolean, default?: any }

---@class ColumnDef
---@field name           string
---@field type           string
---@field primary_key    boolean
---@field default_value  string  @empty string if unset (stringified literal for DDL)

---@class ColumnHelper
---@field asc  fun(self: ColumnHelper): string  @returns "<col> ASC"
---@field desc fun(self: ColumnHelper): string  @returns "<col> DESC"

---@class QueryTable
---@field filter_by fun(self: QueryTable, filters: { [string]: string|number|boolean|nil }): QueryTable
---@field order_by  fun(self: QueryTable, expr: string): QueryTable
---@field limit     fun(self: QueryTable, n: integer): QueryTable
---@field get       fun(self: QueryTable, id: string|integer): table?    @proxy row or nil
---@field first     fun(self: QueryTable): table?                         @proxy row or nil
---@field all       fun(self: QueryTable):  table[]                       @array of plain row tables
---@field count     fun(self: QueryTable):  integer                       @row count for current filters

---@class ModelTable
---@field new   fun(def: { [string]: any }): table    @creates a new instance (to be inserted)
---@field query QueryTable                            @chainable query builder
---@field [string] ColumnHelper                       @each column name → helper with :asc()/:desc()

---@class DB
---@field open             fun(filename: string):      DB?, string?  @open/create `./db/<filename>`
---@field Column           fun(name: string, type: string, options?: ColumnOptions): ColumnDef
---@field Model            fun(def: { __tablename: string, [string]: ColumnDef }): ModelTable
---@field create_all       fun():                      nil
---@field session_add      fun(row: table):            nil            @stage an INSERT (from Model.new)
---@field session_commit   fun():                      nil            @apply staged INSERTs/UPDATEs
---@field select_all       fun(tablename: string):     table[]        @SELECT * FROM <tablename>
---@field begin            fun():                      nil            @BEGIN transaction
---@field commit           fun():                      nil            @COMMIT transaction
---@field rollback         fun():                      nil            @ROLLBACK transaction
---@field last_insert_id   fun():                      integer        @sqlite3_last_insert_rowid()
---@field delete           fun(tablename: string, id: string|integer): nil  @DELETE FROM <table> WHERE id=?

--- Opens (or creates) a SQLite file under `./db/`.
--- Also ensures `./db` and `./log` folders exist and enables `PRAGMA foreign_keys = ON`.
---@param filename string
---@return DB? db, string? err  -- the DB instance or nil+error
function db.open(filename) end

--- Defines a new column descriptor for use in db.Model.
--- If options.default is numeric, it's emitted unquoted; strings are quoted in DDL.
---@param name string
---@param type string
---@param options? ColumnOptions
---@return ColumnDef
function db.Column(name, type, options) end

--- Defines a new model/table. Example:
--- local User = db.Model{ __tablename="users", id=db.Column("id","INTEGER",{primary_key=true}) }
---@param def { __tablename: string, [string]: ColumnDef }
---@return ModelTable
function db.Model(def) end

--- Creates all registered tables with CREATE TABLE IF NOT EXISTS.
function db.create_all() end

--- Stage a row for insertion (from Model.new{...}). Applied on db.session_commit().
---@param row table
function db.session_add(row) end

--- Apply all staged INSERTs and queued UPDATEs (from proxy assignments).
function db.session_commit() end

--- Values are strings or nil.
---@param tablename string
---@return table[]
function db.select_all(tablename) end

--- BEGIN a transaction.
function db.begin() end

--- COMMIT the current transaction.
function db.commit() end

--- ROLLBACK the current transaction.
function db.rollback() end

--- Returns sqlite3_last_insert_rowid() of the current connection.
---@return integer
function db.last_insert_id() end

---@param tablename string
---@param id string|integer
function db.delete(tablename, id) end

return db
