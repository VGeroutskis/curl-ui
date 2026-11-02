# OpenAPI import: design

## Goal

Import an API description, any published version, as a collection the user can run at once:
pick a file in the Import dialog, get one request per operation, with servers, variables, auth
and bodies already in place. Nothing runs until the user presses Run.

## Scope

Versions, per https://spec.openapis.org/oas/:

| Version | Format | Source |
|---|---|---|
| OpenAPI 3.2.x, 3.1.x, 3.0.x | JSON, YAML | one file, plus files it references |
| Swagger 2.0 | JSON, YAML | one file, plus files it references |
| Swagger 1.2 | JSON | a resource listing and its API declarations, in one folder |
| Swagger 1.0, 1.1 | JSON | as 1.2, where the file has the same shape |

Swagger 1.0 and 1.1 have no published schema of the kind 1.2 has, so they are accepted by shape:
what does not fit is refused with a message that says what was found. This is best effort and the
README says so.

Not in this work:

- Export to OpenAPI.
- Import from a URL, or `$ref` to a URL. The app itself opens no network connection; only curl does.
  A remote `$ref` is left unresolved and counted in the report.
- Webhooks and callbacks as requests: the API sends those, not the user. They are counted in the report.
- Postman import, which stays a separate phase.

## How it looks

The Import dialog keeps the box for a curl command. **Open file** uses the file picker. The file is
recognised by what it holds:

- It has `openapi` or `swagger`: an API description. Importing makes a new collection.
- Otherwise: the text goes into the box as a curl command, as today.

After an import, a report in the notice bar: how many requests, in which collection, and anything
skipped, for example `Imported 42 requests into Pet Store. Skipped: 2 webhooks, 1 remote $ref.`
A file that cannot be read is refused with the reason and a line number, and nothing is created.

## The mapping

| In the file | In the app |
|---|---|
| `info.title` | Collection name; if taken, `Name 2` |
| First tag of an operation | Folder; no tag goes at the top |
| Operation | Request named `summary`, else `operationId`, else `METHOD /path` |
| `servers` / `host`+`basePath`+`schemes` / `basePath` | One environment per server, with `baseUrl`. No server at all: `baseUrl` is empty and Run asks for it |
| Server variables | Filled into `baseUrl` with their defaults: the value of a variable is not read again at run time |
| Path, path parameters | URL `{{baseUrl}}/pets/{{petId}}` |
| Query parameters | Params rows: required on, optional off. Value from `example`, `default` or the first `enum`, else `{{name}}` |
| Header parameters | Headers rows, same rule. `Accept`, `Content-Type` and `Authorization` as parameters are ignored, as the specs require |
| Cookie parameters | One `--cookie` with `name={{name}}` |
| `security` (operation, else top level), first alternative | Auth form |
| `http` basic, `http` bearer | Basic, Bearer with `{{username}}`, `{{password}}`, `{{token}}` |
| `http` digest | Digest |
| `apiKey` in header or query | API key form with `{{name}}` |
| `apiKey` in cookie | `--cookie` with `name={{name}}` |
| `oauth2`, `openIdConnect` | Bearer with `{{token}}`; the app does not run the flow, and the report says so |
| `mutualTLS` | `--cert` and `--key` cards with empty values, which have Browse |
| Request body | First usable media type, in this order: JSON, form-urlencoded, multipart, other text, binary |
| JSON | JSON body from `example`, `examples` or generated from the schema |
| form-urlencoded, multipart | URL encoded or Form data rows from the schema's properties; `format: binary` rows are File rows |
| Other text, binary | Raw, or File with an empty path, and a `Content-Type` header |

Secret variables: `password`, `token` and API key names are made secret, using the same rule as the
Variables table (`vars_suggest_secret`), with empty values. Values already in the file as defaults
stay plain. Variables are per collection, so importing touches no other collection.

Generated bodies: from a schema, in this order: `example`, `default`, `const`, first `enum`,
`oneOf`/`anyOf` first choice, `allOf` merged, object with its properties, array with one item, and by
type `"string"`, `0`, `false`. Depth limit 12, and a cycle stops with `null`.

### Version differences handled

- 3.1, 3.2: `type` as an array, `const`, `examples` as an array, `$ref` with siblings, `jsonSchemaDialect` ignored.
- 3.2: `query` method and `additionalOperations`: a custom method is set with `--request`. Hierarchical
  `tags` with `parent` become nested folders. `in: querystring` is one raw query row. `$self` is ignored.
- 2.0: `consumes`, `in: body`, `in: formData`, `securityDefinitions`, `definitions`, `parameters` at the top.
- 1.x: `apis`, `operations`, `method`/`httpMethod`, `nickname`, `paramType`, `authorizations`.

`$ref` resolves within the file and to local files, relative to the file holding it, with a cycle
guard and a limit of 256 files and 64 MB in all. A relative `$ref` cannot leave the folder of the first
file without being listed in the report, so that a hostile file cannot quietly read others: its content
would only become request text, but the user should see it.

## Parts

All SDL-free, in `src/core`, tested by ctest:

- `yaml.c/h`: YAML to the existing `jv` tree. Handles block and flow collections, plain and quoted
  scalars, block scalars `|` and `>` with chomping, comments, anchors, aliases, merge keys, multiple-line
  plain scalars, `---`, and JSON as a subset. Refuses tabs as indentation, tags other than core ones, and
  several documents in a file, with line and column. Not the whole YAML 1.2 specification. Nesting limit
  128 and an alias expansion limit, against "billion laughs".
- `oas_load.c/h`: reads the file and what it references, and resolves `$ref`.
- `oas_model.c/h`: one small model of operations, filled from 1.x, 2.0 or 3.x by one reader per family.
- `oas_import.c/h`: model to `collection` plus `var_store`, and the report.

In the app: `app_import_openapi` in `store.c`, called from the Import dialog; a `--import-openapi FILE`
test hook.

## Testing

- `tests/test_yaml.c`: the YAML the specs use, each refusal, the limits.
- `tests/test_openapi.c`: mapping table above, each row, for each family.
- `tests/fixtures/openapi/`: small files written for this, one per version: 1.0, 1.1, 1.2 (folder), 2.0,
  3.0.0, 3.0.4, 3.1.0, 3.1.2, 3.2.0, 3.2.1, in JSON and YAML where the version has both. The same API in
  each, so that 2.0 and later are compared to one expected result.
- `tests/live/test_openapi.py`: through the real app down to what a local server receives: method, path,
  query, headers, auth, each body kind.
- Also, on Linux CI only and allowed to skip offline: the official Pet Store examples, fetched by the
  test, not committed, must import without a refusal.

Sanitizers run over the parsers, with a damaged-file loop: every fixture cut at every 64th byte must
give an error or a result, never a crash.

## Limits a person will meet

- A big spec (thousands of operations) makes a big collection; the tree has no paging beyond what it has today.
- No schema validation of what the user later edits.
- XML bodies are generated as empty text, with the right `Content-Type`.
- An operation with several security alternatives uses the first; the others are lost.
- A re-import makes a new collection; it does not merge.
