## ADDED Requirements

### Requirement: Codebig entry-point guard for Direct CDN mode
The `codebigdownloadFile()` function SHALL reject requests where `context->direct_cdn == true` by returning immediately before any Codebig signing or download logic executes. This provides defense-in-depth against `isDwnlBlock()` server_type flips that could redirect a Direct CDN download into the Codebig path.

#### Scenario: Direct CDN request blocked at Codebig entry point
- **WHEN** `codebigdownloadFile()` is called with `context->direct_cdn == true`
- **THEN** the function SHALL log an informational message and return -1 without performing any Codebig URL signing or download attempt

#### Scenario: Non-Direct-CDN request proceeds normally
- **WHEN** `codebigdownloadFile()` is called with `context->direct_cdn == false`
- **THEN** the function SHALL proceed with its existing Codebig signing and download logic unchanged

#### Scenario: isDwnlBlock flip does not bypass guard
- **WHEN** `isDwnlBlock()` flips `server_type` from `HTTP_SSR_DIRECT` to `HTTP_SSR_CODEBIG` AND `context->direct_cdn == true`
- **THEN** the resulting call to `codebigdownloadFile()` SHALL still be blocked by the entry-point guard
