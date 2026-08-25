#pragma once

#include <sdk/te_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Forward declaration of cJSON struct from cJSON parser library.
 */
typedef struct cJSON cJSON;

/**
 * Parse a JSONC (JSON with comments) formatted string into a cJSON object hierarchy.
 * Strips both single-line comments and multi-line comments before parsing.
 *
 * @param jsonc_text Null-terminated string containing JSONC payload. Must not be NULL.
 * @param out_root Pointer to receive the parsed cJSON tree root. Must not be NULL.
 *
 * @return TE_S_OK on success,
 *         TE_E_INVALIDARG if jsonc_text or out_root is NULL,
 *         TE_E_OUTOFMEMORY if memory allocation fails,
 *         TE_E_FAIL if JSON syntax parsing fails.
 *
 * @note Thread Safety: Thread-safe. Caller owns the resulting root object and must free it via TE_JsoncFree().
 */
HRESULT TE_JsoncParse(const char* jsonc_text, cJSON** out_root);

/**
 * Read a JSONC file from disk, strip its comments, and parse it into a cJSON object hierarchy.
 *
 * @param file_path Wide character absolute or relative path to the .jsonc file. Must not be NULL.
 * @param out_root Pointer to receive the parsed cJSON tree root. Must not be NULL.
 *
 * @return TE_S_OK on success,
 *         TE_E_INVALIDARG if file_path or out_root is NULL,
 *         TE_E_FAIL if file open/read fails or JSON syntax parsing fails.
 *
 * @note Thread Safety: Thread-safe. Caller owns the resulting root object and must free it via TE_JsoncFree().
 */
HRESULT TE_JsoncParseFile(const wchar_t* file_path, cJSON** out_root);

/**
 * Free a parsed cJSON tree previously allocated by TE_JsoncParse or TE_JsoncParseFile.
 *
 * @param root Pointer to the root cJSON node to delete. If NULL, this operation is a no-op.
 *
 * @note Thread Safety: Thread-safe as long as no other thread is reading or mutating the same cJSON tree.
 */
void TE_JsoncFree(cJSON* root);

/**
 * Strip single-line comments and multi-line comments from a JSONC string.
 * String literals (including escaped quotes within strings) are preserved unmodified.
 *
 * @param input Null-terminated string containing JSONC text. Must not be NULL.
 *
 * @return Newly allocated (malloc) string containing comment-free JSON, or NULL on error/invalid input.
 *         Caller is responsible for freeing the returned buffer with free().
 *
 * @note Thread Safety: Thread-safe.
 */
char* TE_JsoncStripComments(const char* input);

#ifdef __cplusplus
}
#endif
