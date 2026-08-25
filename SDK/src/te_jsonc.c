#include <sdk/te_jsonc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

/**
 * Strip single-line comments and multi-line comments from a JSONC string.
 * String literals (including escaped quotes and backslashes) are preserved unmodified.
 *
 * @param input Null-terminated string containing JSONC text. Must not be NULL.
 *
 * @return Newly allocated string containing comment-free JSON, or NULL on error.
 *         The caller is responsible for releasing the memory with free().
 */
char* TE_JsoncStripComments(const char* input)
{
    size_t input_len = 0;
    char* output = NULL;
    size_t out_idx = 0;
    int in_string = 0;
    int in_line_comment = 0;
    int in_block_comment = 0;
    int is_escaped = 0;
    size_t i = 0;

    if (input == NULL)
    {
        return NULL;
    }

    input_len = strlen(input);
    output = (char*)malloc(input_len + 1);
    if (output == NULL)
    {
        return NULL;
    }

    for (i = 0; i < input_len; ++i)
    {
        char c = input[i];

        if (in_line_comment)
        {
            if (c == '\n')
            {
                in_line_comment = 0;
                output[out_idx++] = c;
            }
            continue;
        }

        if (in_block_comment)
        {
            if (c == '*' && i + 1 < input_len && input[i + 1] == '/')
            {
                in_block_comment = 0;
                ++i; /* Skip closing slash */
            }
            continue;
        }

        if (in_string)
        {
            output[out_idx++] = c;

            if (is_escaped)
            {
                is_escaped = 0;
            }
            else if (c == '\\')
            {
                is_escaped = 1;
            }
            else if (c == '"')
            {
                in_string = 0;
            }
            continue;
        }

        /* Outside of string literals and comments */
        if (c == '"')
        {
            in_string = 1;
            is_escaped = 0;
            output[out_idx++] = c;
        }
        else if (c == '/' && i + 1 < input_len && input[i + 1] == '/')
        {
            in_line_comment = 1;
            ++i; /* Skip second slash */
        }
        else if (c == '/' && i + 1 < input_len && input[i + 1] == '*')
        {
            in_block_comment = 1;
            ++i; /* Skip asterisk */
        }
        else
        {
            output[out_idx++] = c;
        }
    }

    output[out_idx] = '\0';
    return output;
}

/**
 * Parse a JSONC (JSON with comments) formatted string into a cJSON object hierarchy.
 *
 * @param jsonc_text Null-terminated string containing JSONC payload. Must not be NULL.
 * @param out_root Pointer to receive the parsed cJSON tree root. Must not be NULL.
 *
 * @return TE_S_OK on success,
 *         TE_E_INVALIDARG if jsonc_text or out_root is NULL,
 *         TE_E_OUTOFMEMORY if memory allocation fails,
 *         TE_E_FAIL if JSON syntax parsing fails.
 */
HRESULT TE_JsoncParse(const char* jsonc_text, cJSON** out_root)
{
    char* clean_json = NULL;
    cJSON* root = NULL;

    if (jsonc_text == NULL || out_root == NULL)
    {
        return TE_E_INVALIDARG;
    }

    *out_root = NULL;

    clean_json = TE_JsoncStripComments(jsonc_text);
    if (clean_json == NULL)
    {
        return TE_E_OUTOFMEMORY;
    }

    root = cJSON_Parse(clean_json);
    free(clean_json);
    clean_json = NULL;

    if (root == NULL)
    {
        return TE_E_FAIL;
    }

    *out_root = root;
    return TE_S_OK;
}

/**
 * Read a JSONC file from disk, strip comments, and parse it into a cJSON object hierarchy.
 *
 * @param file_path Wide character absolute or relative path to the .jsonc file. Must not be NULL.
 * @param out_root Pointer to receive the parsed cJSON tree root. Must not be NULL.
 *
 * @return TE_S_OK on success,
 *         TE_E_INVALIDARG if file_path or out_root is NULL,
 *         TE_E_FAIL if file open/read fails or JSON syntax parsing fails.
 */
HRESULT TE_JsoncParseFile(const wchar_t* file_path, cJSON** out_root)
{
    FILE* file = NULL;
    long file_size = 0;
    size_t read_bytes = 0;
    char* buffer = NULL;
    HRESULT hr = TE_S_OK;
    errno_t err = 0;

    if (file_path == NULL || out_root == NULL)
    {
        return TE_E_INVALIDARG;
    }

    *out_root = NULL;

    err = _wfopen_s(&file, file_path, L"rb");
    if (err != 0 || file == NULL)
    {
        return TE_E_FAIL;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return TE_E_FAIL;
    }

    file_size = ftell(file);
    if (file_size < 0)
    {
        fclose(file);
        return TE_E_FAIL;
    }

    if (fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return TE_E_FAIL;
    }

    buffer = (char*)malloc((size_t)file_size + 1);
    if (buffer == NULL)
    {
        fclose(file);
        return TE_E_OUTOFMEMORY;
    }

    read_bytes = fread(buffer, 1, (size_t)file_size, file);
    fclose(file);
    file = NULL;

    buffer[read_bytes] = '\0';

    hr = TE_JsoncParse(buffer, out_root);
    free(buffer);
    buffer = NULL;

    return hr;
}

/**
 * Free a parsed cJSON tree previously allocated by TE_JsoncParse or TE_JsoncParseFile.
 *
 * @param root Pointer to the root cJSON node to delete. If NULL, this operation is a no-op.
 */
void TE_JsoncFree(cJSON* root)
{
    if (root != NULL)
    {
        cJSON_Delete(root);
    }
}
